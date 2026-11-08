#pragma once

#include <JuceHeader.h>
#include "Widgets.h"

/** Rack-style header: brand, preset slot, tuner, CPU, meters, utility buttons. */
class TopBar : public juce::Component
{
public:
    TopBar();
    void paint (juce::Graphics&) override;
    void resized() override;

    TunerDisplay tuner;
    StatReadout cpu { "CPU" }, latency { "LATENCY" };
    LevelMeter inMeter { "IN" }, outMeter { "OUT" };
    juce::TextButton presetButton { "INIT  -  Factory" };
    juce::TextButton themeButton { "LIGHT" }, settingsButton { "AUDIO" }, aboutButton { "ABOUT" };
    juce::TextButton liveInputButton { "INPUT MUTED" };

private:
    juce::Image logo;
};

/** The large amplifier head with all front-panel controls. */
class AmpHeadPanel : public juce::Component
{
public:
    explicit AmpHeadPanel (APVTS& state);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Rectangle<float> faceplateArea() const;

    void modelChanged (int index);

    ChannelSelector channel;
    LabeledKnob gain, bass, middle, treble, presence, resonance, master;
    LedSwitch bright, deep, boost;
    AmpModelSelector model;
    ChoiceBox tubeSwap;
    juce::String badgeName, badgeCategory;
};

/** Input/gate/output strip to the left/right of the amp. */
class IOPanel : public juce::Component
{
public:
    explicit IOPanel (APVTS& state);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    LabeledKnob input, gateThreshold, output;
    LedSwitch gateOn;
};

/** Cabinet section. Phase 1: built-in 4x12 response on/off. Phase 3: IR loader, mics. */
class CabPanel : public juce::Component
{
public:
    explicit CabPanel (APVTS& state);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Rectangle<float> cabArea() const;

    LedSwitch cabOn;
    ChoiceBox cabModel;
    LabeledKnob micPosition, micDistance;
    std::unique_ptr<juce::ParameterAttachment> modelWatch;
    int currentCab = 0;
};

/** Signal chain overview. Blocks that exist are live (click toggles bypass);
    the pedalboard slots are clearly marked as arriving in Phase 4. */
class SignalChainStrip : public juce::Component, public juce::SettableTooltipClient, private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit SignalChainStrip (APVTS& state);
    ~SignalChainStrip() override;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void parameterChanged (const juce::String&, float) override { juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<SignalChainStrip> (this)] { if (sp) sp->repaint(); }); }

    struct Block { juce::String name; juce::String paramId; bool live; };
    juce::Array<Block> blocks;
    juce::Array<juce::Rectangle<float>> blockBounds;
    APVTS& state;
};

/** Splash shown when the editor first opens; click or wait to dismiss. */
class SplashOverlay : public juce::Component, private juce::Timer
{
public:
    SplashOverlay();
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override { dismiss(); }
    std::function<void()> onDismissed;

private:
    void timerCallback() override;
    void dismiss();
    juce::Image icon;
    float alpha = 1.0f;
    bool fading = false;
    int ticks = 0;
};

/** About screen with version, credits and legal notices. */
class AboutOverlay : public juce::Component
{
public:
    AboutOverlay();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent& e) override;

    juce::TextButton closeButton { "CLOSE" };

private:
    juce::Rectangle<int> card() const;
    juce::Image icon;
};
