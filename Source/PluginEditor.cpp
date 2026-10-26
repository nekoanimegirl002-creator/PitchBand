#include "PluginEditor.h"

namespace
{
    const juce::Colour bgColour     (0xfffff5f8); // very light pink
    const juce::Colour panelColour  (0xffffe8ef);
    const juce::Colour trackColour  (0xfff7d9e4);
    const juce::Colour accentColour (0xfff3a9c3); // soft pink
    const juce::Colour deepColour   (0xffe58aab); // slightly deeper pink for thumbs
    const juce::Colour textColour   (0xff6e4b5c); // dusty mauve, readable but gentle
    const juce::Colour buttonColour (0xffffd9e5);
}

PinkLookAndFeel::PinkLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, bgColour);

    setColour (juce::Slider::backgroundColourId,          trackColour);
    setColour (juce::Slider::trackColourId,               accentColour);
    setColour (juce::Slider::thumbColourId,               deepColour);
    setColour (juce::Slider::rotarySliderFillColourId,    accentColour);
    setColour (juce::Slider::rotarySliderOutlineColourId, trackColour);
    setColour (juce::Slider::textBoxTextColourId,         textColour);
    setColour (juce::Slider::textBoxBackgroundColourId,   juce::Colours::transparentWhite);
    setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentWhite);
    setColour (juce::Slider::textBoxHighlightColourId,    accentColour.withAlpha (0.4f));

    setColour (juce::Label::textColourId,                 textColour);
    setColour (juce::CaretComponent::caretColourId,       deepColour);

    setColour (juce::TextButton::buttonColourId,          buttonColour);
    setColour (juce::TextButton::buttonOnColourId,        accentColour);
    setColour (juce::TextButton::textColourOffId,         textColour);
    setColour (juce::TextButton::textColourOnId,          textColour);
}

PitchBendEditor::PitchBendEditor (PitchBendProcessor& p)
    : juce::AudioProcessorEditor (&p),
      proc (p),
      bendAtt   (p.apvts, "bend",   bendSlider),
      rangeAtt  (p.apvts, "range",  rangeSlider),
      smoothAtt (p.apvts, "smooth", smoothSlider),
      mixAtt    (p.apvts, "mix",    mixSlider),
      powerAtt  (*p.apvts.getParameter ("power"),
                 [this] (float v)
                 {
                     const bool on = v > 0.5f;
                     powerButton.setToggleState (on, juce::dontSendNotification);
                     powerButton.setButtonText (on ? "ON" : "OFF");
                 })
{
    setLookAndFeel (&laf);

    auto setupLabel = [this] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (l);
    };

    setupLabel (bendLabel,   "Bend");
    setupLabel (rangeLabel,  "Range (+/- semitones)");
    setupLabel (smoothLabel, "Smooth");
    setupLabel (mixLabel,    "Mix");

    for (auto* s : { &bendSlider, &rangeSlider, &smoothSlider, &mixSlider })
        addAndMakeVisible (s);

    bendSlider.setTextBoxStyle   (juce::Slider::TextBoxAbove, false, 120, 24);
    rangeSlider.setTextBoxStyle  (juce::Slider::TextBoxBelow, false, 70, 20);
    smoothSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 20);
    mixSlider.setTextBoxStyle    (juce::Slider::TextBoxBelow, false, 70, 20);

    // Bend shows the real pitch shift in semitones (wheel position x range)
    auto currentRange = [this] { return (double) *proc.apvts.getRawParameterValue ("range"); };

    bendSlider.textFromValueFunction = [currentRange] (double v)
    {
        const double st = v * currentRange();
        return juce::String (st > 0.005 ? "+" : "") + juce::String (st, 2) + " st";
    };
    bendSlider.valueFromTextFunction = [currentRange] (const juce::String& t)
    {
        return juce::jlimit (-1.0, 1.0, t.getDoubleValue() / juce::jmax (1.0, currentRange()));
    };

    rangeSlider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + " st"; };
    rangeSlider.valueFromTextFunction = [] (const juce::String& t) { return juce::jlimit (1.0, 24.0, t.getDoubleValue()); };

    smoothSlider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + " ms"; };
    smoothSlider.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue(); };

    mixSlider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; };
    mixSlider.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue() / 100.0; };

    // When the range changes, the displayed semitone value of the wheel changes too
    rangeSlider.onValueChange = [this] { bendSlider.updateText(); };

    for (auto* s : { &bendSlider, &rangeSlider, &smoothSlider, &mixSlider })
        s->updateText();

    // Power (on/off) and Reset buttons
    addAndMakeVisible (powerButton);
    addAndMakeVisible (resetButton);

    powerButton.setClickingTogglesState (true);
    powerButton.onClick = [this]
    {
        powerAtt.setValueAsCompleteGesture (powerButton.getToggleState() ? 1.0f : 0.0f);
    };
    powerAtt.sendInitialUpdate();

    resetButton.onClick = [this] { proc.resetToDefaults(); };

    setSize (460, 320);
}

PitchBendEditor::~PitchBendEditor()
{
    setLookAndFeel (nullptr);
}

void PitchBendEditor::paint (juce::Graphics& g)
{
    g.fillAll (bgColour);

    auto panel = getLocalBounds().toFloat().reduced (8.0f);
    g.setColour (panelColour);
    g.fillRoundedRectangle (panel, 16.0f);
    g.setColour (accentColour.withAlpha (0.6f));
    g.drawRoundedRectangle (panel, 16.0f, 1.5f);

    // Title with two small sparkles
    g.setColour (textColour);
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    g.drawText ("PITCH BEND", juce::Rectangle<int> (80, 20, getWidth() - 160, 30),
                juce::Justification::centred);

    auto sparkle = [&g] (juce::Point<float> c, float size)
    {
        juce::Path star;
        star.addStar (c, 4, size * 0.28f, size, 0.0f);
        g.setColour (accentColour);
        g.fillPath (star);
    };
    const float cx = (float) getWidth() * 0.5f;
    sparkle ({ cx - 78.0f, 35.0f }, 7.0f);
    sparkle ({ cx + 78.0f, 35.0f }, 5.0f);
}

void PitchBendEditor::resized()
{
    auto area = getLocalBounds().reduced (20);

    auto top = area.removeFromTop (30);
    powerButton.setBounds (top.removeFromLeft (60));
    resetButton.setBounds (top.removeFromRight (70));

    area.removeFromTop (14);
    bendLabel.setBounds  (area.removeFromTop (18));
    bendSlider.setBounds (area.removeFromTop (70));
    area.removeFromTop (12);

    const int col = area.getWidth() / 3;
    auto place = [] (juce::Label& l, juce::Slider& s, juce::Rectangle<int> r)
    {
        l.setBounds (r.removeFromTop (18));
        s.setBounds (r);
    };
    place (rangeLabel,  rangeSlider,  area.removeFromLeft (col));
    place (smoothLabel, smoothSlider, area.removeFromLeft (col));
    place (mixLabel,    mixSlider,    area);
}
