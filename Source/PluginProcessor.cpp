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

    // Grain window ~50 ms (even), matching segment ~16 ms, search range ~12 ms
    int w = (int) std::lround (0.050 * sampleRate);
    w += (w & 1);
    Wf = (float) w;
    corrLen     = ((int) std::lround (0.016 * sampleRate)) & ~3; // multiple of 4
    searchRange = (int) std::lround (0.012 * sampleRate);

    dMin = (float) (corrLen + 4);
    dMax = dMin + Wf;

    const int size = juce::nextPowerOfTwo ((int) dMax + 64);
    mask = size - 1;
    delayBuf.setSize (2, size);
    delayBuf.clear();
    monoBuf.assign ((size_t) size, 0.0f);
    refSeg.assign ((size_t) (corrLen / 4 + 1), 0.0f);
    writePos = 0;

    // Two grains, half a window apart
    dA = dMin;
    dB = dMin + 0.5f * Wf;

    smoothedSemis = *apvts.getRawParameterValue ("bend")
                  * *apvts.getRawParameterValue ("range");

    smoothedMix.reset (sampleRate, 0.02);
    smoothedMix.setCurrentAndTargetValue (*apvts.getRawParameterValue ("mix"));

    smoothedPower.reset (sampleRate, 0.02);
    smoothedPower.setCurrentAndTargetValue (*apvts.getRawParameterValue ("power") > 0.5f ? 1.0f : 0.0f);

    // Dry path is delayed by the average grain delay so wet and dry stay aligned.
    // The same value is reported to the host so it can compensate the latency.
    dryDelay = (int) (dMin + 0.5f * Wf) + 2;
    setLatencySamples (dryDelay);
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

// Picks where a grain that ran out of its window should restart.
// Candidate positions are compared (normalised cross-correlation) with what the other
// grain is playing, and the best match wins, so both grains are in phase when they crossfade.
// 'toTop' = true when the grain left through the bottom of the window (pitch up).
float PitchBendProcessor::chooseReseat (float dOther, bool toTop)
{
    const int refD = (int) std::lround (dOther);
    const int n    = corrLen / 4;

    // How far the other grain has travelled through its window (0..1)
    const float tauO = toTop ? (dMax - dOther) / Wf : (dOther - dMin) / Wf;

    // Keep the two grains at least a quarter window apart so they never collapse together
    const float sMax = juce::jmin ((float) searchRange, juce::jmax (0.0f, (tauO - 0.25f) * Wf));

    for (int k = 0; k < n; ++k)
        refSeg[(size_t) k] = monoBuf[(size_t) ((writePos - refD + 4 * k) & mask)];

    float best = -1.0e30f;
    int bestS = 0;

    for (int s = 0; s <= (int) sMax; s += 2)
    {
        const int dc = toTop ? (int) dMax - s : (int) dMin + s;
        float dot = 0.0f, en = 1.0e-9f;

        for (int k = 0; k < n; ++k)
        {
            const float c = monoBuf[(size_t) ((writePos - dc + 4 * k) & mask)];
            dot += refSeg[(size_t) k] * c;
            en  += c * c;
        }

        const float score = dot / std::sqrt (en);
        if (score > best)
        {
            best = score;
            bestS = s;
        }
    }

    return toTop ? dMax - (float) bestS : dMin + (float) bestS;
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

    // Window shape of a grain: 0 at both ends, 1 in the middle
    auto windowAt = [this] (float d)
    {
        const float x = juce::jlimit (0.0f, 1.0f, (d - dMin) / Wf);
        const float s = std::sin (juce::MathConstants<float>::pi * x);
        return s * s;
    };

    for (int i = 0; i < n; ++i)
    {
        smoothedSemis = targetSemis + (smoothedSemis - targetSemis) * coeff;
        const float mix   = smoothedMix.getNextValue();
        const float power = smoothedPower.getNextValue();
        const float ratio = std::exp2 (smoothedSemis / 12.0f);

        // Write the input into the delay line (and a mono copy used for matching)
        float monoSum = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float x = buffer.getSample (ch, i);
            delayBuf.setSample (ch, writePos, x);
            monoSum += x;
        }
        monoBuf[(size_t) writePos] = monoSum / (float) juce::jmax (1, numCh);

        // Crossfade weights of the two grains, normalised so they always add up to 1
        float wA = windowAt (dA);
        float wB = windowAt (dB);
        const float norm = 1.0f / juce::jmax (0.25f, wA + wB);
        wA *= norm;
        wB *= norm;

        // Near 0 semitones, fade to the (aligned) dry signal
        const float wetAmount = juce::jmin (1.0f, std::abs (smoothedSemis) / 0.1f);
        const float wetMix = mix * wetAmount * power;

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float wet = wA * readHermite (ch, dA) + wB * readHermite (ch, dB);
            const float dry = delayBuf.getSample (ch, (writePos - dryDelay) & mask);
            buffer.setSample (ch, i, dry * (1.0f - wetMix) + wet * wetMix);
        }

        // Read position moves at 'ratio' speed relative to the write position
        const float step = 1.0f - ratio;
        dA += step;
        dB += step;

        if (dA < dMin)       dA = chooseReseat (dB, true);
        else if (dA > dMax)  dA = chooseReseat (dB, false);

        if (dB < dMin)       dB = chooseReseat (dA, true);
        else if (dB > dMax)  dB = chooseReseat (dA, false);

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
