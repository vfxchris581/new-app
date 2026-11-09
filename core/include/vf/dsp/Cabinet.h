// Viridian Forge — DSP core (JUCE-independent)
//
// Parametric speaker cabinet + close-mic simulation (Phase 2 "basic cab").
// Each cabinet is described by the features that define how a miked guitar
// cab sounds: low-frequency cut-off (open vs closed back), cabinet/speaker
// resonance, the speaker's midrange voice, presence peak, cone break-up
// peaks and the top-end roll-off. The mic controls move the virtual mic
// across the cone (centre = bright, edge = dark) and away from it
// (close = proximity bass boost, far = thinner, smoother top).
// Phase 3 adds measured IRs via convolution alongside this model.
#pragma once

#include "Biquad.h"
#include <algorithm>
#include <array>
#include <complex>
#include <vector>

namespace vf::dsp
{

struct CabinetSpec
{
    const char* name;
    const char* speaker;   // speaker family the voicing is inspired by
    bool openBack;
    float lowCutHz;        // where the low end falls away
    float resonanceHz, resonanceDb, resonanceQ;
    float midHz, midDb, midQ;          // speaker midrange voice (+ or -)
    float presenceHz, presenceDb;      // main upper-mid peak
    float breakupHz, breakupDb;        // cone break-up peak
    float rollOffHz;                   // top-end roll-off (centre mic)
};

enum class CabModel { American1x12Open = 0, Alnico2x12Open, Tweed4x10Open, Greenback4x12, V30_4x12, Modern4x12, British1x12Closed, count };

inline const CabinetSpec& getCabinetSpec (CabModel m)
{
    static const std::array<CabinetSpec, static_cast<size_t> (CabModel::count)> specs {{
        { "1x12 American Open",  "Jensen-style ceramic", true,  95.0f, 120.0f, 2.5f, 1.4f,   700.0f, -2.0f, 0.9f, 2400.0f, 3.5f, 4600.0f, 2.5f, 5600.0f },
        { "2x12 Alnico Open",    "Alnico Blue-style",    true,  85.0f, 105.0f, 2.0f, 1.2f,  1100.0f,  1.5f, 1.0f, 3100.0f, 4.5f, 5200.0f, 3.0f, 6200.0f },
        { "4x10 Tweed Open",     "Vintage alnico 10s",   true, 100.0f, 140.0f, 3.0f, 1.3f,   900.0f,  2.0f, 0.8f, 2200.0f, 3.0f, 4000.0f, 2.0f, 5000.0f },
        { "4x12 Greenback",      "Greenback-style",      false, 70.0f,  95.0f, 4.5f, 1.6f,  1400.0f,  2.5f, 0.9f, 2000.0f, 3.0f, 3600.0f, 2.0f, 4800.0f },
        { "4x12 V30",            "Vintage 30-style",     false, 72.0f, 100.0f, 4.0f, 1.8f,   850.0f, -2.5f, 1.2f, 2800.0f, 6.0f, 4700.0f, 3.5f, 5600.0f },
        { "4x12 Modern Metal",   "Modern ceramic",       false, 65.0f,  85.0f, 5.5f, 1.8f,   600.0f, -3.5f, 1.0f, 3300.0f, 5.0f, 5600.0f, 3.0f, 6500.0f },
        { "1x12 British Closed", "Creamback-style",      false, 85.0f, 115.0f, 3.5f, 1.5f,  1200.0f,  1.0f, 0.9f, 2500.0f, 3.5f, 4300.0f, 2.0f, 5300.0f },
    }};
    return specs[static_cast<size_t> (m)];
}

/** Microphone families with their characteristic close-mic response. */
enum class MicType { Dynamic57 = 0, Ribbon121, Condenser, Dynamic421, Dynamic906, BroadcastRE20, LargeCondenser414,
                     TubeCondenser47, SmallCondenser84, Ribbon160, count };

inline constexpr std::array<const char*, static_cast<size_t> (MicType::count)> micTypeNames {
    "Dynamic (57-style)", "Ribbon (121-style)", "Condenser (87-style)", "Dynamic (421-style)", "Dynamic (e906-style)",
    "Broadcast Dynamic (RE20-style)", "Large Condenser (414-style)", "Tube Condenser (U47-style)",
    "Small Condenser (KM84-style)", "Ribbon (160-style)"
};

/** How a mic family colours a close-miked speaker. Multipliers are relative to the
    classic dynamic; dB values are added shelves/peaks. */
struct MicSpec
{
    double roll;          // top-end extension (x rolloff frequency)
    double presence;      // upper-mid peak depth
    double proximity;     // close-mic bass boost
    double breakup;       // cone break-up peak emphasis
    double presenceHz;    // where its presence peak sits (x speaker presence frequency)
    double airDb;         // 9 kHz shelf
    double lowDb;         // 120 Hz shelf (body / weight)
    double bodyDb;        // 350 Hz peak (warmth vs. boxiness)
    double offAxis;       // how fast highs fall when angled (1 = cardioid dynamic)
};

inline const MicSpec& getMicSpec (MicType m)
{
    static const std::array<MicSpec, static_cast<size_t> (MicType::count)> specs {{
        { 1.00, 1.00, 1.00, 1.00, 1.00,  0.0,  0.0,  0.0, 1.00 }, // Dynamic 57: the upper-mid bite reference
        { 0.72, 0.45, 1.50, 0.50, 1.00, -2.0,  1.0,  0.5, 0.60 }, // Ribbon 121: smooth, dark, big proximity
        { 1.35, 0.70, 0.70, 1.20, 1.00,  1.5,  0.0,  0.0, 0.80 }, // Condenser 87: extended, open top
        { 1.15, 0.80, 1.10, 1.00, 0.85,  0.5,  1.5,  1.0, 0.90 }, // Dynamic 421: fuller lows, lower presence
        { 1.05, 1.20, 0.90, 1.00, 1.10, -0.5, -1.0, -1.0, 0.70 }, // Dynamic e906: focused mids, tight lows
        { 0.85, 0.50, 0.40, 0.80, 1.00, -1.5,  0.5,  1.0, 0.90 }, // Broadcast RE20: flat, little proximity
        { 1.30, 0.90, 0.80, 1.10, 1.30,  3.5, -1.0, -0.8, 0.80 }, // Large condenser 414: bright, detailed, lean
        { 1.10, 0.75, 1.00, 0.90, 0.90,  1.0,  1.5,  1.5, 0.80 }, // Tube condenser 47: warm, thick
        { 1.45, 0.60, 0.60, 1.20, 1.20,  2.0, -1.5, -0.5, 0.95 }, // Small condenser 84: fast, airy, lean
        { 0.80, 0.60, 1.20, 0.60, 1.10, -1.0,  0.5,  0.0, 0.70 }, // Ribbon 160: hypercardioid ribbon, focused
    }};
    return specs[static_cast<size_t> (m)];
}

inline constexpr std::array<const char*, static_cast<size_t> (CabModel::count)> cabModelNames {
    "1x12 American Open", "2x12 Alnico Open", "4x10 Tweed Open", "4x12 Greenback", "4x12 V30", "4x12 Modern Metal", "1x12 British Closed"
};

/** Physical layout of a cabinet's baffle (for mic placement and speaker bleed). */
struct CabGeometry
{
    int speakers = 1;          // 1, 2 or 4
    int columns = 1;           // 1 or 2; rows = speakers / columns
    float spacingCm = 34.0f;   // centre-to-centre
    float coneRadiusCm = 13.0f;

