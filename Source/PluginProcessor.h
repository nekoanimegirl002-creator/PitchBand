#pragma once
#include <juce_audio_utils/juce_audio_utils.h>

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

    // Delay-line pitch shifter state
    juce::AudioBuffer<float> delayBuf;
    int mask = 0;
    int writePos = 0;
    float windowSize = 2048.0f; // W, in samples (even number)
    float phase = 0.0f;         // 0..1, position of tap A in the window
    double sr = 44100.0;

    float smoothedSemis = 0.0f;
    juce::SmoothedValue<float> smoothedMix, smoothedPower;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchBendProcessor)
};
