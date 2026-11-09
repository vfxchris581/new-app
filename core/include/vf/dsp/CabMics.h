// Viridian Forge — DSP core (JUCE-independent)
//
// Two-microphone cabinet capture. Each mic has its own position on a chosen
// speaker (cone centre .. edge), distance from the grille, off-axis angle, type,
// level and polarity. What the physics adds on top of the single-mic model:
//
//  - arrival time: sound reaches a mic 1 ms later per ~34 cm, so two mics at
//    different distances comb-filter when mixed (the classic "phasey" two-mic
//    sound), and Time Align removes it by delaying the closer mic;
//  - speaker bleed: backed off a multi-speaker cab, a mic also hears the other
//    speakers, later and duller, which adds the natural comb/"air" of a distant
//    mic; on the grille it hears only its own speaker.
//
// All delays are smoothed, so dragging a mic while playing is click-free.
#pragma once

#include "Cabinet.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

namespace vf::dsp
{

struct MicSettings
{
    bool enabled = true;
    MicType type = MicType::Dynamic57;
    int speaker = 0;          // which speaker (clamped to the cab's count)
    float position = 0.3f;    // 0 = dust cap .. 1 = cone edge
    float distance = 0.2f;    // 0 = on the grille .. 1 = kMaxMicDistanceCm back
    float angle = 0.0f;       // degrees off-axis, 0..kMaxMicAngleDeg
    float levelDb = 0.0f;
    bool invert = false;
};

class CabMics
{
public:
    static constexpr double kSpeedOfSoundCmPerSec = 34300.0;
    static constexpr int kMaxBleed = 3;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        const size_t len = static_cast<size_t> (0.006 * fs) + 8;
        for (auto& m : mics)
        {
            m.sim.prepare (fs);
            m.line.assign (len, 0.0f);
            m.bleedFilter.setCoeffs (BiquadCoeffs::lowPass (fs, 2600.0, 0.6));
        }
        smoothing = static_cast<float> (1.0 - std::exp (-1.0 / (0.012 * fs)));
        configure (true);
        reset();
    }

    void reset() noexcept
    {
        for (auto& m : mics)
        {
            m.sim.reset();
            std::fill (m.line.begin(), m.line.end(), 0.0f);
            m.writePos = 0;
            m.bleedFilter.reset();
            m.delay = m.targetDelay;
            m.bleedDelay = m.targetBleedDelay;
            m.gain = m.targetGain;
        }
    }

    /** Call once per block. */
    void set (CabModel model, const MicSettings& a, const MicSettings& b, bool timeAlign) noexcept
    {
        cab = model;
        settings[0] = a;
        settings[1] = b;
        align = timeAlign;
        configure (false);
    }

    bool isMicBEnabled() const noexcept { return settings[1].enabled; }

    /** One input sample in, each mic's signal out (level, polarity and delay applied). */
    void process (float x, float& outA, float& outB) noexcept
    {
        outA = processMic (0, x);
        outB = settings[1].enabled ? processMic (1, x) : 0.0f;
    }

    /** Frequency response of one mic as configured (for tests and the UI curve). */
    std::complex<double> responseAt (int micIndex, double freq) const noexcept
    {
        const auto& m = mics[static_cast<size_t> (micIndex)];
        const double w = 2.0 * kPi * freq / fs;
        std::complex<double> h = std::polar (1.0, -w * static_cast<double> (m.targetDelay));
        const auto lp = m.bleedFilter.getCoeffs().responseAt (freq, fs);
        for (size_t j = 0; j < static_cast<size_t> (m.numBleed); ++j)
            h += static_cast<double> (m.bleedGain[j]) * lp * std::polar (1.0, -w * static_cast<double> (m.targetBleedDelay[j]));
        return h * m.sim.responseAt (freq) * static_cast<double> (m.targetGain);
    }

    /** The mix a listener hears with both mics summed in mono (B halves the sum so enabling it keeps the level). */
    std::complex<double> monoResponseAt (double freq) const noexcept
    {
        if (! settings[1].enabled) return responseAt (0, freq);
        return 0.5 * (responseAt (0, freq) + responseAt (1, freq));
    }

    /** Arrival delay of a mic in samples (after time alignment). */
    float delaySamples (int micIndex) const noexcept { return mics[static_cast<size_t> (micIndex)].targetDelay; }

private:
    struct Mic
    {
        CabinetSim sim;
        std::vector<float> line;
        size_t writePos = 0;
        Biquad bleedFilter;
        float delay = 0.0f, targetDelay = 0.0f, gain = 1.0f, targetGain = 1.0f;
        std::array<float, kMaxBleed> bleedDelay {}, targetBleedDelay {}, bleedGain {};
        int numBleed = 0;
    };

