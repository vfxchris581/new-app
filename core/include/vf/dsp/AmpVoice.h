// Viridian Forge — DSP core (JUCE-independent)
//
// AmpVoice: evaluates any amp model from the registry (vf/models/AmpModels.h).
//
//   guitar (volts) → [boost] → triode stages with the model's gain pot,
//   bright cap and tone stack at their circuit positions → phase inverter
//   → power stage (tube type, NFB, sag, presence/resonance) → deep → out
//
// Runs at an oversampled rate (the JUCE layer uses 4x). The cabinet is a
// separate linear stage (CabinetSim) run at the base rate.
//
// Real-time behaviour: setSettings() and process() never allocate. Switching
// model or channel fades out (~4 ms), reconfigures, and fades back in, so it
// is click-free. Every model/channel is level-matched by a calibration pass
// computed once, off the audio thread, the first time prepare() runs.
#pragma once

#include "../models/AmpModels.h"
#include "Biquad.h"
#include "Smoother.h"
#include "ToneStack.h"
#include "Triode.h"
#include "TubeStage.h"
#include <array>
#include <cmath>
#include <initializer_list>
#include <vector>

namespace vf::dsp
{

struct AmpSettings
{
    int model = static_cast<int> (models::AmpModelId::Plate59Lead);
    int channel = 0;          // clamped to the model's channel count
    int tubeSwap = -1;        // -1 = model's stock power tubes, else PowerTube index
    float gain = 5.0f;        // 0..10
    float bass = 5.0f, middle = 5.0f, treble = 5.0f;
    float presence = 5.0f, resonance = 5.0f;
    float master = 5.0f;      // 0..10
    bool bright = false, deep = false, boost = false;
};

class AmpVoice
{
public:
    static constexpr float inputVoltsPerUnit = 2.0f; // digital full scale ≈ 2 V peak from the guitar

    void prepare (double sampleRate, int maxBlockSize)
    {
        prepareInternal (sampleRate, maxBlockSize, true);
    }

    void reset()
    {
        for (auto& s : stages) s.reset();
        toneStack.reset();
        power.reset();
        for (auto* b : { &boostLow, &boostHigh, &brightShelf, &deepShelf, &piCoupling })
            b->reset();
    }

    /** Audio thread, once per block. Cheap; never allocates. */
    void setSettings (const AmpSettings& s) noexcept
    {
        pending = s;
        const int ch = clampChannel (s.model, s.channel);
        if (s.model != active.model || ch != activeChannel || s.tubeSwap != active.tubeSwap)
        {
            switchPending = true; // fade out → reconfigure → fade in, handled in process()
            return;
        }
        applyControls (s, false);
    }

    const AmpSettings& getSettings() const noexcept { return active; }
    int getActiveChannel() const noexcept { return activeChannel; }

    void process (float* samples, int numSamples) noexcept
    {
        const auto& ch = getChannel();
        const int n = ch.numStages;

        for (int i = 0; i < numSamples; ++i)
        {
            // Click-free model/channel switching
            if (switchPending)
            {
                fade -= fadeStep;
                if (fade <= 0.0f)
                {
                    fade = 0.0f;
                    switchPending = false;
                    configure (pending);
                    samples[i] = 0.0f;
                    if (i + 1 < numSamples)
                        process (samples + i + 1, numSamples - i - 1); // continue with the new model, fading in
                    return;
                }
            }
            else if (fade < 1.0f)
            {
                fade = std::fmin (1.0f, fade + fadeStep);
            }

            float v = samples[i] * inputVoltsPerUnit;

            if (active.boost)
                v = boostHigh.process (boostLow.process (v)) * 3.5f + v * 0.3f; // tight, mid-forward pre-boost

            if (n == 0)
                v *= 150.0f; // solid-state preamp gain (op-amp stage)

            const float pot = potGain.next();
            for (int s = 0; s <= n; ++s)
            {
                if (s == ch.toneStackBeforeStage) v = toneStack.process (v) * stackMakeup;
                if (s == ch.gainPotBeforeStage)
                {
                    v *= pot;
                    if (active.bright) v = brightShelf.process (v);
                }
                if (s < n) v = stages[static_cast<size_t> (s)].process (v);
            }

            // Phase inverter coupling + power section
            v = piCoupling.process (v) * (1.0f / 45.0f);
            v = power.process (v, masterGain.next());

            if (active.deep)
                v = deepShelf.process (v);

            samples[i] = v * levelTrim * fade;
        }
    }

