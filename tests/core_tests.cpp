// Viridian Forge — core DSP tests (no external test framework needed).
// Run: ctest --test-dir build  (or ./build/tests/vf_core_tests)

#include "vf/Parameters.h"
#include "vf/dsp/AmpVoice.h"
#include "vf/dsp/Cabinet.h"
#include "vf/dsp/NoiseGate.h"
#include "vf/dsp/Resampler.h"
#include "vf/dsp/ToneShaper.h"
#include "vf/dsp/Triode.h"
#include "vf/dsp/Tuner.h"
#include "vf/models/AmpModels.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace vf::dsp;
namespace M = vf::models;

namespace
{
int failures = 0, checks = 0;

void check (bool ok, const std::string& what)
{
    ++checks;
    if (! ok) { ++failures; std::printf ("  FAIL: %s\n", what.c_str()); }
}

void test (const char* name, const std::function<void()>& fn)
{
    std::printf ("[ RUN ] %s\n", name);
    const int before = failures;
    fn();
    std::printf (failures == before ? "[  OK ] %s\n" : "[FAILED] %s\n", name);
}

std::vector<float> sine (double freq, double fs, int n, float amp)
{
    std::vector<float> v (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
        v[static_cast<size_t> (i)] = amp * static_cast<float> (std::sin (2.0 * kPi * freq * i / fs));
    return v;
}

double rms (const std::vector<float>& v, size_t start = 0)
{
    double s = 0.0;
    for (size_t i = start; i < v.size(); ++i) s += static_cast<double> (v[i]) * v[i];
    return std::sqrt (s / static_cast<double> (v.size() - start));
}

bool allFinite (const std::vector<float>& v)
{
    for (float x : v) if (! std::isfinite (x)) return false;
    return true;
}

/** Amplitude of one frequency component (single-bin DFT). */
double toneLevel (const std::vector<float>& v, double freq, double fs, size_t start)
{
    double re = 0.0, im = 0.0;
    for (size_t i = start; i < v.size(); ++i)
    {
        const double ph = 2.0 * kPi * freq * static_cast<double> (i) / fs;
        re += v[i] * std::cos (ph);
        im += v[i] * std::sin (ph);
    }
    return 2.0 * std::sqrt (re * re + im * im) / static_cast<double> (v.size() - start);
}

/** Process in host-sized blocks, like a real audio callback. */
std::vector<float> runAmp (const AmpSettings& s, const std::vector<float>& in, double fs)
{
    AmpVoice amp;
    amp.setSettings (s);
    amp.prepare (fs, 512);
    std::vector<float> out = in;
    for (size_t i = 0; i < out.size(); i += 512)
        amp.process (out.data() + i, static_cast<int> (std::min<size_t> (512, out.size() - i)));
    return out;
}

double thdOf (const std::vector<float>& out, double f0, double fs)
{
    const size_t start = out.size() / 2;
    const double f1 = toneLevel (out, f0, fs, start);
    double h = 0.0;
    for (int k = 2; k <= 8; ++k) { const double a = toneLevel (out, f0 * k, fs, start); h += a * a; }
    return f1 > 1.0e-9 ? std::sqrt (h) / f1 : 0.0;
}

/** Guitar-like reference: decaying note with harmonics. */
std::vector<float> pluck (double f0, double fs, double seconds, float amp)
{
    std::vector<float> v (static_cast<size_t> (fs * seconds));
    for (size_t i = 0; i < v.size(); ++i)
    {
        const double t = static_cast<double> (i) / fs;
        double x = 0.0;
        for (int h = 1; h <= 6; ++h) x += std::sin (2.0 * kPi * f0 * h * t) / h;
        v[i] = static_cast<float> (amp * std::exp (-t * 2.5) * x);
    }
    return v;
}

/** String-like note with a rich harmonic spectrum (for tonal fingerprints). */
std::vector<float> richPluck (double f0, double fs, double seconds, float amp)
{
    std::vector<float> v (static_cast<size_t> (fs * seconds));
    for (size_t i = 0; i < v.size(); ++i)
    {
        const double t = static_cast<double> (i) / fs;
        double x = 0.0;
        for (int h = 1; h <= 24; ++h) x += std::sin (2.0 * kPi * f0 * h * t + 0.7 * h) / h;
        v[i] = static_cast<float> (amp * 0.6 * std::exp (-t * 2.5) * x);
    }
    return v;
}

struct Variant { int model, channel; std::string label; };

std::vector<Variant> allVariants()
{
    std::vector<Variant> v;
    for (int m = 0; m < M::numAmpModels(); ++m)
        for (int c = 0; c < M::getAmpModel (m).numChannels; ++c)
            v.push_back ({ m, c, std::string (M::getAmpModel (m).name) + " / " + M::getAmpModel (m).channels[static_cast<size_t> (c)].name });
    return v;
}
} // namespace

int main()
{
    constexpr double fs = 192000.0; // AmpVoice runs at 4x oversampled 48 kHz

    test ("Biquad low-pass response", []
    {
        const auto c = BiquadCoeffs::lowPass (48000.0, 1000.0, 0.7071);
        check (std::fabs (c.magnitudeAt (20.0, 48000.0) - 1.0) < 0.01, "passband ~0 dB");
        check (std::fabs (20 * std::log10 (c.magnitudeAt (1000.0, 48000.0)) + 3.01) < 0.2, "-3 dB at cutoff");
        check (c.magnitudeAt (10000.0, 48000.0) < 0.02, "stopband attenuation");
    });

    test ("Smoother reaches target exactly", []
    {
        LinearSmoother s;
        s.reset (48000.0, 0.01, 0.0f);
        s.setTarget (1.0f);
        float v = 0.0f;
        for (int i = 0; i < 480; ++i) v = s.next();
        check (v == 1.0f && ! s.isSmoothing(), "ramp completes in 480 samples");
    });

    test ("Every tone stack is passive and its knobs act on the right bands", []
    {
        std::vector<ToneStackComponents> stacks;
        for (int m = 0; m < M::numAmpModels(); ++m)
        {
            stacks.push_back (M::getAmpModel (m).stack);
            for (int c = 0; c < M::getAmpModel (m).numChannels; ++c)
                if (M::getAmpModel (m).channels[static_cast<size_t> (c)].ownStack)
                    stacks.push_back (M::getAmpModel (m).channels[static_cast<size_t> (c)].stack);
        }
        for (const auto& comp : stacks)
        {
            ToneStack ts;
            ts.prepare (48000.0);
            ts.setComponents (comp);
            const std::string tag = comp.name;
            double maxGain = 0.0;
            for (double b : { 0.0, 0.5, 1.0 })
                for (double m : { 0.0, 0.5, 1.0 })
                    for (double t : { 0.0, 0.5, 1.0 })
                    {
                        ts.setControls (b, m, t);
                        for (double f : { 30.0, 100.0, 400.0, 1000.0, 3000.0, 8000.0 })
                            maxGain = std::fmax (maxGain, ts.magnitudeAt (f));
                    }
            check (maxGain <= 1.02, tag + ": passive network never exceeds unity gain");
            ts.setControls (0.0, 0.5, 0.5); const double bLo = ts.magnitudeAt (80.0);
            ts.setControls (1.0, 0.5, 0.5); const double bHi = ts.magnitudeAt (80.0);
            check (bHi > bLo * 1.5, tag + ": bass knob raises 80 Hz");
            ts.setControls (0.5, 0.5, 0.0); const double tLo = ts.magnitudeAt (5000.0);
            ts.setControls (0.5, 0.5, 1.0); const double tHi = ts.magnitudeAt (5000.0);
            check (tHi > tLo * 1.3, tag + ": treble knob raises 5 kHz");
            ts.setControls (0.5, 0.0, 0.5); const double mLo = ts.magnitudeAt (500.0);
            ts.setControls (0.5, 1.0, 0.5); const double mHi = ts.magnitudeAt (500.0);
            check (mHi > mLo * 1.1, tag + ": middle knob raises 500 Hz");
        }
    });

    test ("Triode load-line tables match real 12AX7 behaviour", []
    {
        const auto& std12 = TriodeTable::get (TriodeCircuit::Standard);
        check (std12.quiescentPlate() > 150.0 && std12.quiescentPlate() < 240.0, "standard stage idles 150-240 V on the plate");
        check (std12.smallSignalGain() < -45.0 && std12.smallSignalGain() > -80.0, "standard stage gain ~ -60 (inverting)");
        check (std::fabs (TriodeTable::get (TriodeCircuit::Cold).smallSignalGain()) < 15.0, "cold unbypassed stage has low gain");
        // Asymmetry: cut-off (negative grid) swing is larger than the saturation swing
        check (std12.lookup (-10.0f) > std::fabs (std12.lookup (0.9f)), "positive plate swing (cut-off) exceeds negative swing");
        float prev = std12.lookup (-24.0f);
        bool monotonic = true;
        for (float v = -23.9f; v <= 3.9f; v += 0.1f) { const float y = std12.lookup (v); if (y > prev + 1e-3f) monotonic = false; prev = y; }
        check (monotonic, "transfer curve is monotonic (decreasing)");
    });

    test ("Grid conduction shifts bias on loud transients and recovers", []
    {
        StageSpec spec; spec.recoveryMs = 50.0f;
        TriodeStageModel stage;
        stage.prepare (48000.0, spec);
        const auto burst = sine (200.0, 48000.0, 4800, 8.0f); // hard-driven grid
        for (float x : burst) stage.process (x);
        const float shifted = stage.getBiasShift();
        for (int i = 0; i < 48000; ++i) stage.process (0.0f);
        check (shifted > 0.5f, "loud burst charges the coupling cap (bias shift)");
        check (stage.getBiasShift() < shifted * 0.05f, "bias recovers within a second");
    });

    test ("Every model and channel is finite and bounded at extreme settings", [&]
    {
        for (const auto& v : allVariants())
        {
            AmpSettings s; s.model = v.model; s.channel = v.channel;
            s.gain = 10; s.master = 10; s.bass = 10; s.treble = 10; s.middle = 10; s.presence = 10; s.resonance = 10;
            s.boost = s.bright = s.deep = true;
            const auto out = runAmp (s, sine (110.0, fs, 38400, 1.0f), fs);
            double peak = 0.0;
            for (float x : out) peak = std::fmax (peak, std::fabs (x));
            check (allFinite (out), "finite: " + v.label);
            check (peak < 12.0, "bounded: " + v.label + " (peak " + std::to_string (peak) + ")");
        }
        for (int t = 0; t < static_cast<int> (PowerTube::count); ++t)
        {
            AmpSettings s; s.tubeSwap = t; s.master = 10; s.gain = 10;
            const auto out = runAmp (s, sine (110.0, fs, 38400, 1.0f), fs);
            check (allFinite (out), std::string ("finite with tube swap ") + getPowerTubeProfile (static_cast<PowerTube> (t)).name);
        }
    });

    test ("Gain knob sweeps every tube channel from cleaner to dirtier", [&]
    {
        // Realistic picking level (~0.2 V peak). Amps designed to stay clean (big-headroom
        // cleans) must still respond audibly to the gain knob with a level change.
        for (const auto& v : allVariants())
        {
            if (M::getAmpModel (v.model).power.solidState) continue;
            auto runAt = [&] (float g)
            {
                AmpSettings s; s.model = v.model; s.channel = v.channel; s.gain = g;
                return runAmp (s, sine (196.0, fs, 76800, 0.1f), fs);
            };
            const auto o1 = runAt (1.0f), o9 = runAt (9.0f);
            const double lo = thdOf (o1, 196.0, fs), hi = thdOf (o9, 196.0, fs);
            if (hi < 0.05)
            {
                const double db = 20.0 * std::log10 (rms (o9, o9.size() / 2) / std::fmax (1e-9, rms (o1, o1.size() / 2)));
                check (db > 3.0, v.label + ": clean-headroom amp, gain changes level (" + std::to_string (db) + " dB)");
            }
            else
            {
                check (hi > lo * 1.5, v.label + ": THD rises with gain (" + std::to_string (lo) + " -> " + std::to_string (hi) + ")");
            }
        }
    });

    test ("Clean amps stay clean at moderate settings", [&]
    {
        for (auto id : { M::AmpModelId::Canyon100, M::AmpModelId::GlassClean120 })
        {
            AmpSettings s; s.model = static_cast<int> (id); s.gain = 5.0f;
            const double thd = thdOf (runAmp (s, sine (196.0, fs, 76800, 0.25f), fs), 196.0, fs);
            check (thd < 0.05, std::string (M::getAmpModel (s.model).name) + " THD at gain 5 < 5% (got " + std::to_string (thd) + ")");
        }
    });

    test ("Guitar volume roll-back cleans up the tone", [&]
    {
        AmpSettings s; s.model = static_cast<int> (M::AmpModelId::Plate59Lead); s.gain = 7.0f;
        const double full = thdOf (runAmp (s, sine (196.0, fs, 76800, 0.5f), fs), 196.0, fs);
        const double rolled = thdOf (runAmp (s, sine (196.0, fs, 76800, 0.06f), fs), 196.0, fs);
        std::printf ("    THD full volume: %.4f   rolled back: %.4f\n", full, rolled);
        check (rolled < full * 0.5, "lower input level gives a cleaner tone");
    });

    test ("Models are level-matched (within 4 dB at reference settings)", [&]
    {
        const auto in = pluck (110.0, fs, 0.6, 0.18f);
        for (const auto& v : allVariants())
        {
            AmpSettings s; s.model = v.model; s.channel = v.channel;
            const auto out = runAmp (s, in, fs);
            const double db = 20.0 * std::log10 (rms (out, out.size() / 4) / 0.12);
            check (std::fabs (db) < 4.0, v.label + " level " + std::to_string (db) + " dB");
        }
    });

    test ("Every model and channel sounds distinct", [&]
    {
        // Two fingerprints per channel voicing:
        //  * tone   - harmonic spectrum shape of a string-like pluck at three probe
        //             settings (low gain, high gain, cranked master);
        //  * breakup - distortion (THD, dB) at five gain settings with a typical
        //             picking level, i.e. where and how the amp breaks up.
        // A pair counts as distinct when either differs audibly:
        // >= 1 dB average spectral difference, or >= 3 dB average breakup difference.
        const auto in = richPluck (98.0, fs, 0.4, 0.2f);
        struct Probe { float gain, master; };
        const Probe probes[] = { { 3.0f, 5.0f }, { 8.0f, 5.0f }, { 5.0f, 9.0f } };
        const auto variants = allVariants();
        std::vector<std::vector<double>> tone, breakup;
        for (const auto& v : variants)
        {
            std::vector<double> p;
            for (const auto& pr : probes)
            {
                AmpSettings s; s.model = v.model; s.channel = v.channel; s.gain = pr.gain; s.master = pr.master;
                const auto out = runAmp (s, in, fs);
                std::vector<double> h;
                for (int k = 1; k <= 20; ++k) h.push_back (20.0 * std::log10 (toneLevel (out, 98.0 * k, fs, out.size() / 5) + 1e-7));
                for (auto& x : h) p.push_back (x - h[0]); // level-independent spectral shape
            }
            tone.push_back (p);

            std::vector<double> b;
            for (float g : { 1.0f, 3.0f, 5.0f, 7.0f, 9.0f })
            {
                AmpSettings s; s.model = v.model; s.channel = v.channel; s.gain = g;
                b.push_back (20.0 * std::log10 (thdOf (runAmp (s, sine (196.0, fs, 38400, 0.1f), fs), 196.0, fs) + 1e-4));
            }
            breakup.push_back (b);
        }
        auto meanDiff = [] (const std::vector<double>& x, const std::vector<double>& y)
        {
            double d = 0.0;
            for (size_t k = 0; k < x.size(); ++k) d += std::fabs (x[k] - y[k]);
            return d / static_cast<double> (x.size());
        };
        std::vector<std::vector<double>> score (variants.size(), std::vector<double> (variants.size(), 1e9));
        for (size_t a = 0; a < variants.size(); ++a)
            for (size_t b = a + 1; b < variants.size(); ++b)
            {
                const double dt = meanDiff (tone[a], tone[b]), db = meanDiff (breakup[a], breakup[b]);
                score[a][b] = score[b][a] = std::fmax (dt / 1.0, db / 3.0); // >= 1 means audibly distinct
            }

        // 1) Channels of the same amp must all differ (the channel switch always matters).
        for (size_t a = 0; a < variants.size(); ++a)
            for (size_t b = a + 1; b < variants.size(); ++b)
                if (variants[a].model == variants[b].model)
                    check (score[a][b] >= 1.0, "channels differ: " + variants[a].label + " vs " + variants[b].label
                                                   + " (score " + std::to_string (score[a][b]) + ")");

        // 2) Every model differs from every other model: at least one of its channels has no
        //    near-twin among the other model's channels. (A channel may deliberately emulate
        //    another amp, e.g. a "IIC+ mode", as on the real hardware.)
        int nearTwinChannels = 0;
        for (size_t a = 0; a < variants.size(); ++a)
            for (size_t b = a + 1; b < variants.size(); ++b)
                if (variants[a].model != variants[b].model && score[a][b] < 1.0) ++nearTwinChannels;

        for (int ma = 0; ma < M::numAmpModels(); ++ma)
            for (int mb = ma + 1; mb < M::numAmpModels(); ++mb)
            {
                bool distinct = false;
                for (size_t a = 0; a < variants.size() && ! distinct; ++a)
                {
                    if (variants[a].model != ma && variants[a].model != mb) continue;
                    const int other = variants[a].model == ma ? mb : ma;
                    double best = 1e9;
                    for (size_t b = 0; b < variants.size(); ++b)
                        if (variants[b].model == other) best = std::fmin (best, score[a][b]);
                    if (best >= 1.0) distinct = true;
                }
                check (distinct, std::string ("models differ: ") + M::getAmpModel (ma).name + " vs " + M::getAmpModel (mb).name);
            }
        std::printf ("    %zu channel voicings, %d models; %d cross-model channel pairs are near-twins (allowed when the models still differ)\n",
                     variants.size(), M::numAmpModels(), nearTwinChannels);
    });

    test ("Catalog covers the required collections", []
    {
        check (M::numAmpModels() >= 100, "at least 100 amp models (" + std::to_string (M::numAmpModels()) + ")");
        int perCategory[static_cast<int> (M::AmpCategory::count)] {};
        for (int m = 0; m < M::numAmpModels(); ++m) ++perCategory[static_cast<int> (M::getAmpModel (m).category)];
        for (int c = 0; c < static_cast<int> (M::AmpCategory::count); ++c)
            check (perCategory[c] > 0, std::string ("category has models: ") + M::categoryName (static_cast<M::AmpCategory> (c)));

        auto hasInspiration = [] (const char* text)
        {
            for (int m = 0; m < M::numAmpModels(); ++m)
                if (std::string (M::getAmpModel (m).inspiredBy).find (text) != std::string::npos) return true;
            return false;
        };
        for (const char* mk : { "Mark I", "Mark IIA", "Mark IIB", "Mark IIC", "Mark IIC+", "Mark III", "Mark IV", "Mark V", "Mark VII",
                                "Dual Rectifier", "Triple Rectifier", "Single Rectifier", "Road King", "Roadster" })
            check (hasInspiration (mk), std::string ("Mesa collection includes ") + mk);
        for (const char* mm : { "JTM45", "1959 Super Lead", "1987", "JMP 2203", "JMP 2204", "JCM800 2203", "JCM800 2204", "JCM900",
                                "JCM2000", "JVM410H", "Silver Jubilee", "Vintage Modern", "neo-classical", "Jose Arredondo",
                                "early-1970s Marshall", "mid-1970s Marshall", "late-1970s Marshall" })
            check (hasInspiration (mm), std::string ("Marshall collection includes ") + mm);
        for (int m = 0; m < M::numAmpModels(); ++m)
        {
            const auto& spec = M::getAmpModel (m);
            if (spec.category == M::AmpCategory::MesaMarkCollection)
                check (spec.hasGEQ, std::string (spec.name) + " has the 5-band graphic EQ");
            check (spec.numChannels >= 1 && spec.numChannels <= 3, std::string (spec.name) + " channel count");
            for (int c = 0; c < spec.numChannels; ++c)
                check (spec.channels[static_cast<size_t> (c)].numStages <= 5, std::string (spec.name) + " stage count <= 5");
        }
    });

    test ("Graphic EQ shapes Mark-series tones", [&]
    {
        auto in = sine (98.0, fs, 48000, 0.2f);
        const auto hi = sine (2900.0, fs, 48000, 0.05f);
        for (size_t i = 0; i < in.size(); ++i) in[i] += hi[i];
        AmpSettings flat; flat.model = static_cast<int> (M::AmpModelId::SierraMkIICPlus); flat.channel = 1;
        AmpSettings vee = flat; vee.geq = { 6.0f, 2.0f, -8.0f, 3.0f, 6.0f };
        AmpSettings off = vee; off.geqOn = false;
        const auto a = runAmp (flat, in, fs), b = runAmp (vee, in, fs), c = runAmp (off, in, fs);
        std::vector<float> d (a.size()), e (a.size());
        for (size_t i = 0; i < a.size(); ++i) { d[i] = b[i] - a[i]; e[i] = c[i] - a[i]; }
        check (rms (d, 24000) / rms (a, 24000) > 0.05, "V-shaped GEQ audibly changes the tone");
        check (rms (e, 24000) / rms (a, 24000) < 1e-4, "GEQ switched off = flat");
        AmpSettings plexi = vee; plexi.model = static_cast<int> (M::AmpModelId::Plate59Lead); plexi.channel = 0;
        AmpSettings plexiFlat = plexi; plexiFlat.geq = {};
        const auto p1 = runAmp (plexi, in, fs), p2 = runAmp (plexiFlat, in, fs);
        std::vector<float> f (p1.size());
        for (size_t i = 0; i < p1.size(); ++i) f[i] = p1[i] - p2[i];
        check (rms (f, 24000) / rms (p2, 24000) < 1e-4, "models without a GEQ ignore it");
    });

    test ("Checked-in level calibration matches the models", []
    {
       #if VF_HAS_CALIBRATION_TABLE
        const auto measured = AmpVoice::measureCalibration();
        int stale = 0;
        for (int m = 0; m < M::numAmpModels(); ++m)
            for (int c = 0; c < M::getAmpModel (m).numChannels; ++c)
            {
                const double a = kCalibration[static_cast<size_t> (m)][static_cast<size_t> (c)];
                const double b = measured[static_cast<size_t> (m * 3 + c)];
                if (std::fabs (20.0 * std::log10 (a / b)) > 0.25) ++stale;
            }
        check (stale == 0, "AmpCalibration.inc is current (" + std::to_string (stale) + " stale entries; run tools/vf_calibrate)");
       #else
        check (false, "AmpCalibration.inc is missing (run tools/vf_calibrate)");
       #endif
    });

    test ("Every amp control changes the sound", [&]
    {
        auto in = sine (98.0, fs, 48000, 0.25f);
        const auto hi = sine (2900.0, fs, 48000, 0.05f);
        for (size_t i = 0; i < in.size(); ++i) in[i] += hi[i];
        AmpSettings base;
        const auto ref = runAmp (base, in, fs);
        auto differs = [&] (AmpSettings s, const char* what)
        {
            const auto out = runAmp (s, in, fs);
            std::vector<float> d (out.size());
            for (size_t i = 0; i < d.size(); ++i) d[i] = out[i] - ref[i];
            const double rel = rms (d, 24000) / rms (ref, 24000);
            std::printf ("    %-20s change %.3f\n", what, rel);
            check (rel > 0.02, what);
        };
        AmpSettings s;
        s = base; s.bright = true;     differs (s, "bright switch");
        s = base; s.deep = true;       differs (s, "deep switch");
        s = base; s.boost = true;      differs (s, "boost switch");
        s = base; s.channel = 1;       differs (s, "channel switch");
        s = base; s.gain = 9;          differs (s, "gain knob");
        s = base; s.bass = 9;          differs (s, "bass knob");
        s = base; s.middle = 1;        differs (s, "middle knob");
        s = base; s.treble = 9;        differs (s, "treble knob");
        s = base; s.presence = 10;     differs (s, "presence knob");
        s = base; s.resonance = 10;    differs (s, "resonance knob");
        s = base; s.master = 9;        differs (s, "master knob");
        s = base; s.tubeSwap = static_cast<int> (PowerTube::KT88); differs (s, "power tube swap");
    });

    test ("Switching models mid-stream is click-free", [&]
    {
        AmpVoice amp;
        AmpSettings a; a.model = static_cast<int> (M::AmpModelId::Canyon6V6);
        amp.setSettings (a);
        amp.prepare (fs, 512);
        auto sig = sine (147.0, fs, 512 * 188, 0.2f); // whole number of blocks
        double maxStep = 0.0, maxStepBefore = 0.0;
        float last = 0.0f;
        for (size_t i = 0; i < sig.size(); i += 512)
        {
            if (i == 48128) { AmpSettings b; b.model = static_cast<int> (M::AmpModelId::Forge82Lead); amp.setSettings (b); }
            amp.process (sig.data() + i, 512);
            for (size_t k = i; k < i + 512; ++k)
            {
                const double step = std::fabs (sig[k] - last);
                (i < 48128 ? maxStepBefore : maxStep) = std::fmax (i < 48128 ? maxStepBefore : maxStep, step);
                last = sig[k];
            }
        }
        check (amp.getSettings().model == static_cast<int> (M::AmpModelId::Forge82Lead), "new model is active after the switch");
        check (maxStep < 0.1, "no discontinuity across the switch (max step " + std::to_string (maxStep) + ")");
    });

    test ("Cabinets differ and mic controls behave like a real close mic", []
    {
        std::vector<std::vector<double>> curves;
        for (int c = 0; c < static_cast<int> (CabModel::count); ++c)
        {
            CabinetSim cab; cab.prepare (48000.0);
            cab.setParams (static_cast<CabModel> (c), 0.3f, 0.2f); cab.update();
            std::vector<double> curve;
            for (double f = 60.0; f < 9000.0; f *= 1.25) curve.push_back (20.0 * std::log10 (cab.magnitudeAt (f)));
            curves.push_back (curve);
            check (cab.magnitudeAt (10000.0) < cab.magnitudeAt (2000.0) * 0.3, std::string (cabModelNames[static_cast<size_t> (c)]) + " rolls off the fizz above 10 kHz");
        }
        for (size_t a = 0; a < curves.size(); ++a)
            for (size_t b = a + 1; b < curves.size(); ++b)
            {
                double d = 0.0;
                for (size_t k = 0; k < curves[a].size(); ++k) d += std::fabs (curves[a][k] - curves[b][k]);
                check (d / static_cast<double> (curves[a].size()) > 0.7,
                       std::string (cabModelNames[a]) + " vs " + cabModelNames[b] + " responses differ");
            }

        CabinetSim cab; cab.prepare (48000.0);
        cab.setParams (CabModel::V30_4x12, 0.0f, 0.2f); cab.update(); const double centre = cab.magnitudeAt (4500.0);
        cab.setParams (CabModel::V30_4x12, 1.0f, 0.2f); cab.update(); const double edge = cab.magnitudeAt (4500.0);
        check (centre > edge * 1.6, "mic at cone centre is brighter than at the edge");
        cab.setParams (CabModel::V30_4x12, 0.3f, 0.0f); cab.update(); const double close = cab.magnitudeAt (120.0);
        cab.setParams (CabModel::V30_4x12, 0.3f, 1.0f); cab.update(); const double far = cab.magnitudeAt (120.0);
        check (close > far * 1.4, "close mic has more low end (proximity effect)");
    });

    test ("Streaming resampler round-trips 44.1 <-> 48 kHz accurately", []
    {
        for (const auto& rates : { std::pair<double, double> { 44100.0, 48000.0 }, std::pair<double, double> { 96000.0, 48000.0 } })
        {
            StreamingResampler up, down;
            up.prepare (rates.first, rates.second, 256);
            down.prepare (rates.second, rates.first, up.maxOutputFor (256));
            SampleFifo fifo; fifo.prepare (4096); fifo.pushZeros (8);
            const auto in = sine (1000.0, rates.first, 256 * 200, 0.5f);
            std::vector<float> mid (static_cast<size_t> (up.maxOutputFor (256))), back (static_cast<size_t> (down.maxOutputFor (up.maxOutputFor (256)))), out (in.size());
            bool underrun = false;
            for (size_t b = 0; b < 200; ++b)
            {
                const int m = up.process (in.data() + b * 256, 256, mid.data(), static_cast<int> (mid.size()));
                const int k = down.process (mid.data(), m, back.data(), static_cast<int> (back.size()));
                fifo.push (back.data(), k);
                if (! fifo.pop (out.data() + b * 256, 256) && b > 2) underrun = true;
            }
            const double inLevel = toneLevel (in, 1000.0, rates.first, in.size() / 2);
            const double outLevel = toneLevel (out, 1000.0, rates.first, out.size() / 2);
            double distortion = 0.0;
            for (int k = 2; k <= 5; ++k) distortion += toneLevel (out, 1000.0 * k, rates.first, out.size() / 2);
            const std::string tag = std::to_string (static_cast<int> (rates.first)) + " Hz host: ";
            check (! underrun, tag + "no FIFO underruns after priming");
            check (std::fabs (20.0 * std::log10 (outLevel / inLevel)) < 0.2, tag + "1 kHz level preserved");
            // Measured ~-60 dB; the single-bin DFT's leakage floor is of the same order,
            // so the threshold sits a little above it (-50 dB).
            check (distortion / outLevel < 0.003, tag + "conversion adds < 0.3% (-50 dB) error (" + std::to_string (distortion / outLevel) + ")");
        }
    });

    test ("NAM post-EQ is flat at centre and each knob acts", []
    {
        ToneShaper t; t.prepare (48000.0);
        ToneShaperSettings s;
        t.update (s);
        bool flat = true;
        for (double f : { 50.0, 200.0, 700.0, 2500.0, 6000.0 }) if (std::fabs (20.0 * std::log10 (t.magnitudeAt (f))) > 0.05) flat = false;
        check (flat, "all knobs at 5 = flat");
        auto at = [&] (ToneShaperSettings x, double f) { t.update (x); return 20.0 * std::log10 (t.magnitudeAt (f)); };
        ToneShaperSettings x;
        x = s; x.bass = 10;      check (at (x, 60.0) > 6.0, "bass raises lows");
        x = s; x.middle = 0;     check (at (x, 700.0) < -6.0, "middle cuts mids");
        x = s; x.treble = 10;    check (at (x, 6000.0) > 6.0, "treble raises highs");
        x = s; x.presence = 10;  check (at (x, 4500.0) > 5.0, "presence boosts upper mids");
        x = s; x.resonance = 10; check (at (x, 95.0) > 4.0, "resonance boosts low thump");
        x = s; x.bright = true;  check (at (x, 6000.0) > 3.0, "bright lifts highs");
        x = s; x.deep = true;    check (at (x, 60.0) > 3.0, "deep lifts lows");
    });

    test ("Noise gate closes on hiss and opens on a note", []
    {
        NoiseGate g;
        g.prepare (48000.0);
        g.setThresholdDb (-50.0f);
        std::vector<float> hiss (48000);
        unsigned seed = 1;
        for (auto& x : hiss) { seed = seed * 1664525u + 1013904223u; x = ((seed >> 9) / 8388608.0f - 1.0f) * 0.0005f; }
        for (auto& x : hiss) x = g.process (x);
        check (g.getGain() < 0.01f, "gate closed on -66 dBFS hiss");
        const auto note = sine (82.4, 48000.0, 4800, 0.3f);
        for (float x : note) g.process (x);
        check (g.getGain() > 0.99f, "gate open on a played note");
    });

    test ("Tuner detects standard and drop tunings", []
    {
        struct Case { double freq; const char* name; int octave; };
        for (const Case& c : { Case { 82.41, "E", 2 }, Case { 110.0, "A", 2 }, Case { 61.74, "B", 1 },
                               Case { 329.63, "E", 4 }, Case { 73.42, "D", 2 }, Case { 440.0 * std::pow (2.0, 13.0 / 1200.0), "A", 4 } })
        {
            TunerAnalyser t;
            t.prepare (48000.0);
            // Guitar-like signal: fundamental + harmonics
            std::vector<float> sig (48000 / 2);
            for (size_t i = 0; i < sig.size(); ++i)
            {
                const double ph = 2.0 * kPi * c.freq * static_cast<double> (i) / 48000.0;
                sig[i] = static_cast<float> (0.3 * std::sin (ph) + 0.2 * std::sin (2 * ph) + 0.1 * std::sin (3 * ph));
            }
            t.pushSamples (sig.data(), static_cast<int> (sig.size()));
            const auto r = t.analyse();
            const double expectedCents = 1200.0 * std::log2 (c.freq / (440.0 * std::pow (2.0, static_cast<double> (std::lround (69 + 12 * std::log2 (c.freq / 440.0)) - 69) / 12.0)));
            check (r.valid, std::string ("reading valid for ") + c.name);
            check (std::string (r.noteName()) == c.name && r.octave() == c.octave,
                   std::string ("note name for ") + c.name + " got " + r.noteName() + std::to_string (r.octave()));
            check (std::fabs (r.cents - expectedCents) < 2.0,
                   std::string ("cents accuracy for ") + c.name + " (got " + std::to_string (r.cents) + ")");
        }
    });

    test ("Tuner is safe before prepare() and across rate changes", []
    {
        TunerAnalyser t;               // editor may poll before the host prepares audio
        check (! t.analyse().valid, "analyse before prepare returns no reading");
        t.prepare (96000.0);
        const auto sig = sine (110.0, 96000.0, 48000, 0.3f);
        t.pushSamples (sig.data(), static_cast<int> (sig.size()));
        const auto r = t.analyse();
        check (r.valid && std::string (r.noteName()) == "A", "reads A2 after switching to 96 kHz");
    });

    test ("Tuner ignores silence", []
    {
        TunerAnalyser t;
        t.prepare (48000.0);
        std::vector<float> silence (24000, 0.0f);
        t.pushSamples (silence.data(), static_cast<int> (silence.size()));
        check (! t.analyse().valid, "no reading on silence");
    });

    test ("Parameter specs are well-formed", []
    {
        for (const auto& p : vf::parameterSpecs)
        {
            check (p.min < p.max, std::string (p.id) + " range");
            check (p.defaultValue >= p.min && p.defaultValue <= p.max, std::string (p.id) + " default in range");
            for (const auto& q : vf::parameterSpecs)
                if (&p != &q) check (std::string (p.id) != q.id, std::string ("unique id ") + p.id);
        }
    });

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
