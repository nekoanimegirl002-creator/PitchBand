#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <vector>

class PitchBendProcessor : public juce::AudioProcessor
{
public:
    PitchBendProcessor();
    ~PitchBendProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Pitch Bend"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Sets every parameter back to its default value (used by the Reset button)
    void resetToDefaults();

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    float readHermite (int ch, float delay) const noexcept;
    float chooseReseat (float dOther, bool toTop);

    // Delay-line pitch shifter state.
    // Two "grains" (taps) read the delay line at a speed that depends on the pitch ratio.
    // When a grain runs out of its window it jumps back, and the jump target is chosen
    // by waveform correlation so that the two grains stay in phase (no pumping).
    juce::AudioBuffer<float> delayBuf;
    std::vector<float> monoBuf, refSeg;
    int mask = 0;
    int writePos = 0;
    float Wf = 2400.0f;     // grain window length in samples
    float dMin = 0.0f;      // smallest grain delay (needs room to look ahead)
    float dMax = 0.0f;      // largest grain delay = dMin + Wf
    int searchRange = 0;    // how far (samples) a grain may be moved to find a match
    int corrLen = 0;        // length (samples) of the segment used for matching
    int dryDelay = 0;       // dry path delay = reported latency
    float dA = 0.0f, dB = 0.0f; // current delays of the two grains
    double sr = 44100.0;

    float smoothedSemis = 0.0f;
    juce::SmoothedValue<float> smoothedMix, smoothedPower;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchBendProcessor)
};
