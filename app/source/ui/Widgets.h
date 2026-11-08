#pragma once

#include <JuceHeader.h>
#include "ViridianLookAndFeel.h"
#include "vf/dsp/Tuner.h"

using APVTS = juce::AudioProcessorValueTreeState;

/** Rotary knob with a label underneath, bound to one parameter. */
class LabeledKnob : public juce::Component
{
public:
    LabeledKnob (APVTS& state, const juce::String& paramId, const juce::String& label);
    void resized() override;
    juce::Slider& getSlider() noexcept { return slider; }

private:
    juce::Slider slider;
    juce::Label caption;
    APVTS::SliderAttachment attachment;
};

/** LED toggle bound to a bool parameter. */
class LedSwitch : public juce::Component
{
public:
    LedSwitch (APVTS& state, const juce::String& paramId, const juce::String& label);
    void resized() override { button.setBounds (getLocalBounds()); }
    juce::ToggleButton& getButton() noexcept { return button; }

private:
    juce::ToggleButton button;
    APVTS::ButtonAttachment attachment;
};

/** Labelled combo box bound to a choice parameter. */
class ChoiceBox : public juce::Component
{
public:
    ChoiceBox (APVTS& state, const juce::String& paramId, const juce::String& label);
    void resized() override;

private:
    juce::ComboBox box;
    juce::Label caption;
    APVTS::ComboBoxAttachment attachment;
};

/** Three latching channel buttons with LEDs (Clean / Crunch / Lead). */
class ChannelSelector : public juce::Component
{
public:
    ChannelSelector (APVTS& state, const juce::String& paramId);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void select (int index);
    juce::OwnedArray<juce::TextButton> buttons;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    int current = 0;
};

/** Segmented peak meter with peak hold and clip indicator. */
class LevelMeter : public juce::Component
{
public:
    explicit LevelMeter (juce::String labelText) : label (std::move (labelText)) {}
    void pushPeak (float linearPeak);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { clipped = false; repaint(); }

private:
    juce::String label;
    float levelDb = -96.0f, holdDb = -96.0f;
    int holdFrames = 0;
    bool clipped = false;
};

/** Chromatic tuner readout: note name + cents needle + in-tune LED. */
class TunerDisplay : public juce::Component
{
public:
    void setReading (const vf::dsp::TunerReading& r);
    void paint (juce::Graphics&) override;

private:
    vf::dsp::TunerReading reading;
    float smoothedCents = 0.0f;
    int framesSinceValid = 1000;
};

/** Small text readout used for CPU load and latency. */
class StatReadout : public juce::Component
{
public:
    explicit StatReadout (juce::String title) : titleText (std::move (title)) {}
    void setValueText (const juce::String& v, juce::Colour c);
    void paint (juce::Graphics&) override;

private:
    juce::String titleText, valueText { "--" };
    juce::Colour valueColour { juce::Colours::white };
};