    /** Speaker centre relative to the baffle centre, in cm (x right, y down). */
    void centre (int k, float& x, float& y) const noexcept
    {
        const int rows = speakers / columns;
        const int c = k % columns, r = k / columns;
        x = (static_cast<float> (c) - 0.5f * static_cast<float> (columns - 1)) * spacingCm;
        y = (static_cast<float> (r) - 0.5f * static_cast<float> (rows - 1)) * spacingCm;
    }
};

inline CabGeometry getCabGeometry (CabModel m) noexcept
{
    switch (m)
    {
        case CabModel::Alnico2x12Open: return { 2, 2, 34.0f, 13.0f };
        case CabModel::Tweed4x10Open:  return { 4, 2, 28.0f, 11.0f };
        case CabModel::Greenback4x12:
        case CabModel::V30_4x12:
        case CabModel::Modern4x12:     return { 4, 2, 34.0f, 13.0f };
        case CabModel::American1x12Open:
        case CabModel::British1x12Closed:
        case CabModel::count:
        default:                       return { 1, 1, 34.0f, 13.0f };
    }
}

inline constexpr float kMaxMicDistanceCm = 30.0f;  // distance knob 10 = 30 cm off the grille
inline constexpr float kMaxMicAngleDeg = 60.0f;

class CabinetSim
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        dirty = true;
        update();
        reset();
    }

    void reset()
    {
        for (auto& b : filters) b.reset();
    }

    /** micPosition: 0 = cone centre (bright) .. 1 = cone edge (dark).
        micDistance: 0 = on the grille (proximity boost) .. 1 = ~30 cm back. */
    void setParams (CabModel model, float micPosition, float micDistance, MicType micType = MicType::Dynamic57) noexcept
    {
        if (model != current || controlChanged (micPosition, pos) || controlChanged (micDistance, dist) || micType != mic)
        {
            current = model; pos = micPosition; dist = micDistance; mic = micType;
            dirty = true;
        }
    }

    /** Which speaker of a multi-speaker cab is miked (no two speakers sound quite the same)
        and how far the mic is turned off-axis (0..60 degrees: darker, less presence). */
    void setPlacement (int speakerIndex, float angleDegrees) noexcept
    {
        if (speakerIndex != speaker || controlChanged (angleDegrees, angle))
        {
            speaker = speakerIndex; angle = angleDegrees;
            dirty = true;
        }
    }

    /** Mic in front of the baffle (normal) or behind the cab. */
    void setBack (bool behind) noexcept
    {
        if (behind != back) { back = behind; dirty = true; }
    }

    /** Recompute coefficients if anything changed (cheap; call once per block). */
    void update() noexcept
    {
        if (! dirty) return;
        dirty = false;
        const auto& c = getCabinetSpec (current);
        const double p = pos, d = dist;

        // Speaker-to-speaker variation (manufacturing spread + break-in): small, fixed per position.
        static constexpr double variation[4] { 0.0, 1.0, -0.8, 0.5 };
        const int spk = std::min (std::max (0, speaker), getCabGeometry (current).speakers - 1);
        const double v = variation[spk];
        // Off-axis: the mic turns away from the dust cap; highs fall first.
        const double a = std::min (1.0, std::max (0.0, static_cast<double> (angle) / kMaxMicAngleDeg));

        const auto& ms = getMicSpec (mic);
        double micRoll = ms.roll, micPresence = ms.presence, micProximity = ms.proximity, micBreakup = ms.breakup;

        // Behind the cab: open back = the cone's rear (darker, the magnet blocks the centre,
        // more low end); closed back = heard through the panel (heavily muffled, boomy).
        double backHighDb = 0.0, backLowDb = 0.0;
        if (back)
        {
            if (c.openBack) { micRoll *= 0.7; micPresence *= 0.45 + 0.4 * p; micBreakup *= 0.5; backHighDb = -4.0; backLowDb = 2.5; }
            else            { micRoll *= 0.35; micPresence *= 0.15; micBreakup *= 0.1; backHighDb = -14.0; backLowDb = 5.0; }
        }

        const double lowCut = c.lowCutHz * (1.0 + 0.25 * d);              // further away: less low end
        const double proximityDb = 5.0 * micProximity * (1.0 - d) * (1.0 - d); // close-mic bass boost
        const double rollOff = std::fmin (fs * 0.45, c.rollOffHz * micRoll * (1.0 - 0.45 * p) * (1.0 - 0.15 * d));
        const double presenceDb = c.presenceDb * micPresence * (1.0 - 0.6 * p) * (1.0 - 0.45 * a);
        const double breakupDb = c.breakupDb * micBreakup * (1.0 - 0.8 * p) - 1.5 * d + 0.8 * v;
        const double offAxisDb = -10.0 * a * a * ms.offAxis;

        filters[0].setCoeffs (BiquadCoeffs::highPass (fs, lowCut, c.openBack ? 0.6 : 0.85));
        filters[1].setCoeffs (BiquadCoeffs::peak (fs, c.resonanceHz, c.resonanceQ, c.openBack ? c.resonanceDb * 0.7 : c.resonanceDb));
        filters[2].setCoeffs (BiquadCoeffs::lowShelf (fs, 180.0, 0.7, proximityDb));
        filters[3].setCoeffs (BiquadCoeffs::peak (fs, c.midHz, c.midQ, c.midDb + 0.4 * v));
        filters[4].setCoeffs (BiquadCoeffs::peak (fs, c.presenceHz * ms.presenceHz * (1.0 + 0.04 * v), 1.1, presenceDb));
        filters[5].setCoeffs (BiquadCoeffs::peak (fs, c.breakupHz * (1.0 - 0.05 * v), 2.2, breakupDb));
        filters[6].setCoeffs (BiquadCoeffs::lowPass (fs, rollOff, 0.54));   // 4th-order roll-off
        filters[7].setCoeffs (BiquadCoeffs::lowPass (fs, rollOff, 1.31));
        filters[8].setCoeffs (BiquadCoeffs::lowPass (fs, std::fmin (11000.0, fs * 0.45), 0.7)); // residual fizz control
        filters[9].setCoeffs (BiquadCoeffs::highShelf (fs, 2800.0, 0.6, offAxisDb));
        filters[10].setCoeffs (BiquadCoeffs::highShelf (fs, std::fmin (9000.0, fs * 0.4), 0.7, ms.airDb));
        filters[11].setCoeffs (BiquadCoeffs::lowShelf (fs, 120.0, 0.7, ms.lowDb + backLowDb));
        filters[12].setCoeffs (BiquadCoeffs::peak (fs, 350.0, 0.9, ms.bodyDb));
        filters[13].setCoeffs (BiquadCoeffs::highShelf (fs, c.openBack ? 2500.0 : 900.0, 0.6, backHighDb));
    }

    float process (float x) noexcept
    {
        for (auto& f : filters) x = f.process (x);
        return x;
    }

    double magnitudeAt (double freq) const noexcept { return std::abs (responseAt (freq)); }

    std::complex<double> responseAt (double freq) const noexcept
    {
        std::complex<double> h (1.0, 0.0);
        for (const auto& f : filters) h *= f.getCoeffs().responseAt (freq, fs);
        return h;
    }

