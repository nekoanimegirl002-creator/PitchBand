#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <vector>
#include "GrainShifter.h"
#include "PhaseVocoder.h"

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

    const juce::String getName() const override { return "Pitch Band"; }
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

    // Default mode engine (grains + correlation) and Complex mode engine (phase vocoder + formants)
    GrainShifter grain;
    PhaseVocoder pv[2];

    // Dry signal delayed by the reported latency, and a small pad that aligns the Complex engine
    juce::AudioBuffer<float> dryBuf;
    int dryMask = 0, dryPos = 0, dryDelay = 0;
    std::vector<float> padBuf[2];
    int padMask = 0, padPos = 0, padDelay = 0;

    double sr = 44100.0;
    float smoothedSemis = 0.0f;
    juce::SmoothedValue<float> smoothedMix, smoothedPower, smoothedMode, smoothedFormant;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchBendProcessor)
};
