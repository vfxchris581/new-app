// Viridian Forge — DSP core (JUCE-independent)
//
// Static-curve tube stage models used by the Phase 1 reference amp voice.
// These are deliberately simple, *original* waveshaping models with
// frequency shaping around them; Phase 2 replaces/extends them with
// stateful triode (Koren-style) and push-pull power-stage models.
#pragma once

#include "Biquad.h"
#include <cmath>

namespace vf::dsp
{

/** Asymmetric soft clipper: different curvature on each half-wave
    produces even harmonics, and a bias offset shifts where clipping starts. */
inline float asymmetricClip (float x, float posHardness, float negHardness) noexcept
{
    return x >= 0.0f ? std::tanh (x * posHardness) / posHardness
                     : std::tanh (x * negHardness) / negHardness;
}

/** One triode gain stage: cathode-bypass emphasis → bias + asymmetric clip
    → DC block → Miller-capacitance roll-off. */
class TriodeStage
{
public:
    void prepare (double sampleRate, double millerCutoffHz = 9000.0, double cathodeShelfHz = 650.0, double cathodeShelfDb = 4.0)
    {
        fs = sampleRate;
        cathode.setCoeffs (BiquadCoeffs::lowShelf (fs, cathodeShelfHz, 0.6, -cathodeShelfDb)); // bypass cap: lows get less gain
        miller.setCoeffs (BiquadCoeffs::lowPass (fs, millerCutoffHz, 0.6));
        dc.prepare (fs);
        reset();
    }

    void reset() { cathode.reset(); miller.reset(); dc.reset(); }

    void setBias (float b) noexcept { bias = b; biasOffset = asymmetricClip (bias, 1.0f, 1.6f); }

    float process (float x, float drive) noexcept
    {
        const float pre = cathode.process (x) * drive;
        const float y = asymmetricClip (pre + bias, 1.0f, 1.6f) - biasOffset;
        return miller.process (dc.process (y));
    }

private:
    double fs = 48000.0;
    Biquad cathode, miller;
    DcBlocker dc;
    float bias = 0.12f, biasOffset = asymmetricClip (0.12f, 1.0f, 1.6f);
};

/** Power tube families. Values are original voicing choices that capture the
    commonly described character of each tube type, not measured data. */
enum class PowerTube { EL34, SixL6, EL84, Kt6550, KT88, SixV6, T5881, count };

struct PowerTubeProfile
{
    const char* name;
    float hardness;      // how abruptly the stage clips (higher = harder)
    float asymmetry;     // >1 → more even-order content
    float sagDepth;      // supply sag amount (rectifier / supply impedance)
    float sagReleaseMs;  // supply recovery time
    float voiceFreq;     // frequency of the tube's characteristic emphasis
    float voiceDb;       // emphasis amount
};

inline PowerTubeProfile getPowerTubeProfile (PowerTube t) noexcept
{
    switch (t)
    {
        case PowerTube::EL34:   return { "EL34",   1.6f, 1.25f, 0.18f, 120.0f, 1800.0f,  2.5f };
        case PowerTube::SixL6:  return { "6L6",    1.2f, 1.10f, 0.12f, 140.0f,  120.0f,  1.5f };
        case PowerTube::EL84:   return { "EL84",   1.9f, 1.35f, 0.25f, 100.0f, 2400.0f,  3.0f };
        case PowerTube::Kt6550: return { "6550",   1.0f, 1.05f, 0.08f, 160.0f,   90.0f,  2.0f };
        case PowerTube::KT88:   return { "KT88",   0.9f, 1.05f, 0.07f, 170.0f,   80.0f,  2.5f };
        case PowerTube::SixV6:  return { "6V6",    1.7f, 1.30f, 0.28f, 110.0f,  900.0f,  1.5f };
        case PowerTube::T5881:  return { "5881",   1.3f, 1.15f, 0.15f, 130.0f,  150.0f,  1.0f };
        case PowerTube::count:  break;
    }
    return { "EL34", 1.6f, 1.25f, 0.18f, 120.0f, 1800.0f, 2.5f };
}

/** Push-pull power stage with supply sag and presence/resonance shaping.
    Presence and resonance are implemented as the frequency regions where the
    negative-feedback loop is reduced, i.e. the power stage gets more gain there. */
class PowerStage
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        dc.prepare (fs);
        reset();
        setTube (PowerTube::EL34);
        setPresenceResonance (0.5f, 0.5f);
    }

    void reset() { presence.reset(); resonance.reset(); voice.reset(); dc.reset(); sagEnv = 0.0f; }

    void setTube (PowerTube t)
    {
        profile = getPowerTubeProfile (t);
        voice.setCoeffs (BiquadCoeffs::peak (fs, profile.voiceFreq, 0.8, profile.voiceDb));
        sagRelease = static_cast<float> (std::exp (-1.0 / (0.001 * profile.sagReleaseMs * fs)));
        sagAttack = static_cast<float> (std::exp (-1.0 / (0.004 * fs)));
    }

    /** 0..1 knob positions. */
    void setPresenceResonance (float presence01, float resonance01)
    {
        presence.setCoeffs (BiquadCoeffs::highShelf (fs, 3500.0, 0.7, -3.0 + 9.0 * presence01));
        resonance.setCoeffs (BiquadCoeffs::lowShelf (fs, 95.0, 0.9, -2.0 + 8.0 * resonance01));
    }

    float process (float x, float drive) noexcept
    {
        float v = voice.process (resonance.process (presence.process (x)));

        // Supply sag: heavy output current pulls the supply down, reducing headroom.
        const float headroom = 1.0f - profile.sagDepth * sagEnv;
        v *= drive;
        const float h = profile.hardness / headroom;
        const float y = asymmetricClip (v, h, h * profile.asymmetry) * headroom;

        const float level = std::fabs (y);
        const float coeff = level > sagEnv ? sagAttack : sagRelease;
        sagEnv = flushDenormal (coeff * sagEnv + (1.0f - coeff) * level);
        return dc.process (y);
    }

    float getSagAmount() const noexcept { return sagEnv * profile.sagDepth; }

private:
    double fs = 48000.0;
    PowerTubeProfile profile = getPowerTubeProfile (PowerTube::EL34);
    Biquad presence, resonance, voice;
    DcBlocker dc;
    float sagEnv = 0.0f, sagAttack = 0.99f, sagRelease = 0.999f;
};

} // namespace vf::dsp