    float getSagAmount() const noexcept { return power.getSagAmount(); }
    float getBiasShift (int stage) const noexcept { return stages[static_cast<size_t> (stage)].getBiasShift(); }

    /** Level-matching trim for a model/channel (1.0 before calibration). */
    static float calibrationTrim (int model, int channel)
    {
        const auto& t = calibrationTable();
        const size_t idx = static_cast<size_t> (model * 3 + channel);
        return idx < t.size() ? t[idx] : 1.0f;
    }

private:
    static int clampChannel (int model, int channel)
    {
        const int count = models::getAmpModel (model).numChannels;
        return channel < 0 ? 0 : (channel >= count ? count - 1 : channel);
    }

    const models::ChannelSpec& getChannel() const noexcept
    {
        return models::getAmpModel (active.model).channels[static_cast<size_t> (activeChannel)];
    }

    void prepareInternal (double sampleRate, int, bool useCalibration)
    {
        fs = sampleRate;
        calibrated = useCalibration;
        if (useCalibration)
            (void) calibrationTable(); // build once (off the audio thread)

        boostLow.setCoeffs (BiquadCoeffs::highPass (fs, 720.0, 0.5));
        boostHigh.setCoeffs (BiquadCoeffs::lowPass (fs, 6000.0, 0.7));
        piCoupling.setCoeffs (BiquadCoeffs::highPass (fs, 20.0, 0.7));
        deepShelf.setCoeffs (BiquadCoeffs::lowShelf (fs, 90.0, 0.8, 5.0));
        power.prepare (fs);
        potGain.reset (fs, 0.03, 0.1f);
        masterGain.reset (fs, 0.03, 0.5f);
        fadeStep = static_cast<float> (1.0 / (0.004 * fs));
        fade = 1.0f;
        switchPending = false;
        configure (pending);
        fade = 1.0f;
    }

    /** Reconfigure for a model/channel/tube. No allocation: only filter coefficients and state. */
    void configure (const AmpSettings& s) noexcept
    {
        active = s;
        activeChannel = clampChannel (s.model, s.channel);
        const auto& m = models::getAmpModel (s.model);
        const auto& ch = m.channels[static_cast<size_t> (activeChannel)];

        for (int i = 0; i < ch.numStages; ++i)
            stages[static_cast<size_t> (i)].prepare (fs, ch.stages[static_cast<size_t> (i)]);

        toneStack.prepare (fs);
        toneStack.setComponents (m.stack);
        stackMakeup = dbToGain (ch.stackMakeupDb);
        brightShelf.setCoeffs (BiquadCoeffs::highShelf (fs, ch.brightHz, 0.7, ch.brightDb));

        power.reset();
        const auto tube = s.tubeSwap >= 0 && s.tubeSwap < static_cast<int> (PowerTube::count)
                              ? static_cast<PowerTube> (s.tubeSwap) : m.power.tube;
        power.setTube (tube);
        power.setCharacter (m.power.nfb, m.power.sagMul, m.power.headroom, m.power.solidState);

        levelTrim = calibrated ? calibrationTrim (s.model, activeChannel) : 1.0f;
        applyControls (s, true);
        for (auto* b : { &boostLow, &boostHigh, &brightShelf, &deepShelf, &piCoupling })
            b->reset();
    }

