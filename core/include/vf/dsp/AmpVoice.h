// Viridian Forge — DSP core (JUCE-independent)
//
// AmpVoice: the Phase 1 *reference* amplifier signal path.
//
//   in → [boost] → bright cap → V1 triode → (V2) → (V3) → FMV tone stack
//      → master → power stage (tube type, sag, presence, resonance) → deep
//      → [basic cab filter] → out
//
// It is real DSP — every control changes the sound — but it is one generic
// amp voice, not the 100-model catalog. Phase 2 adds per-model circuits and
// Phase 3 adds IR cabinets and NAM. Run it at an oversampled rate (the JUCE
// layer wraps it in 4x oversampling) to keep aliasing down.
#pragma once

#include "Biquad.h"
#include "NoiseGate.h"
#include "Smoother.h"
#include "ToneStack.h"
#include "TubeStage.h"
#include <array>
#include <initializer_list>

namespace vf::dsp
{

enum class AmpChannel { Clean = 0, Crunch, Lead, count };
enum class ToneStackVoicing { Tweed = 0, Blackface, British, Modern, count };

struct AmpSettings
{
    float gain = 5.0f;       // 0..10
    float bass = 5.0f;       // 0..10
    float middle = 5.0f;     // 0..10
    float treble = 5.0f;     // 0..10
    float presence = 5.0f;   // 0..10
    float resonance = 5.0f;  // 0..10
    float master = 5.0f;     // 0..10
    bool bright = false;
    bool deep = false;
    bool boost = false;
    AmpChannel channel = AmpChannel::Crunch;
    PowerTube tube = PowerTube::EL34;
    ToneStackVoicing toneStack = ToneStackVoicing::British;
    bool cabEnabled = true;
};

class AmpVoice
{
public:
    void prepare (double sampleRate, int /*maxBlockSize*/)
    {
        fs = sampleRate;
        for (auto& t : triodes) t.prepare (fs);
        triodes[1].setBias (0.18f);
        triodes[2].setBias (0.22f);
        toneStack.prepare (fs);
        power.prepare (fs);

        boostLow.setCoeffs (BiquadCoeffs::highPass (fs, 720.0, 0.5));   // tight, mid-forward pre-boost
        boostHigh.setCoeffs (BiquadCoeffs::lowPass (fs, 6000.0, 0.7));
        for (auto& c : interstage) c.setCoeffs (BiquadCoeffs::highPass (fs, 25.0, 0.7)); // coupling caps

        cabHp.setCoeffs (BiquadCoeffs::highPass (fs, 75.0, 0.8));
        cabLowRes.setCoeffs (BiquadCoeffs::peak (fs, 110.0, 1.2, 3.0));
        cabPresence.setCoeffs (BiquadCoeffs::peak (fs, 2600.0, 1.0, 3.5));
        cabLp1.setCoeffs (BiquadCoeffs::lowPass (fs, 5200.0, 0.54));
        cabLp2.setCoeffs (BiquadCoeffs::lowPass (fs, 5200.0, 1.31));

        preGain.reset (fs, 0.03, 1.0f);
        masterGain.reset (fs, 0.03, 0.5f);
        outTrim.reset (fs, 0.03, 1.0f);
        reset();
        applySettings (settings, true);
    }

    void reset()
    {
        for (auto& t : triodes) t.reset();
        toneStack.reset();
        power.reset();
        for (auto* b : { &boostLow, &boostHigh, &brightShelf, &deepShelf, &interstage[0], &interstage[1], &cabHp, &cabLowRes, &cabPresence, &cabLp1, &cabLp2 })
            b->reset();
    }

    /** Call from the audio thread before each block (cheap; no allocation). */
    void setSettings (const AmpSettings& s) { applySettings (s, false); }
    const AmpSettings& getSettings() const noexcept { return settings; }

    void process (float* samples, int numSamples) noexcept
    {
        const int stages = numStages();
        for (int i = 0; i < numSamples; ++i)
        {
            float x = samples[i];

            if (settings.boost)
                x = boostHigh.process (boostLow.process (x)) * 3.2f + x * 0.35f;

            if (settings.bright)
                x = brightShelf.process (x);

            const float drive = preGain.next();
            x = triodes[0].process (x, drive);
            for (int s = 1; s < stages; ++s)
                x = triodes[static_cast<size_t> (s)].process (interstage[static_cast<size_t> (s - 1)].process (x) * 0.9f, drive * 0.55f + 1.2f);

            x = toneStack.process (x) * toneStackMakeup;

            x = power.process (x, masterGain.next());

            if (settings.deep)
                x = deepShelf.process (x);

            if (settings.cabEnabled)
                x = cabLp2.process (cabLp1.process (cabPresence.process (cabLowRes.process (cabHp.process (x)))));

            samples[i] = x * outTrim.next();
        }
    }

