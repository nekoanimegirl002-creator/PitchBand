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

    // ~50 ms grain window; keep it even
    int w = (int) std::lround (0.050 * sampleRate);
    w += (w & 1);
    windowSize = (float) w;

    const int size = juce::nextPowerOfTwo (w + 16);
    mask = size - 1;
    delayBuf.setSize (2, size);
    delayBuf.clear();
    writePos = 0;
    phase = 0.0f;

    smoothedSemis = *apvts.getRawParameterValue ("bend")
                  * *apvts.getRawParameterValue ("range");

    smoothedMix.reset (sampleRate, 0.02);
    smoothedMix.setCurrentAndTargetValue (*apvts.getRawParameterValue ("mix"));

    smoothedPower.reset (sampleRate, 0.02);
    smoothedPower.setCurrentAndTargetValue (*apvts.getRawParameterValue ("power") > 0.5f ? 1.0f : 0.0f);

    // Dry path is delayed by half a window so wet and dry stay aligned
    setLatencySamples (w / 2 + 2);
}

// 4-point Hermite interpolation. 'delay' is in samples (>= 0, fractional).
float PitchBendProcessor::readHermite (int ch, float delay) const noexcept
{
    const float rp = (float) writePos - delay - 2.0f;
    const int   i0 = (int) std::floor (rp);
    const float f  = rp - (float) i0;

    const float* d = delayBuf.getReadPointer (ch);
    const float y0 = d[(i0 - 1) & mask];
    const float y1 = d[ i0      & mask];
    const float y2 = d[(i0 + 1) & mask];
    const float y3 = d[(i0 + 2) & mask];

    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + y1;
}

void PitchBendProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    const int n     = buffer.getNumSamples();

    // Wheel position (-1..1) x range (1..24 semitones) = target pitch shift
    const float bend  = *apvts.getRawParameterValue ("bend");
    const float range = *apvts.getRawParameterValue ("range");
    const float targetSemis = bend * range;

    const float smoothMs = *apvts.getRawParameterValue ("smooth");
    smoothedMix.setTargetValue (*apvts.getRawParameterValue ("mix"));
    smoothedPower.setTargetValue (*apvts.getRawParameterValue ("power") > 0.5f ? 1.0f : 0.0f);

    // One-pole smoothing coefficient for the pitch value
    const float coeff = smoothMs < 0.01f
                          ? 0.0f
                          : std::exp (-1.0f / (0.001f * smoothMs * (float) sr));

    const float W = windowSize;
    const int dryDelay = (int) (W * 0.5f) + 2;
    constexpr float pi = juce::MathConstants<float>::pi;

    for (int i = 0; i < n; ++i)
    {
        smoothedSemis = targetSemis + (smoothedSemis - targetSemis) * coeff;
        const float mix   = smoothedMix.getNextValue();
        const float power = smoothedPower.getNextValue();
        const float ratio = std::exp2 (smoothedSemis / 12.0f);

        // Write the input into the delay line
        for (int ch = 0; ch < numCh; ++ch)
            delayBuf.setSample (ch, writePos, buffer.getSample (ch, i));

        // Two taps, half a window apart, crossfaded with sin^2 / cos^2
        const float phaseB = (phase + 0.5f >= 1.0f) ? phase - 0.5f : phase + 0.5f;
        const float dA = phase  * W;
        const float dB = phaseB * W;
        const float s  = std::sin (pi * phase);
        const float wA = s * s;
        const float wB = 1.0f - wA;

        // Near 0 semitones, fade to the (aligned) dry signal so the two taps
        // do not produce a comb filter on a static setting
        const float wetAmount = juce::jmin (1.0f, std::abs (smoothedSemis) / 0.1f);
        const float wetMix = mix * wetAmount * power;

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float wet = wA * readHermite (ch, dA) + wB * readHermite (ch, dB);
            const float dry = delayBuf.getSample (ch, (writePos - dryDelay) & mask);
            buffer.setSample (ch, i, dry * (1.0f - wetMix) + wet * wetMix);
        }

        // Read pointer moves at 'ratio' speed relative to the write pointer
        phase += (1.0f - ratio) / W;
        while (phase < 0.0f)  phase += 1.0f;
        while (phase >= 1.0f) phase -= 1.0f;

        writePos = (writePos + 1) & mask;
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
