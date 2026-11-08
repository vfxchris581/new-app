#pragma once

#include <JuceHeader.h>
#include "vf/Parameters.h"
#include "vf/dsp/AmpVoice.h"
#include "vf/dsp/Cabinet.h"
#include "vf/dsp/NoiseGate.h"
#include "vf/dsp/ToneShaper.h"
#include "vf/dsp/Tuner.h"

#if VF_WITH_NAM
 #include "vf/nam/NamEngine.h"
#endif

/** Audio processor shared by the Standalone app, VST3 and AU builds.

    Signal flow:
      input -> tuner tap -> noise gate -> AMP SOURCE (4x oversampled modeled amp,
      or a NAM capture with drive + post-EQ) -> CAB SECTION (built-in cab with
      mic type, IR A, or IR A+B blend / stereo) -> low/high cut -> room -> output

    Real-time rules followed in processBlock():
      - no allocation (all buffers sized in prepareToPlay)
      - no locks, no file I/O (NAM/IR files load on background threads)
      - parameters read through cached atomics
      - meters/tuner hand data to the UI through atomics and a lock-free FIFO
*/
class ViridianForgeProcessor : public juce::AudioProcessor
{
public:
    ViridianForgeProcessor();
    ~ViridianForgeProcessor() override;

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
    double getTailLengthSeconds() const override { return 0.05; }

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

    //==============================================================================
    // Files: NAM captures and impulse responses (call from the message thread)
    enum class LoadState { Empty, Loading, Ready, Failed };

    struct FileSlotStatus
    {
        LoadState state = LoadState::Empty;
        juce::String path, description, error;
    };

    void loadNamModel (const juce::File& file);
    void unloadNamModel();
    FileSlotStatus getNamStatus() const;

    void loadImpulseResponse (int slot, const juce::File& file); // slot 0 = A, 1 = B
    void clearImpulseResponse (int slot);
    FileSlotStatus getIrStatus (int slot) const;

    /** Library folders (created on demand) in the user's application-data directory. */
    static juce::File getLibraryFolder (const juce::String& kind); // "NAM" or "IRs"
    static juce::File importIntoLibrary (const juce::File& source, const juce::String& kind);

    /** Message thread, from the editor's timer: frees retired NAM models. */
    void housekeeping();

    static bool isNamAvailable() noexcept
    {
       #if VF_WITH_NAM
        return true;
       #else
        return false;
       #endif
    }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    struct Settings;
    void readSettings (Settings&) const noexcept;
    void processChunk (const float* in, float* outL, float* outR, int numSamples, const Settings&) noexcept;
    void restoreFilesFromState();

    juce::AudioProcessorValueTreeState apvts;

    struct Raw
    {
        std::atomic<float>* inputGain, *gateOn, *gateThresh, *ampModel, *channel, *boost, *gain, *bass, *middle, *treble,
            *presence, *resonance, *master, *bright, *deep, *tubeSwap, *cabOn, *cabModel, *micPos, *micDist, *outLevel,
            *ampSource, *geqOn, *geq[5], *cabSource, *micType, *irBlend, *irPhaseB, *cabLowCut, *cabHighCut, *room, *cabStereo;
    } raw {};

    // Amp
    static constexpr int oversamplingFactorLog2 = 2; // 4x for the nonlinear amp path
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    vf::dsp::AmpVoice amp;
   #if VF_WITH_NAM
    vf::nam::Engine nam;
   #endif
    vf::dsp::ToneShaper namTone;
    vf::dsp::Biquad namBoostLow, namBoostHigh;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> namDrive { 1.0f }, namLevel { 1.0f };

    // Front end
    vf::dsp::NoiseGate gate;
    vf::dsp::TunerAnalyser tuner;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inputGain { 1.0f }, outputGain { 1.0f };

    // Cab section
    vf::dsp::CabinetSim cabinet;
    juce::dsp::Convolution irA, irB;
    vf::dsp::Biquad lowCutL, lowCutR, highCutL, highCutR;
    vf::dsp::RoomAmbience roomL, roomR;
    juce::SmoothedValue<float> cabMix { 1.0f }, irBlend { 0.5f }, roomAmount { 0.0f };
    float lastLowCut = -1.0f, lastHighCut = -1.0f;

    // Click-free source switching (fade out -> switch -> fade in)
    struct SourceSwitch
    {
        int active = 0, target = 0;
        float gain = 1.0f, step = 0.01f;
        void reset (double rate, int value) { active = target = value; gain = 1.0f; step = static_cast<float> (1.0 / (0.005 * rate)); }
        float next() noexcept
        {
            if (target != active) { gain -= step; if (gain <= 0.0f) { gain = 0.0f; active = target; } }
            else if (gain < 1.0f) gain = std::min (1.0f, gain + step);
            return gain;
        }
    } ampSwitch, cabSwitch;

    juce::AudioBuffer<float> work; // channels: 0 mono, 1 IR A, 2 IR B, 3 left, 4 right
    int maxChunk = 512;
    double currentRate = 48000.0;

    // File state (message thread), shared with the loader thread through the lock
    mutable juce::CriticalSection fileLock;
    FileSlotStatus namStatus, irStatus[2];
    std::atomic<bool> irReady[2] { false, false };
    std::atomic<bool> namReady { false };
    juce::ThreadPool loader { 1 };

    juce::AudioProcessLoadMeasurer loadMeasurer;
    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f };
    std::atomic<bool> gateOpen { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ViridianForgeProcessor)
};