    void configure (bool snap) noexcept
    {
        const auto geo = getCabGeometry (cab);
        float ownPath[2] {};
        for (int k = 0; k < 2; ++k)
        {
            const auto& s = settings[static_cast<size_t> (k)];
            auto& m = mics[static_cast<size_t> (k)];
            const int spk = std::min (std::max (0, s.speaker), geo.speakers - 1);
            m.sim.setParams (cab, s.position, s.distance, s.type);
            m.sim.setPlacement (spk, s.angle);
            m.sim.update();

            // Own speaker: straight-line distance (plus a little for the cone depth).
            const float dcm = std::max (0.0f, std::min (1.0f, s.distance)) * kMaxMicDistanceCm;
            ownPath[k] = dcm + 1.0f;

            // Other speakers: further away and arriving later; negligible right on the grille.
            m.numBleed = 0;
            float sx, sy;
            geo.centre (spk, sx, sy);
            for (int j = 0; j < geo.speakers && m.numBleed < kMaxBleed; ++j)
            {
                if (j == spk) continue;
                float ox, oy;
                geo.centre (j, ox, oy);
                const float lateral = std::hypot (ox - sx, oy - sy);
                const float path = std::hypot (dcm, lateral) + 1.0f;
                const float ratio = dcm / std::max (1.0f, path);
                m.bleedGain[static_cast<size_t> (m.numBleed)] = 0.4f * ratio * ratio * std::min (1.0f, dcm / 8.0f);
                m.targetBleedDelay[static_cast<size_t> (m.numBleed)] = path;   // cm for now
                ++m.numBleed;
            }
            m.targetGain = std::pow (10.0f, s.levelDb / 20.0f) * (s.invert ? -1.0f : 1.0f);
        }
        const float common = settings[1].enabled ? std::max (ownPath[0], ownPath[1]) : ownPath[0];
        const float cmToSamples = static_cast<float> (fs / kSpeedOfSoundCmPerSec);
        for (int k = 0; k < 2; ++k)
        {
            auto& m = mics[static_cast<size_t> (k)];
            const float extra = align ? common - ownPath[k] : 0.0f;  // delay the closer mic to line both up
            m.targetDelay = (ownPath[k] + extra) * cmToSamples;
            for (int j = 0; j < m.numBleed; ++j)
                m.targetBleedDelay[static_cast<size_t> (j)] = (m.targetBleedDelay[static_cast<size_t> (j)] + extra) * cmToSamples;
            if (snap)
            {
                m.delay = m.targetDelay;
                m.gain = m.targetGain;
                for (int j = 0; j < kMaxBleed; ++j) m.bleedDelay[static_cast<size_t> (j)] = m.targetBleedDelay[static_cast<size_t> (j)];
            }
        }
    }

    static float readFrac (const std::vector<float>& line, size_t writePos, float delay) noexcept
    {
        const size_t n = line.size();
        delay = std::min (std::max (delay, 1.0f), static_cast<float> (n - 3));
        const int di = static_cast<int> (delay);
        const float f = delay - static_cast<float> (di);
        const size_t i0 = (writePos + n - static_cast<size_t> (di)) % n;
        const size_t i1 = (i0 + n - 1) % n;
        return line[i0] + f * (line[i1] - line[i0]);
    }

    float processMic (int k, float x) noexcept
    {
        auto& m = mics[static_cast<size_t> (k)];
        m.line[m.writePos] = m.sim.process (x);
        m.delay += smoothing * (m.targetDelay - m.delay);
        m.gain += smoothing * (m.targetGain - m.gain);
        float y = readFrac (m.line, m.writePos, m.delay);
        if (m.numBleed > 0)
        {
            float b = 0.0f;
            for (int j = 0; j < m.numBleed; ++j)
            {
                auto& d = m.bleedDelay[static_cast<size_t> (j)];
                d += smoothing * (m.targetBleedDelay[static_cast<size_t> (j)] - d);
                b += m.bleedGain[static_cast<size_t> (j)] * readFrac (m.line, m.writePos, d);
            }
            y += m.bleedFilter.process (b);
        }
        m.writePos = (m.writePos + 1) % m.line.size();
        return y * m.gain;
    }

    double fs = 48000.0;
    float smoothing = 0.002f;
    CabModel cab = CabModel::Greenback4x12;
    std::array<MicSettings, 2> settings { MicSettings {}, MicSettings { false } };
    std::array<Mic, 2> mics;
    bool align = false;
};

} // namespace vf::dsp
