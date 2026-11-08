#include "PluginEditor.h"

namespace
{
    Palette pinkPalette()
    {
        Palette p;
        p.bg     = juce::Colour (0xfffff5f8); // very light pink
        p.panel  = juce::Colour (0xffffe8ef);
        p.track  = juce::Colour (0xfff7d9e4);
        p.accent = juce::Colour (0xfff3a9c3); // soft pink
        p.deep   = juce::Colour (0xffe58aab); // slightly deeper pink for thumbs
        p.text   = juce::Colour (0xff6e4b5c); // dusty mauve, readable but gentle
        p.soft   = juce::Colour (0xffa8869a);
        p.button = juce::Colour (0xffffd9e5);
        return p;
    }

    Palette navyPalette()
    {
        Palette p;
        p.bg     = juce::Colour (0xff0e1525); // classic dark navy
        p.panel  = juce::Colour (0xff16213a);
        p.track  = juce::Colour (0xff263554);
        p.accent = juce::Colour (0xff3f7be0);
        p.deep   = juce::Colour (0xff7aa8ff);
        p.text   = juce::Colour (0xffdde6f7);
        p.soft   = juce::Colour (0xff8a9bbd);
        p.button = juce::Colour (0xff1f2d4d);
        return p;
    }
}

void ThemeLookAndFeel::applyPalette (const Palette& p)
{
    setColour (juce::ResizableWindow::backgroundColourId, p.bg);

    setColour (juce::Slider::backgroundColourId,          p.track);
    setColour (juce::Slider::trackColourId,               p.accent);
    setColour (juce::Slider::thumbColourId,               p.deep);
    setColour (juce::Slider::rotarySliderFillColourId,    p.accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, p.track);
    setColour (juce::Slider::textBoxTextColourId,         p.text);
    setColour (juce::Slider::textBoxBackgroundColourId,   juce::Colours::transparentWhite);
    setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentWhite);
    setColour (juce::Slider::textBoxHighlightColourId,    p.accent.withAlpha (0.4f));

    setColour (juce::Label::textColourId,                 p.text);
    setColour (juce::CaretComponent::caretColourId,       p.deep);

    setColour (juce::TextButton::buttonColourId,          p.button);
    setColour (juce::TextButton::buttonOnColourId,        p.accent);
    setColour (juce::TextButton::textColourOffId,         p.text);
    setColour (juce::TextButton::textColourOnId,          p.text);

    setColour (juce::ToggleButton::textColourId,          p.text);
    setColour (juce::ToggleButton::tickColourId,          p.deep);
    setColour (juce::ToggleButton::tickDisabledColourId,  p.soft);

    setColour (juce::ComboBox::backgroundColourId,        p.button);
    setColour (juce::ComboBox::textColourId,              p.text);
    setColour (juce::ComboBox::outlineColourId,           p.accent.withAlpha (0.5f));
    setColour (juce::ComboBox::buttonColourId,            p.button);
    setColour (juce::ComboBox::arrowColourId,             p.text);

    setColour (juce::PopupMenu::backgroundColourId,            p.panel);
    setColour (juce::PopupMenu::textColourId,                  p.text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, p.track);
    setColour (juce::PopupMenu::highlightedTextColourId,       p.text);
}