    float getSagAmount() const noexcept { return power.getSagAmount(); }

private:
    int numStages() const noexcept
    {
        switch (settings.channel)
        {
            case AmpChannel::Clean:  return 1;
            case AmpChannel::Crunch: return 2;
            case AmpChannel::Lead:
            case AmpChannel::count:  break;
        }
        return 3;
    }

    static ToneStackComponents componentsFor (ToneStackVoicing v)
    {
        switch (v)
        {
            case ToneStackVoicing::Tweed:     return ToneStackComponents::tweedBassman();
            case ToneStackVoicing::Blackface: return ToneStackComponents::blackface();
            case ToneStackVoicing::Modern:    return ToneStackComponents::modernHiGain();
            case ToneStackVoicing::British:
            case ToneStackVoicing::count:     break;
        }
        return ToneStackComponents::british();
    }

    void applySettings (const AmpSettings& s, bool force)
    {
        const bool toneChanged = force || controlChanged (s.bass, settings.bass) || controlChanged (s.middle, settings.middle)
                              || controlChanged (s.treble, settings.treble) || s.toneStack != settings.toneStack;
        const bool powerChanged = force || controlChanged (s.presence, settings.presence) || controlChanged (s.resonance, settings.resonance);
        const bool tubeChanged = force || s.tube != settings.tube;
        const bool gainChanged = force || controlChanged (s.gain, settings.gain) || s.channel != settings.channel;

        if (s.channel != settings.channel)
            for (auto& t : triodes) t.reset();

        settings = s;

        if (toneChanged)
        {
            toneStack.setComponents (componentsFor (s.toneStack));
            toneStack.setControls (s.bass / 10.0, s.middle / 10.0, s.treble / 10.0);
        }
        if (powerChanged)
            power.setPresenceResonance (s.presence / 10.0f, s.resonance / 10.0f);
        if (tubeChanged)
            power.setTube (s.tube);

        if (gainChanged)
        {
            // Channel sets the gain range; the knob sweeps it on a log-ish curve.
            const float g = s.gain / 10.0f;
            float minDb = 0.0f, maxDb = 0.0f;
            switch (s.channel)
            {
                case AmpChannel::Clean:  minDb = -12.0f; maxDb = 14.0f; break;
                case AmpChannel::Crunch: minDb = -4.0f;  maxDb = 26.0f; break;
                case AmpChannel::Lead:
                case AmpChannel::count:  minDb = 4.0f;   maxDb = 36.0f; break;
            }
            preGain.setTarget (dbToGain (minDb + (maxDb - minDb) * g));

            // Bright cap: strong at low gain settings, fades as the volume pot opens (like the real circuit).
            brightShelf.setCoeffs (BiquadCoeffs::highShelf (fs, 2200.0, 0.7, 7.0 * (1.0f - 0.8f * g)));

            // Rough loudness compensation so channel/gain changes don't jump wildly in level.
            outTrim.setTarget (s.channel == AmpChannel::Clean ? 1.6f : 0.9f);
        }

        if (force || s.deep != settings.deep)
            deepShelf.setCoeffs (BiquadCoeffs::lowShelf (fs, 90.0, 0.8, 5.0));

        // Master drives the power stage: low = clean headroom, high = power-tube breakup.
        masterGain.setTarget (dbToGain (-20.0f + 34.0f * (s.master / 10.0f)));
    }

    double fs = 48000.0;
    AmpSettings settings;
    std::array<TriodeStage, 3> triodes;
    ToneStack toneStack;
    PowerStage power;
    Biquad boostLow, boostHigh, brightShelf, deepShelf;
    std::array<Biquad, 2> interstage;
    Biquad cabHp, cabLowRes, cabPresence, cabLp1, cabLp2;
    LinearSmoother preGain, masterGain, outTrim;
    static constexpr float toneStackMakeup = 3.0f; // compensates the passive stack's insertion loss
};

} // namespace vf::dsp