private:
    double fs = 48000.0;
    std::array<Biquad, 14> filters;
    CabModel current = CabModel::Greenback4x12;
    MicType mic = MicType::Dynamic57;
    float pos = 0.3f, dist = 0.2f, angle = 0.0f;
    int speaker = 0;
    bool back = false;
    bool dirty = true;
};

/** Small stereo room, as heard by a pair of room mics: early reflections off nearby
    walls. Turns the mono amp/cab signal into a natural stereo image. The room-mic
    distance moves the pair back from the cab: later, sparser, darker reflections. */
class RoomAmbience
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        const size_t len = static_cast<size_t> ((kMaxScale * 35.0 + kMaxPreDelayMs + 2.0) * 0.001 * fs) + 2;
        line.assign (len, 0.0f);
        writePos = 0;
        damping.reset();
        configured = -1.0f;
        setDistance (distance);
    }

    /** 0 = room mics close to the cab (the Phase 3 sound) .. 1 = far back in the room. */
    void setDistance (float d) noexcept
    {
        d = std::min (1.0f, std::max (0.0f, d));
        distance = d;
        if (std::fabs (d - configured) < 1.0e-4f || line.empty()) return;
        configured = d;
        const double scale = 1.0 + (kMaxScale - 1.0) * d, pre = kMaxPreDelayMs * d;
        for (size_t i = 0; i < taps; ++i)
        {
            tapL[i] = static_cast<int> ((msL[i] * scale + pre) * 0.001 * fs);
            tapR[i] = static_cast<int> ((msR[i] * scale + pre) * 0.001 * fs);
        }
        damping.setCoeffs (BiquadCoeffs::lowPass (fs, 5500.0 - 2700.0 * d, 0.6));
        level = static_cast<float> (1.0 / (1.0 + 0.6 * d));
    }

    /** amount 0..1 (wet level). Writes stereo output. */
    void process (float in, float amount, float& left, float& right) noexcept
    {
        const float x = damping.process (in);
        line[writePos] = x;
        float l = 0.0f, r = 0.0f;
        const int n = static_cast<int> (line.size());
        for (size_t i = 0; i < taps; ++i)
        {
            l += gain[i] * line[static_cast<size_t> ((static_cast<int> (writePos) - tapL[i] + n) % n)];
            r += gain[i] * line[static_cast<size_t> ((static_cast<int> (writePos) - tapR[i] + n) % n)];
        }
        writePos = (writePos + 1) % line.size();
        left = in + amount * level * l;
        right = in + amount * level * r;
    }

    void reset() noexcept { std::fill (line.begin(), line.end(), 0.0f); damping.reset(); }

private:
    static constexpr size_t taps = 6;
    static constexpr double kMaxScale = 2.6, kMaxPreDelayMs = 9.0;
    static constexpr float msL[taps] { 3.1f, 7.3f, 11.9f, 17.3f, 23.9f, 31.7f };
    static constexpr float msR[taps] { 4.3f, 8.9f, 13.7f, 19.1f, 26.3f, 34.1f };
    static constexpr float gain[taps] { 0.55f, -0.42f, 0.33f, -0.26f, 0.2f, -0.15f };
    double fs = 48000.0;
    std::vector<float> line;
    size_t writePos = 0;
    int tapL[taps] {}, tapR[taps] {};
    float distance = 0.0f, configured = -1.0f, level = 1.0f;
    Biquad damping;
};

} // namespace vf::dsp