PitchBendEditor::PitchBendEditor (PitchBendProcessor& p)
    : juce::AudioProcessorEditor (&p),
      proc (p),
      bendAtt    (p.apvts, "bend",    bendSlider),
      formantAtt (p.apvts, "formant", formantSlider),
      rangeAtt   (p.apvts, "range",   rangeSlider),
      smoothAtt  (p.apvts, "smooth",  smoothSlider),
      mixAtt     (p.apvts, "mix",     mixSlider),
      snapAtt    (p.apvts, "snap",    snapButton),
      powerAtt   (*p.apvts.getParameter ("power"),
                  [this] (float v)
                  {
                      const bool on = v > 0.5f;
                      powerButton.setToggleState (on, juce::dontSendNotification);
                      powerButton.setButtonText (on ? "ON" : "OFF");
                  })
{
    // Theme (saved with the project)
    themeIndex = juce::jlimit (0, 1, (int) proc.apvts.state.getProperty ("theme", 0));
    pal = themeIndex == 0 ? pinkPalette() : navyPalette();
    laf.applyPalette (pal);
    setLookAndFeel (&laf);

    themeButton.iconColour = pal.text;
    themeButton.onClick = [this] { showThemeMenu(); };
    addAndMakeVisible (themeButton);

    auto setupLabel = [this] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (l);
    };

    setupLabel (bendLabel,    "Bend");
    setupLabel (formantLabel, "Formant (Complex mode)");
    setupLabel (rangeLabel,   "Range (+/- semitones)");
    setupLabel (smoothLabel,  "Smooth");
    setupLabel (mixLabel,     "Mix");

    for (auto* s : { (juce::Slider*) &bendSlider, &formantSlider, &rangeSlider, &smoothSlider, &mixSlider })
        addAndMakeVisible (s);

    bendSlider.setTextBoxStyle    (juce::Slider::TextBoxAbove, false, 120, 22);
    formantSlider.setTextBoxStyle (juce::Slider::TextBoxAbove, false, 120, 22);
    rangeSlider.setTextBoxStyle   (juce::Slider::TextBoxBelow, false, 70, 20);
    smoothSlider.setTextBoxStyle  (juce::Slider::TextBoxBelow, false, 70, 20);
    mixSlider.setTextBoxStyle     (juce::Slider::TextBoxBelow, false, 70, 20);

    auto currentRange = [this] { return rangeSlider.getValue(); };
    auto snapOn       = [this] { return snapButton.getToggleState(); };

    // Snap step of the -1..+1 wheel = one semitone
    bendSlider.getSnapStep = [currentRange, snapOn]
    {
        return snapOn() ? 1.0 / juce::jmax (1.0, currentRange()) : 0.0;
    };

    // Bend shows the real pitch shift in semitones (wheel position x range)
    bendSlider.textFromValueFunction = [currentRange, snapOn] (double v)
    {
        double st = v * currentRange();
        if (snapOn()) st = std::round (st);
        if (std::abs (st) < 0.004) st = 0.0;
        return juce::String (st > 0.0 ? "+" : "") + juce::String (st, snapOn() ? 0 : 2) + " st";
    };
    bendSlider.valueFromTextFunction = [currentRange, snapOn] (const juce::String& t)
    {
        const double r = juce::jmax (1.0, currentRange());
        double v = juce::jlimit (-1.0, 1.0, t.getDoubleValue() / r);
        if (snapOn()) v = std::round (v * r) / r;
        return v;
    };

    formantSlider.textFromValueFunction = [] (double v)
    {
        if (std::abs (v) < 0.004) v = 0.0;
        return juce::String (v > 0.0 ? "+" : "") + juce::String (v, 2) + " st";
    };
    formantSlider.valueFromTextFunction = [] (const juce::String& t) { return juce::jlimit (-12.0, 12.0, t.getDoubleValue()); };

    rangeSlider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + " st"; };
    rangeSlider.valueFromTextFunction = [] (const juce::String& t) { return juce::jlimit (1.0, 24.0, t.getDoubleValue()); };

    smoothSlider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + " ms"; };
    smoothSlider.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue(); };

    mixSlider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; };
    mixSlider.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue() / 100.0; };

    // Range / snap changes affect how the bend value is shown; after the user changes them the wheel is re-snapped
    rangeSlider.onValueChange = [this] { bendSlider.updateText(); };
    rangeSlider.onDragEnd     = [this] { updateSnap(); };
    snapButton.onClick        = [this] { updateSnap(); };

    for (auto* s : { (juce::Slider*) &bendSlider, &formantSlider, &rangeSlider, &smoothSlider, &mixSlider })
        s->updateText();

    // Mode selector (Default / Complex)
    modeBox.addItemList ({ "Default", "Complex" }, 1);
    addAndMakeVisible (modeBox);
    modeAtt = std::make_unique<ComboAttachment> (proc.apvts, "mode", modeBox);
    modeBox.onChange = [this] { updateModeUi(); };
    updateModeUi();

    // Snap toggle
    addAndMakeVisible (snapButton);

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

    setSize (480, 404);
}

