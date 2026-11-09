// Viridian Forge — distortion, overdrive, fuzz and boost pedals.
//
// One engine, voiced per pedal by the circuit features that define its sound:
// input pre-emphasis (high-pass), op-amp mid hump, clipping element (soft,
// hard, diode pair, asymmetric, germanium/silicon fuzz), number of clipping
// stages, clean blend, octave (full-wave rectifier), fuzz bias, and the tone
// control topology. Runs 2x oversampled. Names are original; "inspiredBy"
// describes the classic circuit that guided the voicing.
#pragma once

#include "Effect.h"
#include "Primitives.h"

namespace vf::fx
{

enum class Clip { Soft, Hard, Diode, Asym, FuzzGe, FuzzSi, Rectify };
enum class ToneStyle { LowPass, Tilt, MuffScoop, BigBand, Treble };

struct DriveVoicing
{
    const char* name;
    const char* inspiredBy;
    float preHpHz;          // input coupling / bass cut before clipping
    float humpHz, humpDb;   // op-amp feedback mid hump (0 dB = none)
    Clip clip;
    int stages;             // cascaded clipping stages
    float minGainDb, maxGainDb;
    float asymmetry;        // for Asym / fuzz
    float cleanBlend;       // 0..1 of dry signal mixed around the clipper (Klon-style)
    ToneStyle tone;
    float toneLoHz, toneHiHz; // tone sweep range
    float postLpHz;         // fixed post filter (rounding)
    float outTrimDb;
    bool hasBias;           // extra "Bias" (fuzz starve) control
};

class DriveFx : public Effect
{
public:
    explicit DriveFx (const DriveVoicing& voicing) : vc (voicing)
    {
        pDrive = addParam (knob ("Drive", 5.0f));
        pTone = addParam (knob ("Tone", 5.0f));
        pLevel = addParam (levelDb ("Level", 0.0f));
        if (vc.hasBias) pBias = addParam (knob ("Bias", 10.0f));
    }

    void prepare (double sampleRate, int maxBlockSize) override
    {
        fs = sampleRate; maxBlock = maxBlockSize;
        const double os = fs * 2.0;
        for (int c = 0; c < 2; ++c)
        {
            ch[c].preHp.setCoeffs (vf::dsp::BiquadCoeffs::highPass (os, vc.preHpHz, 0.7));
            ch[c].hump.setCoeffs (vf::dsp::BiquadCoeffs::peak (os, std::fmax (100.0f, vc.humpHz), 0.7, vc.humpDb));
            ch[c].postLp.setCoeffs (vf::dsp::BiquadCoeffs::lowPass (os, std::fmin (vc.postLpHz, static_cast<float> (os * 0.45)), 0.6));
            ch[c].dcBlock.prepare (os, 15.0);
        }
        gain.reset (os, 0.03, 1.0f);
        out.reset (os, 0.03, 1.0f);
        updateTone();
        reset();
    }

    void reset() override
    {
        for (auto& c : ch)
        {
            c.os.reset(); c.preHp.reset(); c.hump.reset(); c.postLp.reset(); c.toneA.reset(); c.toneB.reset(); c.dcBlock.reset();
        }
    }

    void process (float* l, float* r, int n) noexcept override
    {
        if (consumeDirty())
        {
            const float d = v (pDrive) / 10.0f;
            gain.setTarget (dbToGain (vc.minGainDb + (vc.maxGainDb - vc.minGainDb) * d * d * (3.0f - 2.0f * d)));
            out.setTarget (dbToGain (v (pLevel) + vc.outTrimDb));
            bias = pBias >= 0 ? v (pBias) / 10.0f : 1.0f;
            updateTone();
        }
        float* io[2] = { l, r };
        for (int i = 0; i < n; ++i)
        {
            const float g = gain.next(), o = out.next();
            gain.next(); out.next(); // smoothers run at the oversampled rate
            for (int c = 0; c < 2; ++c)
            {
                auto& s = ch[c];
                float u0, u1;
                s.os.upsample (io[c][i], u0, u1);
                u0 = runOne (s, u0, g);
                u1 = runOne (s, u1, g);
                io[c][i] = applyTone (s, s.os.downsample (u0, u1)) * o;
            }
        }
    }

private:
    struct Channel
    {
        Oversampler2x os;
        vf::dsp::Biquad preHp, hump, postLp, toneA, toneB;
        vf::dsp::DcBlocker dcBlock;
    };

