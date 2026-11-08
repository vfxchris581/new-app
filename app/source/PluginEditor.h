#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ui/Panels.h"
#include "ui/ViridianLookAndFeel.h"

class ViridianForgeEditor : public juce::AudioProcessorEditor,
                            private juce::Timer,
                            private juce::Value::Listener
{
public:
    explicit ViridianForgeEditor (ViridianForgeProcessor&);
    ~ViridianForgeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int baseWidth = 1280, baseHeight = 800;

private:
    void timerCallback() override;
    void valueChanged (juce::Value&) override;
    void applyTheme();
    void updateLiveInputButton();
    bool isStandalone() const;

    ViridianForgeProcessor& processor;
    ViridianLookAndFeel laf;
    juce::TooltipWindow tooltips { this, 600 };

    TopBar topBar;
    IOPanel ioPanel;
    AmpHeadPanel ampHead;
    CabPanel cabPanel;
    SignalChainStrip chain;
    AboutOverlay about;
    std::unique_ptr<SplashOverlay> splash;

    juce::Value themeValue;
    int tunerDivider = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ViridianForgeEditor)
};
