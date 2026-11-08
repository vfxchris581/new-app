#include "Widgets.h"

using namespace juce;

static ViridianLookAndFeel* vfLaf (Component& c)
{
    return dynamic_cast<ViridianLookAndFeel*> (&c.getLookAndFeel());
}

static VFTheme themeOf (Component& c)
{
    if (auto* l = vfLaf (c)) return l->getTheme();
    return VFTheme::dark();
}

//==============================================================================
LabeledKnob::LabeledKnob (APVTS& state, const String& paramId, const String& label)
    : attachment (state, paramId, slider)
{
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.2f, MathConstants<float>::pi * 2.8f, true);
    slider.setPopupDisplayEnabled (true, true, this);
    slider.setDoubleClickReturnValue (true, (double) state.getParameter (paramId)->convertFrom0to1 (
                                                state.getParameter (paramId)->getDefaultValue()));
    slider.setTooltip (label + " (double-click to reset)");
    addAndMakeVisible (slider);

    caption.setText (label.toUpperCase(), dontSendNotification);
    caption.setJustificationType (Justification::centred);
    addAndMakeVisible (caption);
}

void LabeledKnob::resized()
{
    auto r = getLocalBounds();
    const int labelH = jlimit (12, 22, r.getHeight() / 5);
    caption.setBounds (r.removeFromBottom (labelH));
    slider.setBounds (r);
}

//==============================================================================
LedSwitch::LedSwitch (APVTS& state, const String& paramId, const String& label)
    : attachment (state, paramId, button)
{
    button.setButtonText (label);
    addAndMakeVisible (button);
}

//==============================================================================
ChoiceBox::ChoiceBox (APVTS& state, const String& paramId, const String& label)
    : attachment (state, paramId, [&]() -> ComboBox&
      {
          if (auto* p = dynamic_cast<AudioParameterChoice*> (state.getParameter (paramId)))
              box.addItemList (p->choices, 1);
          return box;
      }())
{
    addAndMakeVisible (box);
    caption.setText (label.toUpperCase(), dontSendNotification);
    caption.setJustificationType (Justification::centredLeft);
    addAndMakeVisible (caption);
}

void ChoiceBox::resized()
{
    auto r = getLocalBounds();
    caption.setBounds (r.removeFromTop (r.getHeight() / 3));
    box.setBounds (r);
}

//==============================================================================
ChannelSelector::ChannelSelector (APVTS& state, const String& paramId)
{
    auto* param = state.getParameter (paramId);
    auto* choice = dynamic_cast<AudioParameterChoice*> (param);
    jassert (choice != nullptr);

    for (int i = 0; i < choice->choices.size(); ++i)
    {
        auto* b = buttons.add (new TextButton (choice->choices[i].toUpperCase()));
        b->setClickingTogglesState (false);
        b->onClick = [this, i] { select (i); };
        addAndMakeVisible (b);
    }

    attachment = std::make_unique<ParameterAttachment> (*param, [this] (float denormValue)
    {
        current = roundToInt (denormValue);
        for (int i = 0; i < buttons.size(); ++i)
            buttons[i]->setToggleState (i == current, dontSendNotification);
        repaint();
    });
    attachment->sendInitialUpdate();
}

void ChannelSelector::select (int index)
{
    attachment->setValueAsCompleteGesture ((float) index);
}

void ChannelSelector::paint (Graphics& g)
{
    const auto t = themeOf (*this);
    for (int i = 0; i < buttons.size(); ++i)
    {
        const auto b = buttons[i]->getBounds().toFloat();
        const float d = jmin (10.0f, b.getHeight() * 0.3f);
        ViridianLookAndFeel::drawLed (g, Rectangle<float> (d, d).withCentre ({ b.getCentreX(), b.getY() - d }), t.led, i == current);
    }
}

void ChannelSelector::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (r.getHeight() / 3); // room for LEDs
    const int w = r.getWidth() / jmax (1, buttons.size());
    for (auto* b : buttons)
        b->setBounds (r.removeFromLeft (w).reduced (3, 0));
}

//==============================================================================
void LevelMeter::pushPeak (float linearPeak)
{
    const float db = Decibels::gainToDecibels (linearPeak, -96.0f);
    levelDb = db > levelDb ? db : jmax (-96.0f, levelDb - 1.5f); // ~45 dB/s fall at 30 fps
    if (db >= holdDb) { holdDb = db; holdFrames = 45; }
    else if (--holdFrames < 0) holdDb = jmax (-96.0f, holdDb - 1.0f);
    if (linearPeak >= 0.999f) clipped = true;
    repaint();
}

