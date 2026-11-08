#pragma once
#include "PluginProcessor.h"
#include <functional>
#include <memory>

// Colours of one theme
struct Palette
{
    juce::Colour bg, panel, track, accent, deep, text, soft, button;
};

class ThemeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void applyPalette (const Palette&);
};

// Slider whose value can snap to whole semitones (1 / range steps of the -1..+1 wheel)
class SnapSlider : public juce::Slider
{
public:
    using juce::Slider::Slider;

    std::function<double()> getSnapStep;

    double snapTo (double v) const
    {
        const double step = getSnapStep ? getSnapStep() : 0.0;
        if (step > 0.0)
            return juce::jlimit (-1.0, 1.0, std::round (v / step) * step);
        return v;
    }

    double snapValue (double attemptedValue, DragMode) override { return snapTo (attemptedValue); }
};

// Small "change theme" icon: a circle that is half filled
class ThemeIconButton : public juce::Button
{
public:
    ThemeIconButton() : juce::Button ("theme") {}

    juce::Colour iconColour { juce::Colours::white };

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (5.0f);
        const float d = juce::jmin (r.getWidth(), r.getHeight());
        auto circle = juce::Rectangle<float> (d, d).withCentre (r.getCentre());

        g.setColour (iconColour.withAlpha (down ? 0.6f : (over ? 1.0f : 0.8f)));
        g.drawEllipse (circle, 1.6f);

        juce::Path half;
        half.addPieSegment (circle, 0.0f, juce::MathConstants<float>::pi, 0.0f);
        g.fillPath (half);
    }
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
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void setTheme (int index);
    void showThemeMenu();
    void updateModeUi();
    void updateSnap();

    PitchBendProcessor& proc;
    ThemeLookAndFeel laf;
    Palette pal;
    int themeIndex = 0;

    SnapSlider   bendSlider    { juce::Slider::LinearHorizontal, juce::Slider::TextBoxAbove };
    juce::Slider formantSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxAbove };
    juce::Slider rangeSlider   { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Slider smoothSlider  { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Slider mixSlider     { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };

    juce::Label bendLabel, formantLabel, rangeLabel, smoothLabel, mixLabel;

    juce::TextButton powerButton { "ON" };
    juce::TextButton resetButton { "Reset" };
    juce::ToggleButton snapButton { "Snap to semitones" };
    juce::ComboBox modeBox;
    ThemeIconButton themeButton;

    SliderAttachment bendAtt, formantAtt, rangeAtt, smoothAtt, mixAtt;
    ButtonAttachment snapAtt;
    std::unique_ptr<ComboAttachment> modeAtt;
    juce::ParameterAttachment powerAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchBendEditor)
};
