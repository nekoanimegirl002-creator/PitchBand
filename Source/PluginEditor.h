#pragma once
#include "PluginProcessor.h"

// Soft pastel-pink look: low contrast against the background, easy on the eyes
class PinkLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PinkLookAndFeel();
};

class PitchBendEditor : public juce::AudioProcessorEditor
{
public:
    explicit PitchBendEditor (PitchBendProcessor&);
    ~PitchBendEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    PitchBendProcessor& proc;
    PinkLookAndFeel laf;

    juce::Slider bendSlider   { juce::Slider::LinearHorizontal, juce::Slider::TextBoxAbove };
    juce::Slider rangeSlider  { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Slider smoothSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Slider mixSlider    { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };

    juce::Label bendLabel, rangeLabel, smoothLabel, mixLabel;

    juce::TextButton powerButton { "ON" };
    juce::TextButton resetButton { "Reset" };

    SliderAttachment bendAtt, rangeAtt, smoothAtt, mixAtt;
    juce::ParameterAttachment powerAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchBendEditor)
};