PitchBendEditor::~PitchBendEditor()
{
    setLookAndFeel (nullptr);
}

void PitchBendEditor::updateModeUi()
{
    const bool complex = modeBox.getSelectedId() == 2;
    formantSlider.setEnabled (complex);
    formantSlider.setAlpha (complex ? 1.0f : 0.35f);
    formantLabel.setAlpha (complex ? 1.0f : 0.35f);
}

void PitchBendEditor::updateSnap()
{
    bendSlider.updateText();

    if (snapButton.getToggleState())
        bendSlider.setValue (bendSlider.snapTo (bendSlider.getValue()), juce::sendNotificationSync);
}

void PitchBendEditor::showThemeMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Theme");
    menu.addItem (1, "Light pink",        true, themeIndex == 0);
    menu.addItem (2, "Classic dark blue", true, themeIndex == 1);

    juce::Component::SafePointer<PitchBendEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&themeButton),
                        [safe] (int result)
                        {
                            if (safe != nullptr && result > 0)
                                safe->setTheme (result - 1);
                        });
}

void PitchBendEditor::setTheme (int index)
{
    themeIndex = juce::jlimit (0, 1, index);
    proc.apvts.state.setProperty ("theme", themeIndex, nullptr);

    pal = themeIndex == 0 ? pinkPalette() : navyPalette();
    laf.applyPalette (pal);
    themeButton.iconColour = pal.text;

    sendLookAndFeelChange();
    repaint();
}

void PitchBendEditor::paint (juce::Graphics& g)
{
    g.fillAll (pal.bg);

    auto panel = getLocalBounds().toFloat().reduced (8.0f);
    g.setColour (pal.panel);
    g.fillRoundedRectangle (panel, 16.0f);
    g.setColour (pal.accent.withAlpha (0.6f));
    g.drawRoundedRectangle (panel, 16.0f, 1.5f);

    // Title, version and author
    g.setColour (pal.text);
    g.setFont (juce::FontOptions (19.0f, juce::Font::bold));
    g.drawText ("PITCH BAND", juce::Rectangle<int> (0, 52, getWidth(), 26), juce::Justification::centred);

    g.setColour (pal.soft);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("v0.1.2 beta  by N3KO", juce::Rectangle<int> (0, 78, getWidth(), 16), juce::Justification::centred);

    auto sparkle = [&g, this] (juce::Point<float> c, float size)
    {
        juce::Path star;
        star.addStar (c, 4, size * 0.28f, size, 0.0f);
        g.setColour (pal.accent);
        g.fillPath (star);
    };
    const float cx = (float) getWidth() * 0.5f;
    sparkle ({ cx - 88.0f, 65.0f }, 7.0f);
    sparkle ({ cx + 88.0f, 65.0f }, 5.0f);
}

void PitchBendEditor::resized()
{
    auto area = getLocalBounds().reduced (18);

    // Top row: theme icon, power, mode selector, reset
    auto top = area.removeFromTop (30);
    themeButton.setBounds (top.removeFromLeft (30));
    top.removeFromLeft (8);
    powerButton.setBounds (top.removeFromLeft (56));
    resetButton.setBounds (top.removeFromRight (70));
    modeBox.setBounds (top.withSizeKeepingCentre (130, 28));

    area.removeFromTop (52); // title and version are painted here

    // Bend wheel with the snap switch
    auto bendRow = area.removeFromTop (20);
    snapButton.setBounds (bendRow.removeFromRight (170));
    bendLabel.setBounds (bendRow);
    bendSlider.setBounds (area.removeFromTop (64));

    area.removeFromTop (6);

    // Formant (only active in Complex mode)
    formantLabel.setBounds (area.removeFromTop (18));
    formantSlider.setBounds (area.removeFromTop (54));

    area.removeFromTop (8);

    // Knobs
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
