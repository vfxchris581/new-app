// Viridian Forge — core DSP tests (no external test framework needed).
// Run: ctest --test-dir build  (or ./build/tests/vf_core_tests)

#include "vf/Parameters.h"
#include "vf/dsp/AmpVoice.h"
#include "vf/dsp/Cabinet.h"
#include "vf/dsp/NoiseGate.h"
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
        for (int m = 0; m < M::numAmpModels(); ++m) stacks.push_back (M::getAmpModel (m).stack);
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

    test ("Gain knob raises distortion on every tube model", [&]
    {
        for (const auto& v : allVariants())
        {
            if (M::getAmpModel (v.model).power.solidState) continue;
            auto thdAt = [&] (float g)
            {
                AmpSettings s; s.model = v.model; s.channel = v.channel; s.gain = g;
                return thdOf (runAmp (s, sine (196.0, fs, 76800, 0.25f), fs), 196.0, fs);
            };
            const double lo = thdAt (1.0f), hi = thdAt (9.0f);
            check (hi > lo * 2.0, v.label + ": THD rises with gain (" + std::to_string (lo) + " -> " + std::to_string (hi) + ")");
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
        // Compare spectral envelopes (harmonic levels) of a pluck through each variant.
        const auto in = pluck (98.0, fs, 0.5, 0.2f);
        std::vector<std::vector<double>> prints;
        const auto variants = allVariants();
        for (const auto& v : variants)
        {
            AmpSettings s; s.model = v.model; s.channel = v.channel;
            const auto out = runAmp (s, in, fs);
            std::vector<double> p;
            for (int k = 1; k <= 30; ++k) p.push_back (20.0 * std::log10 (toneLevel (out, 98.0 * k, fs, out.size() / 5) + 1e-7));
            prints.push_back (p);
        }
        double minDist = 1e9;
        for (size_t a = 0; a < prints.size(); ++a)
            for (size_t b = a + 1; b < prints.size(); ++b)
            {
                double d = 0.0;
                for (size_t k = 0; k < prints[a].size(); ++k) d += std::fabs (prints[a][k] - prints[b][k]);
                d /= static_cast<double> (prints[a].size());
                minDist = std::fmin (minDist, d);
                check (d > 1.0, variants[a].label + " vs " + variants[b].label + " differ (mean " + std::to_string (d) + " dB)");
            }
        std::printf ("    closest pair differs by %.2f dB per harmonic on average\n", minDist);
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