    float clipOne (float x) const noexcept
    {
        switch (vc.clip)
        {
            case Clip::Soft:    return shape::soft (x);
            case Clip::Hard:    return shape::hard (x * 0.9f);
            case Clip::Diode:   return shape::diode (x);
            case Clip::Asym:    return shape::asym (x, vc.asymmetry);
            case Clip::FuzzGe:  return shape::fuzz (x, 0.55f + 0.45f * bias) * 0.9f;
            case Clip::FuzzSi:  return shape::hard (shape::fuzz (x * 1.4f, 0.4f + 0.6f * bias));
            case Clip::Rectify: return shape::soft (std::fabs (x) * 1.6f - 0.3f) - shape::soft (-0.3f) + 0.25f * shape::soft (x); // zero in -> zero out
        }
        return x;
    }

    float runOne (Channel& s, float x, float g) noexcept
    {
        const float dry = x;
        float y = s.hump.process (s.preHp.process (x)) * g;
        for (int k = 0; k < vc.stages; ++k)
            y = clipOne (k == 0 ? y : y * 2.5f);
        if (vc.cleanBlend > 0.0f) y = y * (1.0f - vc.cleanBlend) + dry * vc.cleanBlend * 2.0f;
        return s.dcBlock.process (s.postLp.process (y));
    }

    void updateTone() noexcept
    {
        const double t = (pTone >= 0 ? v (pTone) : 5.0f) / 10.0;
        const double f = vc.toneLoHz * std::pow (vc.toneHiHz / vc.toneLoHz, t);
        for (auto& c : ch)
        {
            switch (vc.tone)
            {
                case ToneStyle::LowPass:
                    c.toneA.setCoeffs (vf::dsp::BiquadCoeffs::lowPass (fs, f, 0.7));
                    c.toneB.setCoeffs (vf::dsp::BiquadCoeffs::peak (fs, 1000.0, 0.7, 0.0));
                    break;
                case ToneStyle::Tilt:
                    c.toneA.setCoeffs (vf::dsp::BiquadCoeffs::highShelf (fs, 1200.0, 0.6, -9.0 + 18.0 * t));
                    c.toneB.setCoeffs (vf::dsp::BiquadCoeffs::lowShelf (fs, 300.0, 0.6, 4.0 - 8.0 * t));
                    break;
                case ToneStyle::MuffScoop: // passive LP/HP blend: deepest mid scoop at noon
                    c.toneA.setCoeffs (vf::dsp::BiquadCoeffs::peak (fs, 1000.0, 0.6, -9.0 * (1.0 - std::fabs (2.0 * t - 1.0) * 0.6)));
                    c.toneB.setCoeffs (vf::dsp::BiquadCoeffs::highShelf (fs, 1500.0, 0.6, -10.0 + 20.0 * t));
                    break;
                case ToneStyle::BigBand:
                    c.toneA.setCoeffs (vf::dsp::BiquadCoeffs::highShelf (fs, 2500.0, 0.7, -6.0 + 12.0 * t));
                    c.toneB.setCoeffs (vf::dsp::BiquadCoeffs::peak (fs, 700.0, 0.8, 2.0));
                    break;
                case ToneStyle::Treble:
                    c.toneA.setCoeffs (vf::dsp::BiquadCoeffs::highShelf (fs, 3000.0, 0.7, -12.0 + 16.0 * t));
                    c.toneB.setCoeffs (vf::dsp::BiquadCoeffs::lowShelf (fs, 150.0, 0.7, -3.0));
                    break;
            }
        }
    }