    void applyControls (const AmpSettings& s, bool force) noexcept
    {
        const auto& ch = models::getAmpModel (active.model).channels[static_cast<size_t> (activeChannel)];

        if (force || controlChanged (s.bass, active.bass) || controlChanged (s.middle, active.middle) || controlChanged (s.treble, active.treble))
            toneStack.setControls (s.bass / 10.0, s.middle / 10.0, s.treble / 10.0);

        if (force || controlChanged (s.presence, active.presence) || controlChanged (s.resonance, active.resonance))
            power.setPresenceResonance (s.presence / 10.0f, s.resonance / 10.0f);

        if (force || controlChanged (s.gain, active.gain))
        {
            // Audio-taper pot: most of the useful range in the upper half, like the real control.
            const float g = s.gain / 10.0f;
            const float taper = (std::pow (10.0f, 2.0f * g) - 1.0f) / 99.0f;
            const float minGain = dbToGain (ch.gainPotMinDb);
            const float value = minGain + (1.0f - minGain) * taper;
            if (force) potGain.snap (value); else potGain.setTarget (value);
        }

        const float master = dbToGain (-24.0f + 36.0f * (s.master / 10.0f));
        if (force) masterGain.snap (master); else masterGain.setTarget (master);

        const int keepModel = active.model, keepTube = active.tubeSwap, keepChannel = active.channel;
        active = s;
        active.model = keepModel; active.tubeSwap = keepTube; active.channel = keepChannel;
    }

    /** Level-match every model/channel at the reference settings (all knobs at 5). */
    static const std::vector<float>& calibrationTable()
    {
        static const std::vector<float> table = []
        {
            constexpr double rate = 96000.0;
            std::vector<float> t (static_cast<size_t> (models::numAmpModels() * 3), 1.0f);
            // Reference DI: decaying note with harmonics, peaking around -12 dBFS
            std::vector<float> di (static_cast<size_t> (rate * 0.6));
            for (size_t i = 0; i < di.size(); ++i)
            {
                const double tt = static_cast<double> (i) / rate;
                const double env = std::exp (-tt * 3.0);
                double v = 0.0;
                for (int h = 1; h <= 6; ++h) v += std::sin (2.0 * kPi * 110.0 * h * tt) / h;
                di[i] = static_cast<float> (0.18 * env * v);
            }
            constexpr double targetRms = 0.12;
            for (int m = 0; m < models::numAmpModels(); ++m)
                for (int c = 0; c < models::getAmpModel (m).numChannels; ++c)
                {
                    AmpVoice v;
                    AmpSettings s; s.model = m; s.channel = c;
                    v.pending = s;
                    v.prepareInternal (rate, 512, false);
                    std::vector<float> buf = di;
                    v.process (buf.data(), static_cast<int> (buf.size()));
                    double sum = 0.0;
                    const size_t start = buf.size() / 4;
                    for (size_t i = start; i < buf.size(); ++i) sum += static_cast<double> (buf[i]) * buf[i];
                    const double rms = std::sqrt (sum / static_cast<double> (buf.size() - start));
                    const double trim = rms > 1.0e-6 ? targetRms / rms : 1.0;
                    t[static_cast<size_t> (m * 3 + c)] = static_cast<float> (std::fmin (8.0, std::fmax (0.125, trim)));
                }
            return t;
        }();
        return table;
    }

    double fs = 48000.0;
    AmpSettings active, pending;
    int activeChannel = 0;
    bool switchPending = false, calibrated = false;
    float fade = 1.0f, fadeStep = 0.001f, levelTrim = 1.0f, stackMakeup = 1.0f;

    std::array<TriodeStageModel, 4> stages;
    ToneStack toneStack;
    PowerStage power;
    Biquad boostLow, boostHigh, brightShelf, deepShelf, piCoupling;
    LinearSmoother potGain, masterGain;
};

} // namespace vf::dsp
