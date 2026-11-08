// Viridian Forge — core DSP tests (no external test framework needed).
// Run: ctest --test-dir build  (or ./build/tests/vf_core_tests)

#include "vf/Parameters.h"
#include "vf/dsp/AmpVoice.h"
#include "vf/dsp/NoiseGate.h"
#include "vf/dsp/Tuner.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace vf::dsp;

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

std::vector<float> runAmp (const AmpSettings& s, const std::vector<float>& in, double fs)
{
    AmpVoice amp;
    amp.prepare (fs, 512);
    amp.setSettings (s);
    std::vector<float> out = in;
    amp.process (out.data(), static_cast<int> (out.size()));
    return out;
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

    test ("Tone stack is passive and knobs act on the right bands", []
    {
        for (int v = 0; v < 4; ++v)
        {
            ToneStack ts;
            ts.prepare (48000.0);
            const ToneStackComponents comps[] = { ToneStackComponents::tweedBassman(), ToneStackComponents::blackface(),
                                                  ToneStackComponents::british(), ToneStackComponents::modernHiGain() };
            ts.setComponents (comps[v]);
            const std::string tag = comps[v].name;

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

            ts.setControls (0.0, 0.5, 0.5); const double bassLow = ts.magnitudeAt (80.0);
            ts.setControls (1.0, 0.5, 0.5); const double bassHigh = ts.magnitudeAt (80.0);
            check (bassHigh > bassLow * 1.5, tag + ": bass knob raises 80 Hz");

            ts.setControls (0.5, 0.5, 0.0); const double trebLow = ts.magnitudeAt (5000.0);
            ts.setControls (0.5, 0.5, 1.0); const double trebHigh = ts.magnitudeAt (5000.0);
            check (trebHigh > trebLow * 1.5, tag + ": treble knob raises 5 kHz");

            ts.setControls (0.5, 0.0, 0.5); const double midLow = ts.magnitudeAt (500.0);
            ts.setControls (0.5, 1.0, 0.5); const double midHigh = ts.magnitudeAt (500.0);
            check (midHigh > midLow * 1.2, tag + ": middle knob raises 500 Hz");
        }
    });

    test ("Amp output is finite and bounded at extreme settings", [&]
    {
        AmpSettings s;
        s.gain = 10; s.master = 10; s.bass = 10; s.treble = 10; s.middle = 10; s.presence = 10; s.resonance = 10;
        s.boost = s.bright = s.deep = true;
        s.channel = AmpChannel::Lead;
        for (int t = 0; t < static_cast<int> (PowerTube::count); ++t)
        {
            s.tube = static_cast<PowerTube> (t);
            const auto out = runAmp (s, sine (110.0, fs, 48000, 1.0f), fs);
            double peak = 0.0;
            for (float x : out) peak = std::fmax (peak, std::fabs (x));
            check (allFinite (out), std::string ("finite output, tube ") + getPowerTubeProfile (s.tube).name);
            check (peak < 8.0, std::string ("bounded output, tube ") + getPowerTubeProfile (s.tube).name);
        }
    });

    test ("Gain knob increases distortion (THD)", [&]
    {
        auto thd = [&] (float gain)
        {
            AmpSettings s; s.channel = AmpChannel::Crunch; s.gain = gain; s.cabEnabled = false;
            const auto out = runAmp (s, sine (220.0, fs, 96000, 0.3f), fs);
            const size_t start = 48000;
            const double f1 = toneLevel (out, 220.0, fs, start);
            double h = 0.0;
            for (int k = 2; k <= 7; ++k) { const double a = toneLevel (out, 220.0 * k, fs, start); h += a * a; }
            return std::sqrt (h) / f1;
        };
        const double low = thd (1.0f), high = thd (9.0f);
        std::printf ("    THD gain 1: %.4f   gain 9: %.4f\n", low, high);
        check (high > low * 3.0, "THD at gain 9 is much higher than at gain 1");
    });

    test ("Guitar volume roll-back cleans up the tone", [&]
    {
        auto thdForInput = [&] (float level)
        {
            AmpSettings s; s.channel = AmpChannel::Crunch; s.gain = 6.0f; s.cabEnabled = false;
            const auto out = runAmp (s, sine (196.0, fs, 96000, level), fs);
            const double f1 = toneLevel (out, 196.0, fs, 48000);
            double h = 0.0;
            for (int k = 2; k <= 7; ++k) { const double a = toneLevel (out, 196.0 * k, fs, 48000); h += a * a; }
            return std::sqrt (h) / f1;
        };
        const double full = thdForInput (0.5f), rolled = thdForInput (0.08f);
        std::printf ("    THD full volume: %.4f   rolled back: %.4f\n", full, rolled);
        check (rolled < full * 0.5, "lower input level gives a cleaner tone");
    });

    test ("Every channel and power tube sounds different", [&]
    {
        const auto in = sine (147.0, fs, 48000, 0.4f);
        std::vector<std::vector<float>> outs;
        for (int c = 0; c < 3; ++c)
        {
            AmpSettings s; s.channel = static_cast<AmpChannel> (c);
            outs.push_back (runAmp (s, in, fs));
        }
        for (int t = 0; t < static_cast<int> (PowerTube::count); ++t)
        {
            AmpSettings s; s.master = 8.0f; s.tube = static_cast<PowerTube> (t);
            outs.push_back (runAmp (s, in, fs));
        }
        for (size_t a = 0; a < outs.size(); ++a)
            for (size_t b = a + 1; b < outs.size(); ++b)
            {
                std::vector<float> d (outs[a].size());
                for (size_t i = 0; i < d.size(); ++i) d[i] = outs[a][i] - outs[b][i];
                const double rel = rms (d, 24000) / std::fmax (rms (outs[a], 24000), 1e-9);
                check (rel > 0.02, "variants " + std::to_string (a) + " and " + std::to_string (b) + " differ audibly");
            }
    });

    test ("Every amp switch changes the sound", [&]
    {
        // Full-range test signal: low note plus upper-harmonic content for the treble-side controls.
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
        s = base; s.cabEnabled = false; differs (s, "cab switch");
        s = base; s.presence = 10;     differs (s, "presence knob");
        s = base; s.resonance = 10;    differs (s, "resonance knob");
        s = base; s.master = 9;        differs (s, "master knob");
        s = base; s.toneStack = ToneStackVoicing::Tweed; differs (s, "tone stack voicing");
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