    float applyTone (Channel& s, float x) noexcept { return s.toneB.process (s.toneA.process (x)); }

    DriveVoicing vc;
    int pDrive = -1, pTone = -1, pLevel = -1, pBias = -1;
    float bias = 1.0f;
    vf::dsp::LinearSmoother gain, out;
    Channel ch[2];
};

/** Voicing table. Index order is part of the effect IDs: append only. */
inline const std::array<DriveVoicing, 26>& driveVoicings()
{
    static const std::array<DriveVoicing, 26> v {{
        // name                 inspired by                         preHP  hump  humpDb clip         st  minG  maxG  asym clean tone               lo     hi      postLP  trim  bias
        { "Green Screamer",     "Ibanez TS808 Tube Screamer",       720,   720,  6.0f, Clip::Diode,   1,  6.0f, 40.0f, 1.0f, 0.15f, ToneStyle::LowPass, 700,  5000,  7000,   -6.0f, false },
        { "Screamer Nine",      "Ibanez TS9",                       700,   760,  5.0f, Clip::Diode,   1,  6.0f, 40.0f, 1.0f, 0.10f, ToneStyle::LowPass, 900,  6500,  8500,   -4.0f, false },
        { "Centaur Gold",       "Klon Centaur",                     160,   900,  3.0f, Clip::Asym,    1,  0.0f, 32.0f, 1.3f, 0.45f, ToneStyle::Tilt,    500,  5000,  9000,   -5.0f, false },
        { "Breaker Blue",       "Marshall Bluesbreaker pedal",      100,   600,  1.5f, Clip::Soft,    1,  0.0f, 26.0f, 1.0f, 0.20f, ToneStyle::LowPass, 1200, 9000,  10000,  -3.0f, false },
        { "Rodent",             "ProCo RAT",                        60,    1500, 2.0f, Clip::Hard,    1, 10.0f, 58.0f, 1.0f, 0.00f, ToneStyle::LowPass, 600,  12000, 12000,  -12.0f, false },
        { "Orange Box DS",      "Boss DS-1",                        250,   1000, 1.0f, Clip::Hard,    1,  8.0f, 46.0f, 1.0f, 0.00f, ToneStyle::MuffScoop, 400, 7000, 9000,  -10.0f, false },
        { "Metal Core",         "Boss MT-2 Metal Zone",             180,   2200, 5.0f, Clip::Hard,    2, 18.0f, 60.0f, 1.0f, 0.00f, ToneStyle::MuffScoop, 500, 6000, 8000,  -14.0f, false },
        { "Smiley Ge",          "Dallas Arbiter Fuzz Face (germanium)", 40, 400,  0.0f, Clip::FuzzGe,  1,  8.0f, 36.0f, 1.6f, 0.00f, ToneStyle::LowPass, 1500, 9000,  7000,   -6.0f, true  },
        { "Smiley Si",          "Fuzz Face (silicon)",              60,    500,  0.0f, Clip::FuzzSi,  1, 12.0f, 44.0f, 1.4f, 0.00f, ToneStyle::LowPass, 2000, 12000, 9000,   -9.0f, true  },
        { "Big Fluff",          "Electro-Harmonix Big Muff Pi",     90,    800,  0.0f, Clip::Diode,   2, 16.0f, 52.0f, 1.0f, 0.00f, ToneStyle::MuffScoop, 400, 7000, 8000,  -12.0f, false },
        { "Bender Ge",          "Sola Sound Tone Bender (germanium)", 120, 900,  2.0f, Clip::FuzzGe,  2,  8.0f, 34.0f, 1.8f, 0.00f, ToneStyle::LowPass, 1800, 8000,  6500,   -6.0f, true  },
        { "Factory Si",         "Z.Vex Fuzz Factory (silicon, gated)", 80, 1200, 0.0f, Clip::FuzzSi,  2, 14.0f, 48.0f, 1.2f, 0.00f, ToneStyle::Treble,  1000, 9000,  10000,  -10.0f, true  },
        { "Clear Drive",        "Paul Cochrane Timmy (transparent)", 300,  1000, 0.0f, Clip::Diode,   1,  0.0f, 30.0f, 1.0f, 0.30f, ToneStyle::Tilt,    800,  8000,  12000,  -3.0f, false },
        { "Clean Boost",        "MXR Micro Amp / clean boosts",     30,    1000, 0.0f, Clip::Soft,    1,  0.0f, 20.0f, 1.0f, 0.00f, ToneStyle::Tilt,    1000, 10000, 16000,  -6.0f, false },
        { "Range Treble",       "Dallas Rangemaster treble booster", 1500, 2500, 3.0f, Clip::FuzzGe,  1,  4.0f, 26.0f, 1.5f, 0.10f, ToneStyle::Treble,  2000, 8000,  9000,   -2.0f, false },
        { "Echo Preamp",        "Echoplex EP-3 preamp (EP Booster)", 40,   400, -1.0f, Clip::Asym,    1,  2.0f, 20.0f, 1.25f, 0.10f, ToneStyle::Tilt,   900,  9000,  12000,  -3.0f, false },
        { "Hard Clipper",       "generic op-amp hard clipping",     80,    1000, 0.0f, Clip::Hard,    1,  6.0f, 48.0f, 1.0f, 0.00f, ToneStyle::LowPass, 1000, 12000, 14000,  -10.0f, false },
        { "Soft Clipper",       "generic soft (tanh) saturation",   50,    1000, 0.0f, Clip::Soft,    1,  0.0f, 36.0f, 1.0f, 0.00f, ToneStyle::LowPass, 1000, 12000, 14000,  -6.0f, false },
        { "Modern Gain",        "modern high-gain pedals",          220,   1600, 4.0f, Clip::Asym,    3, 20.0f, 60.0f, 1.15f, 0.00f, ToneStyle::BigBand, 1000, 9000, 9000,   -16.0f, false },
        { "Vintage Plus",       "MXR Distortion+ (germanium diodes)", 120, 1000, 0.0f, Clip::Diode,   1, 10.0f, 44.0f, 1.0f, 0.00f, ToneStyle::LowPass, 1500, 6000,  6500,   -8.0f, false },
        { "Octavian",           "Roger Mayer Octavia (octave fuzz)", 300,  1200, 2.0f, Clip::Rectify, 1, 10.0f, 40.0f, 1.0f, 0.00f, ToneStyle::Treble,  1500, 9000,  8000,   -8.0f, false },
        { "Plate In A Box",     "Marshall-in-a-box pedals",         140,   1100, 3.0f, Clip::Asym,    2, 10.0f, 44.0f, 1.3f, 0.00f, ToneStyle::BigBand, 1000, 9000,  7500,   -10.0f, false },
        { "Tweed In A Box",     "tweed-amp-in-a-box pedals",        80,    500,  2.0f, Clip::Asym,    1,  6.0f, 34.0f, 1.5f, 0.05f, ToneStyle::LowPass, 1500, 7000,  6000,   -6.0f, false },
        { "Brown Box",          "'brown sound' amp-in-a-box pedals", 180,  900,  4.0f, Clip::Asym,    2, 12.0f, 50.0f, 1.2f, 0.00f, ToneStyle::BigBand, 1000, 8000,  7000,   -12.0f, false },
        { "Blues Pilot",        "Boss BD-2 Blues Driver",           90,    800,  1.0f, Clip::Asym,    2,  2.0f, 34.0f, 1.4f, 0.10f, ToneStyle::Tilt,    1000, 9000,  11000,  -6.0f, false },
        { "Obsessive Drive",    "Fulltone OCD",                     110,   900,  2.0f, Clip::Hard,    1,  8.0f, 42.0f, 1.0f, 0.05f, ToneStyle::LowPass, 1000, 10000, 10000,  -9.0f, false },
    }};
    return v;
}

} // namespace vf::fx
