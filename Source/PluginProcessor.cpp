#include "PluginProcessor.h"
#include "PluginEditor.h"

PitchBendProcessor::PitchBendProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout PitchBendProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    // Bend wheel: -1 .. +1, like a synth pitch wheel. Centre = no change.
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "bend", 1 }, "Bend",
        NormalisableRange<float> (-1.0f, 1.0f, 0.0005f), 0.0f));

    // Bend range in semitones (what the wheel reaches at its extremes): 1 .. 24
    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { "range", 1 }, "Range", 1, 24, 12));

    // Glide time in ms (smooths jumps / automation steps)
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "smooth", 1 }, "Smooth",
        NormalisableRange<float> (0.0f, 2000.0f, 0.1f, 0.4f), 30.0f));

    // Dry/wet
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "mix", 1 }, "Mix",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));

    // Power (on/off)
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "power", 1 }, "Power", true));

    // Mode: Default (pitch and timbre move together) or Complex (formant control)
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "mode", 1 }, "Mode", StringArray { "Default", "Complex" }, 0));

    // Formant shift in semitones (Complex mode)
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "formant", 1 }, "Formant",
        NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f));

    // Snap: the bend moves in whole semitones only
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "snap", 1 }, "Snap", false));

    return layout;
}

void PitchBendProcessor::resetToDefaults()
{
    for (auto* p : getParameters())
    {
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (rp->getDefaultValue());
            rp->endChangeGesture();
        }
    }
}

bool PitchBendProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void PitchBendProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;

    // FFT size of the Complex engine; it also sets the common latency of both engines
    const int fftSize = sampleRate <= 50000.0 ? 2048 : (sampleRate <= 100000.0 ? 4096 : 8192);

    grain.setTuning (60.0, 15.0, 16.0);
    const int latency = grain.prepare (sampleRate, fftSize);
    for (auto& v : pv)
        v.prepare (sampleRate, fftSize, 8);

    // The Complex engine delays by fftSize samples; pad it up to the common latency
    padDelay = juce::jmax (0, latency - fftSize);
    const int padSize = juce::nextPowerOfTwo (padDelay + 8);
    for (auto& b : padBuf)
        b.assign ((size_t) padSize, 0.0f);
    padMask = padSize - 1;
    padPos = 0;

    // Dry path delayed by the same latency
    dryDelay = latency;
    const int drySize = juce::nextPowerOfTwo (latency + 8);
    dryBuf.setSize (2, drySize);
    dryBuf.clear();
    dryMask = drySize - 1;
    dryPos = 0;

    smoothedSemis = *apvts.getRawParameterValue ("bend")
                  * *apvts.getRawParameterValue ("range");

    smoothedMix.reset (sampleRate, 0.02);
    smoothedMix.setCurrentAndTargetValue (*apvts.getRawParameterValue ("mix"));

    smoothedPower.reset (sampleRate, 0.02);
    smoothedPower.setCurrentAndTargetValue (*apvts.getRawParameterValue ("power") > 0.5f ? 1.0f : 0.0f);

    smoothedMode.reset (sampleRate, 0.025);
    smoothedMode.setCurrentAndTargetValue (*apvts.getRawParameterValue ("mode") > 0.5f ? 1.0f : 0.0f);

    smoothedFormant.reset (sampleRate, 0.03);
    smoothedFormant.setCurrentAndTargetValue (*apvts.getRawParameterValue ("formant"));

    setLatencySamples (latency);
}

void PitchBendProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    const int n     = buffer.getNumSamples();
    if (numCh < 1)
        return;

    // Wheel position (-1..1) x range (1..24 semitones) = target pitch shift
    const float bend  = *apvts.getRawParameterValue ("bend");
    const float range = *apvts.getRawParameterValue ("range");
    float targetSemis = bend * range;

    // Snap: only whole semitones
    if (*apvts.getRawParameterValue ("snap") > 0.5f)
        targetSemis = std::round (targetSemis);

    const float smoothMs = *apvts.getRawParameterValue ("smooth");
    smoothedMix.setTargetValue (*apvts.getRawParameterValue ("mix"));
    smoothedPower.setTargetValue (*apvts.getRawParameterValue ("power") > 0.5f ? 1.0f : 0.0f);
    smoothedMode.setTargetValue (*apvts.getRawParameterValue ("mode") > 0.5f ? 1.0f : 0.0f);
    smoothedFormant.setTargetValue (*apvts.getRawParameterValue ("formant"));

    // One-pole smoothing coefficient for the pitch value
    const float coeff = smoothMs < 0.01f
                          ? 0.0f
                          : std::exp (-1.0f / (0.001f * smoothMs * (float) sr));

    for (int i = 0; i < n; ++i)
    {
        smoothedSemis = targetSemis + (smoothedSemis - targetSemis) * coeff;
        const float mix    = smoothedMix.getNextValue();
        const float power  = smoothedPower.getNextValue();
        const float modeW  = smoothedMode.getNextValue();     // 0 = Default, 1 = Complex
        const float fSemis = smoothedFormant.getNextValue();

        const float ratio  = std::exp2 (smoothedSemis / 12.0f);
        const float fRatio = std::exp2 (fSemis / 12.0f);

        const float inL = buffer.getSample (0, i);
        const float inR = numCh > 1 ? buffer.getSample (1, i) : inL;

        // Dry signal, delayed by the latency
        dryBuf.setSample (0, dryPos, inL);
        dryBuf.setSample (1, dryPos, inR);
        const int rd = (dryPos - dryDelay) & dryMask;
        const float dryL = dryBuf.getSample (0, rd);
        const float dryR = dryBuf.getSample (1, rd);
        dryPos = (dryPos + 1) & dryMask;

        // Default engine
        float gL = 0.0f, gR = 0.0f;
        grain.process (inL, inR, ratio, gL, gR);

        // Complex engine (both engines always run so a mode switch is seamless)
        float cL = pv[0].process (inL, ratio, fRatio);
        float cR = numCh > 1 ? pv[1].process (inR, ratio, fRatio) : cL;

        if (padDelay > 0)
        {
            padBuf[0][(size_t) padPos] = cL;
            padBuf[1][(size_t) padPos] = cR;
            const size_t rp = (size_t) ((padPos - padDelay) & padMask);
            cL = padBuf[0][rp];
            cR = padBuf[1][rp];
            padPos = (padPos + 1) & padMask;
        }

        const float wetL = (1.0f - modeW) * gL + modeW * cL;
        const float wetR = (1.0f - modeW) * gR + modeW * cR;

        // Near 0 semitones (and 0 formant shift) fade to the aligned dry signal
        const float waDefault = juce::jmin (1.0f, std::abs (smoothedSemis) / 0.1f);
        const float waComplex = juce::jmin (1.0f, (std::abs (smoothedSemis) + std::abs (fSemis)) / 0.1f);
        const float wetAmount = (1.0f - modeW) * waDefault + modeW * waComplex;
        const float wetMix = mix * wetAmount * power;

        buffer.setSample (0, i, dryL * (1.0f - wetMix) + wetL * wetMix);
        if (numCh > 1)
            buffer.setSample (1, i, dryR * (1.0f - wetMix) + wetR * wetMix);
    }
}

juce::AudioProcessorEditor* PitchBendProcessor::createEditor()
{
    return new PitchBendEditor (*this);
}

void PitchBendProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PitchBendProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PitchBendProcessor();
}