void LevelMeter::paint (Graphics& g)
{
    const auto t = themeOf (*this);
    auto r = getLocalBounds().toFloat();
    g.setColour (t.textDim);
    g.setFont (ViridianLookAndFeel::labelFont (10.0f));
    g.drawText (label, r.removeFromBottom (12.0f), Justification::centred);

    auto clipLed = r.removeFromTop (8.0f).withSizeKeepingCentre (8.0f, 6.0f);
    g.setColour (clipped ? Colour (0xffff3b30) : t.panelEdge);
    g.fillRoundedRectangle (clipLed, 1.5f);
    r.removeFromTop (2.0f);

    g.setColour (Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (r, 2.0f);

    const auto toY = [&] (float db) { return jmap (jlimit (-60.0f, 0.0f, db), -60.0f, 0.0f, r.getBottom(), r.getY()); };
    const int segments = 24;
    const float segH = r.getHeight() / (float) segments;
    for (int i = 0; i < segments; ++i)
    {
        const float segDb = -60.0f + 60.0f * ((float) i + 0.5f) / (float) segments;
        const auto seg = Rectangle<float> (r.getX() + 2.0f, r.getBottom() - segH * (float) (i + 1) + 1.0f, r.getWidth() - 4.0f, segH - 2.0f);
        const Colour c = segDb > -3.0f ? Colour (0xffff453a) : (segDb > -12.0f ? Colour (0xffffb02e) : t.accent);
        g.setColour (segDb <= levelDb ? c : c.withAlpha (0.12f));
        g.fillRect (seg);
    }
    if (holdDb > -60.0f)
    {
        g.setColour (t.text);
        g.fillRect (r.getX() + 2.0f, toY (holdDb), r.getWidth() - 4.0f, 1.5f);
    }
}

//==============================================================================
void TunerDisplay::setReading (const vf::dsp::TunerReading& r)
{
    if (r.valid && r.clarity > 0.75)
    {
        reading = r;
        smoothedCents = framesSinceValid > 5 ? (float) r.cents : smoothedCents * 0.6f + (float) r.cents * 0.4f;
        framesSinceValid = 0;
    }
    else
    {
        ++framesSinceValid;
    }
    repaint();
}

void TunerDisplay::paint (Graphics& g)
{
    const auto t = themeOf (*this);
    auto r = getLocalBounds().toFloat();
    g.setColour (Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (t.panelEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
    r.reduce (8.0f, 4.0f);

    const bool active = framesSinceValid < 12;
    const bool inTune = active && std::abs (smoothedCents) < 3.0f;

    auto noteArea = r.removeFromLeft (r.getHeight() * 1.3f);
    g.setColour (active ? (inTune ? t.accent : Colours::white) : t.textDim.withAlpha (0.5f));
    g.setFont (ViridianLookAndFeel::labelFont (r.getHeight() * 0.72f));
    g.drawText (active ? String (reading.noteName()) : String ("--"), noteArea, Justification::centred);

    auto ledArea = r.removeFromRight (r.getHeight() * 0.5f);
    ViridianLookAndFeel::drawLed (g, ledArea.withSizeKeepingCentre (9.0f, 9.0f), t.led, inTune);

    // Cents scale: -50 .. +50
    auto scale = r.reduced (6.0f, r.getHeight() * 0.18f);
    g.setColour (t.textDim);
    for (int c = -50; c <= 50; c += 10)
    {
        const float x = jmap ((float) c, -50.0f, 50.0f, scale.getX(), scale.getRight());
        const float len = (c == 0) ? scale.getHeight() : scale.getHeight() * 0.45f;
        g.drawLine (x, scale.getBottom() - len, x, scale.getBottom(), c == 0 ? 1.5f : 1.0f);
    }
    if (active)
    {
        const float x = jmap (jlimit (-50.0f, 50.0f, smoothedCents), -50.0f, 50.0f, scale.getX(), scale.getRight());
        g.setColour (inTune ? t.accent : Colour (0xffffb02e));
        g.fillRoundedRectangle (x - 2.0f, scale.getY() - 2.0f, 4.0f, scale.getHeight() + 4.0f, 2.0f);
        g.setFont (ViridianLookAndFeel::labelFont (10.0f, false));
        g.setColour (t.textDim);
        g.drawText (String (smoothedCents >= 0 ? "+" : "") + String (smoothedCents, 1) + " ct  " + String (reading.frequency, 1) + " Hz",
                    r.removeFromTop (12.0f), Justification::centredRight);
    }
}

//==============================================================================
void StatReadout::setValueText (const String& v, Colour c)
{
    if (v != valueText || c != valueColour) { valueText = v; valueColour = c; repaint(); }
}

void StatReadout::paint (Graphics& g)
{
    const auto t = themeOf (*this);
    auto r = getLocalBounds().toFloat();
    g.setColour (t.textDim);
    g.setFont (ViridianLookAndFeel::labelFont (10.0f));
    g.drawText (titleText, r.removeFromTop (r.getHeight() * 0.4f), Justification::centred);
    g.setColour (valueColour);
    g.setFont (ViridianLookAndFeel::labelFont (jmin (16.0f, r.getHeight() * 0.8f)));
    g.drawText (valueText, r, Justification::centred);
}
