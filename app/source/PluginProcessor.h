#pragma once

#include <JuceHeader.h>
#include "vf/Parameters.h"
#include "vf/dsp/AmpVoice.h"
#include "vf/dsp/Cabinet.h"
#include "vf/dsp/NoiseGate.h"
#include "vf/dsp/Tuner.h"

/** Audio processor shared by the Standalone app, VST3 and AU builds.

    Real-time rules followed in processBlock():
      - no allocation (all buffers sized in prepareToPlay)
      - no locks, no file I/O
      - parameters read through cached atomics
      - meters/tuner hand data to the UI through atomics and a lock-free FIFO
*/
class ViridianForgeProcessor : public juce::AudioProcessor
{
public:
    ViridianForgeProcessor();
    ~ViridianForgeProcessor() override = default;

    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Phase 5 replaces this with the full preset system.
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Init"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // UI access
    juce::AudioProcessorValueTreeState& getState() noexcept { return apvts; }
    float popInputPeak() noexcept  { return inputPeak.exchange (0.0f); }
    float popOutputPeak() noexcept { return outputPeak.exchange (0.0f); }
    double getCpuLoad() const noexcept { return loadMeasurer.getLoadAsProportion(); }
    vf::dsp::TunerReading analyseTuner() { return tuner.analyse(); } // message thread only
    bool isGateOpen() const noexcept { return gateOpen.load(); }

    /** Non-automatable UI setting stored with the session. */
    juce::Value getThemeValue() { return apvts.state.getPropertyAsValue ("theme", nullptr); }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    vf::dsp::AmpSettings readAmpSettings() const noexcept;
    void processMonoChunk (float* data, int numSamples) noexcept;

    juce::AudioProcessorValueTreeState apvts;

    struct RawParams
    {
        std::atomic<float>* inputGain = nullptr; std::atomic<float>* gateOn = nullptr; std::atomic<float>* gateThresh = nullptr;
        std::atomic<float>* ampModel = nullptr;  std::atomic<float>* channel = nullptr; std::atomic<float>* boost = nullptr;
        std::atomic<float>* gain = nullptr;      std::atomic<float>* bass = nullptr;    std::atomic<float>* middle = nullptr;
        std::atomic<float>* treble = nullptr;    std::atomic<float>* presence = nullptr; std::atomic<float>* resonance = nullptr;
        std::atomic<float>* master = nullptr;    std::atomic<float>* bright = nullptr;  std::atomic<float>* deep = nullptr;
        std::atomic<float>* tubeSwap = nullptr;  std::atomic<float>* cabOn = nullptr;   std::atomic<float>* cabModel = nullptr;
        std::atomic<float>* micPos = nullptr;    std::atomic<float>* micDist = nullptr; std::atomic<float>* outLevel = nullptr;
    } raw;

    static constexpr int oversamplingFactorLog2 = 2; // 4x for the nonlinear amp path
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    vf::dsp::AmpVoice amp;
    vf::dsp::NoiseGate gate;
    vf::dsp::CabinetSim cabinet;
    vf::dsp::TunerAnalyser tuner;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inputGain { 1.0f }, outputGain { 1.0f };

    juce::AudioBuffer<float> monoBuffer;
    int maxChunk = 512;

    juce::AudioProcessLoadMeasurer loadMeasurer;
    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f };
    std::atomic<bool> gateOpen { true };
    juce::SmoothedValue<float> cabMix { 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ViridianForgeProcessor)
};
