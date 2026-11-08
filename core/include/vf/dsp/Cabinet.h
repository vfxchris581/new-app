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
#include <array>
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
enum class MicType { Dynamic57 = 0, Ribbon121, Condenser, count };

inline constexpr std::array<const char*, static_cast<size_t> (MicType::count)> micTypeNames {
    "Dynamic (57-style)", "Ribbon (121-style)", "Condenser (87-style)"
};

inline constexpr std::array<const char*, static_cast<size_t> (CabModel::count)> cabModelNames {
    "1x12 American Open", "2x12 Alnico Open", "4x10 Tweed Open", "4x12 Greenback", "4x12 V30", "4x12 Modern Metal", "1x12 British Closed"
};

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

    /** Recompute coefficients if anything changed (cheap; call once per block). */
    void update() noexcept
    {
        if (! dirty) return;
        dirty = false;
        const auto& c = getCabinetSpec (current);
        const double p = pos, d = dist;

        // Mic family: dynamic = classic upper-mid bite; ribbon = smooth, darker, big
        // proximity effect; condenser = extended top, flatter presence.
        double micRoll = 1.0, micPresence = 1.0, micProximity = 1.0, micBreakup = 1.0;
        switch (mic)
        {
            case MicType::Ribbon121: micRoll = 0.72; micPresence = 0.45; micProximity = 1.5; micBreakup = 0.5; break;
            case MicType::Condenser: micRoll = 1.35; micPresence = 0.7;  micProximity = 0.7; micBreakup = 1.2; break;
            case MicType::Dynamic57:
            case MicType::count:     break;
        }

        const double lowCut = c.lowCutHz * (1.0 + 0.25 * d);              // further away: less low end
        const double proximityDb = 5.0 * micProximity * (1.0 - d) * (1.0 - d); // close-mic bass boost
        const double rollOff = std::fmin (fs * 0.45, c.rollOffHz * micRoll * (1.0 - 0.45 * p) * (1.0 - 0.15 * d));
        const double presenceDb = c.presenceDb * micPresence * (1.0 - 0.6 * p);
        const double breakupDb = c.breakupDb * micBreakup * (1.0 - 0.8 * p) - 1.5 * d;

        filters[0].setCoeffs (BiquadCoeffs::highPass (fs, lowCut, c.openBack ? 0.6 : 0.85));
        filters[1].setCoeffs (BiquadCoeffs::peak (fs, c.resonanceHz, c.resonanceQ, c.openBack ? c.resonanceDb * 0.7 : c.resonanceDb));
        filters[2].setCoeffs (BiquadCoeffs::lowShelf (fs, 180.0, 0.7, proximityDb));
        filters[3].setCoeffs (BiquadCoeffs::peak (fs, c.midHz, c.midQ, c.midDb));
        filters[4].setCoeffs (BiquadCoeffs::peak (fs, c.presenceHz, 1.1, presenceDb));
        filters[5].setCoeffs (BiquadCoeffs::peak (fs, c.breakupHz, 2.2, breakupDb));
        filters[6].setCoeffs (BiquadCoeffs::lowPass (fs, rollOff, 0.54));   // 4th-order roll-off
        filters[7].setCoeffs (BiquadCoeffs::lowPass (fs, rollOff, 1.31));
        filters[8].setCoeffs (BiquadCoeffs::lowPass (fs, std::fmin (11000.0, fs * 0.45), 0.7)); // residual fizz control
    }

    float process (float x) noexcept
    {
        for (auto& f : filters) x = f.process (x);
        return x;
    }

    double magnitudeAt (double freq) const noexcept
    {
        double m = 1.0;
        for (const auto& f : filters) m *= f.getCoeffs().magnitudeAt (freq, fs);
        return m;
    }

private:
    double fs = 48000.0;
    std::array<Biquad, 9> filters;
    CabModel current = CabModel::Greenback4x12;
    MicType mic = MicType::Dynamic57;
    float pos = 0.3f, dist = 0.2f;
    bool dirty = true;
};

/** Small stereo room: early reflections off nearby walls, as heard by a room mic.
    Turns the mono amp/cab signal into a natural stereo image. */
class RoomAmbience
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        const size_t len = static_cast<size_t> (0.04 * fs) + 2;
        line.assign (len, 0.0f);
        writePos = 0;
        damping.setCoeffs (BiquadCoeffs::lowPass (fs, 5500.0, 0.6));
        damping.reset();
        for (size_t i = 0; i < taps; ++i)
        {
            tapL[i] = static_cast<int> (msL[i] * 0.001 * fs);
            tapR[i] = static_cast<int> (msR[i] * 0.001 * fs);
        }
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
        left = in + amount * l;
        right = in + amount * r;
    }

private:
    static constexpr size_t taps = 6;
    static constexpr float msL[taps] { 3.1f, 7.3f, 11.9f, 17.3f, 23.9f, 31.7f };
    static constexpr float msR[taps] { 4.3f, 8.9f, 13.7f, 19.1f, 26.3f, 34.1f };
    static constexpr float gain[taps] { 0.55f, -0.42f, 0.33f, -0.26f, 0.2f, -0.15f };
    double fs = 48000.0;
    std::vector<float> line;
    size_t writePos = 0;
    int tapL[taps] {}, tapR[taps] {};
    Biquad damping;
};

} // namespace vf::dsp
