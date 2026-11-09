// Viridian Forge — amp dynamics measurement (developer tool).
//
// Measures how an amp responds to playing, so models can be compared with
// captures of real amps (NAM files):
//   * palm-mute chugs: attack punch (onset peak vs body), chug decay (body vs
//     the tail just before the next chug), low-end share (flub) and level;
//   * a sustained pluck: decay rate of the output envelope (dB/s);
//   * static compression slope between two input levels.
//
//   vf_dynamics nam <file.nam> [label]        measure a NAM capture (48 kHz)
//   vf_dynamics amp "<model name>" <channel> <gain> [master]
#include "vf/dsp/AmpVoice.h"
#include "vf/dsp/Biquad.h"
#include "vf/fx/Registry.h"
#if VF_WITH_NAM
 #include "vf/nam/NamEngine.h"
#endif
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using Proc = std::function<void (float*, int)>;
static constexpr double kPi = 3.14159265358979323846;

/** Palm-muted low-E chugs: harmonic-rich string with a pick click, heavily damped. */
static std::vector<float> chugs (double fs, float peak, int count, double spacing, double tau)
{
    std::vector<float> x (static_cast<size_t> ((count * spacing + 0.3) * fs));
    for (int c = 0; c < count; ++c)
    {
        const size_t start = static_cast<size_t> (c * spacing * fs);
        for (size_t i = 0; i < static_cast<size_t> (spacing * fs) && start + i < x.size(); ++i)
        {
            const double t = static_cast<double> (i) / fs;
            double v = 0.0;
            for (int h = 1; h <= 12; ++h) v += std::sin (2.0 * kPi * 82.41 * h * t + h) / (h * (1.0 + 0.15 * h * h * t * 30.0));
            v *= std::exp (-t / tau) * (1.0 - std::exp (-t / 0.0015));
            x[start + i] += static_cast<float> (v);
        }
    }
    float m = 0.0f;
    for (float v : x) m = std::max (m, std::fabs (v));
    for (auto& v : x) v *= peak / m;
    return x;
}

static std::vector<float> pluck (double fs, float peak, double seconds)
{
    std::vector<float> x (static_cast<size_t> (seconds * fs));
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = static_cast<double> (i) / fs;
        double v = 0.0;
        for (int h = 1; h <= 10; ++h) v += std::sin (2.0 * kPi * 110.0 * h * t + h) / h * std::exp (-t * (1.5 + 0.4 * h));
        x[i] = static_cast<float> (v * (1.0 - std::exp (-t / 0.002)));
    }
    float m = 0.0f;
    for (float v : x) m = std::max (m, std::fabs (v));
    for (auto& v : x) v *= peak / m;
    return x;
}

static double rmsDb (const std::vector<float>& y, double fs, double t0, double t1)
{
    double s = 0.0; size_t n = 0;
    for (size_t i = static_cast<size_t> (t0 * fs); i < std::min (y.size(), static_cast<size_t> (t1 * fs)); ++i) { s += static_cast<double> (y[i]) * y[i]; ++n; }
    return 10.0 * std::log10 (s / std::max<size_t> (1, n) + 1e-20);
}

static double peakDb (const std::vector<float>& y, double fs, double t0, double t1)
{
    float p = 0.0f;
    for (size_t i = static_cast<size_t> (t0 * fs); i < std::min (y.size(), static_cast<size_t> (t1 * fs)); ++i) p = std::max (p, std::fabs (y[i]));
    return 20.0 * std::log10 (p + 1e-12);
}

static void measure (const char* label, const Proc& proc, double fs)
{
    std::printf ("%-34s", label);
    for (float inDb : { -18.0f, -9.0f })
    {
        const float peak = std::pow (10.0f, inDb / 20.0f);
        // warm-up then 8 chugs, 8ths at ~160 bpm
        const double spacing = 0.1875;
        auto x = chugs (fs, peak, 10, spacing, 0.07);
        auto y = x;
        proc (y.data(), static_cast<int> (y.size()));
        double punch = 0, decay = 0, low = 0, level = 0;
        vf::dsp::Biquad lp; lp.setCoeffs (vf::dsp::BiquadCoeffs::lowPass (fs, 150.0, 0.707));
        std::vector<float> lows (y.size());
        for (size_t i = 0; i < y.size(); ++i) lows[i] = lp.process (y[i]);
        int n = 0;
        for (int c = 2; c < 10; ++c, ++n)
        {
            const double t = c * spacing;
            const double pk = peakDb (y, fs, t, t + 0.02), body = rmsDb (y, fs, t + 0.04, t + 0.08), tail = rmsDb (y, fs, t + 0.15, t + 0.18);
            punch += pk - body; decay += body - tail;
            low += rmsDb (lows, fs, t + 0.01, t + 0.1) - rmsDb (y, fs, t + 0.01, t + 0.1);
            level += rmsDb (y, fs, t, t + 0.12);
        }
        std::printf (" | in %3.0f: punch %5.1f decay %5.1f low %6.1f lvl %6.1f", inDb, punch / n, decay / n, low / n, level / n);
    }
    // spectrum of the chugs at -12 dBFS: octave bands relative to the total (dB)
    {
        auto x = chugs (fs, 0.25f, 10, 0.1875, 0.07);
        proc (x.data(), static_cast<int> (x.size()));
        const double total = rmsDb (x, fs, 0.375, 1.875);
        std::printf ("\n%34s spectrum:", "");
        for (double fc : { 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0 })
        {
            vf::dsp::Biquad a, b;
            a.setCoeffs (vf::dsp::BiquadCoeffs::highPass (fs, fc / 1.414, 0.707));
            b.setCoeffs (vf::dsp::BiquadCoeffs::lowPass (fs, fc * 1.414, 0.707));
            std::vector<float> band (x.size());
            for (size_t i = 0; i < x.size(); ++i) band[i] = b.process (a.process (x[i]));
            std::printf (" %4.0f:%5.1f", fc, rmsDb (band, fs, 0.375, 1.875) - total);
        }
        std::printf ("\n%34s", "");
    }
    // sustain: pluck at -12 dBFS, output decay between 0.2 and 1.2 s (input decays ~16 dB/s)
    auto p = pluck (fs, 0.25f, 1.6);
    proc (p.data(), static_cast<int> (p.size()));
    const double d = (rmsDb (p, fs, 0.15, 0.25) - rmsDb (p, fs, 1.1, 1.2)) / 0.95;
    std::printf (" | sustain decay %5.1f dB/s\n", d);
}


/** Pedal fingerprint: small-signal EQ (re 1 kHz), small-signal gain, THD vs level, even/odd balance. */
static void measurePedal (const char* label, const Proc& proc, double fs)
{
    auto tone = [&] (double f, float amp, double seconds)
    {
        std::vector<float> x (static_cast<size_t> (seconds * fs));
        for (size_t i = 0; i < x.size(); ++i) x[i] = amp * static_cast<float> (std::sin (2.0 * kPi * f * static_cast<double> (i) / fs));
        proc (x.data(), static_cast<int> (x.size()));
        return x;
    };
    auto bin = [&] (const std::vector<float>& y, double f)
    {
        double re = 0, im = 0; size_t a = y.size() / 2;
        for (size_t i = a; i < y.size(); ++i) { const double ph = 2.0 * kPi * f * static_cast<double> (i) / fs; re += y[i] * std::cos (ph); im += y[i] * std::sin (ph); }
        return 2.0 * std::sqrt (re * re + im * im) / static_cast<double> (y.size() - a);
    };
    const float small = std::pow (10.0f, -50.0f / 20.0f);
    const double ref = bin (tone (1000.0, small, 0.3), 1000.0);
    std::printf ("%-28s gain %5.1f dB | EQ:", label, 20.0 * std::log10 (ref / small));
    for (double f : { 100.0, 200.0, 400.0, 700.0, 2000.0, 4000.0, 8000.0 })
        std::printf (" %g:%5.1f", f, 20.0 * std::log10 (bin (tone (f, small, 0.3), f) / ref));
    std::printf (" | THD@220:");
    for (float db : { -40.0f, -30.0f, -20.0f, -10.0f })
    {
        auto y = tone (220.0, std::pow (10.0f, db / 20.0f), 0.4);
        const double f1 = bin (y, 220.0);
        double h = 0, ev = 0, od = 0;
        for (int k = 2; k <= 9; ++k) { const double a = bin (y, 220.0 * k); h += a * a; (k % 2 ? od : ev) += a * a; }
        std::printf (" %3.0f:%5.1f%%", db, 100.0 * std::sqrt (h) / f1);
        if (db == -20.0f) std::printf ("(e/o %4.1f dB, out %5.1f)", 10.0 * std::log10 ((ev + 1e-20) / (od + 1e-20)), 20.0 * std::log10 (f1 + 1e-12));
    }
    std::printf ("\n");
}

int main (int argc, char** argv)
{
    if (argc < 3) { std::printf ("usage: vf_dynamics nam <file> [label] | amp <model> <channel> <gain> [master]\n"); return 2; }
    const std::string mode = argv[1];
#if VF_WITH_NAM
    if (mode == "nam")
    {
        auto r = vf::nam::loadModel (argv[2], 48000.0, 4096);
        if (r.model == nullptr) { std::printf ("load failed: %s\n", r.error.c_str()); return 1; }
        vf::nam::Engine e;
        e.prepare (48000.0, 4096);
        e.setPending (std::move (r));
        std::vector<float> warm (4096, 0.0f);
        for (int i = 0; i < 4; ++i) e.process (warm.data(), 4096);
        Proc proc = [&e] (float* s, int n) { for (int i = 0; i < n; i += 4096) e.process (s + i, std::min (4096, n - i)); };
        if (argc > 4 && std::string (argv[4]) == "pedal") measurePedal (argv[3], proc, 48000.0);
        else measure (argc > 3 ? argv[3] : argv[2], proc, 48000.0);
        return 0;
    }
#endif
    if (mode == "fx")
    {
        // vf_dynamics fx <type-id> Name=value ...
        const int t = vf::fx::findEffectType (argv[2]);
        if (t < 0) { std::printf ("unknown effect %s\n", argv[2]); return 1; }
        auto fx = vf::fx::effectTypes()[static_cast<size_t> (t)].create();
        fx->prepare (48000.0, 4096);
        std::string label = argv[2];
        for (int a = 3; a < argc; ++a)
        {
            std::string kv = argv[a];
            const auto eq = kv.find ('=');
            for (int p = 0; p < fx->params().count; ++p)
                if (kv.substr (0, eq) == fx->params().p[static_cast<size_t> (p)].name) fx->setParam (p, std::stof (kv.substr (eq + 1)));
            label += " " + kv;
        }
        std::vector<float> r (4096);
        Proc proc = [&] (float* x, int n)
        {
            fx->reset();
            for (int i = 0; i < n; i += 4096)
            {
                const int m = std::min (4096, n - i);
                std::copy (x + i, x + i + m, r.begin());
                fx->process (x + i, r.data(), m);
            }
        };
        measurePedal (label.c_str(), proc, 48000.0);
        return 0;
    }
    if (mode == "amp")
    {
        int model = -1;
        for (int m = 0; m < vf::models::numAmpModels(); ++m)
            if (std::strcmp (vf::models::getAmpModel (m).name, argv[2]) == 0) model = m;
        if (model < 0) { std::printf ("unknown model %s\n", argv[2]); return 1; }
        // run the voice at 4x like the plugin; feed/measure at 48 kHz via zero-order hold + averaging
        auto amp = std::make_shared<vf::dsp::AmpVoice>();
        amp->prepare (192000.0, 4096 * 4);
        vf::dsp::AmpSettings s;
        s.model = model; s.channel = std::atoi (argv[3]); s.gain = static_cast<float> (std::atof (argv[4]));
        if (argc > 5) s.master = static_cast<float> (std::atof (argv[5]));
        amp->setSettings (s);
        std::vector<float> up (4096 * 4);
        vf::dsp::Biquad aa1, aa2, d1, d2;
        aa1.setCoeffs (vf::dsp::BiquadCoeffs::lowPass (192000.0, 20000.0, 0.54)); aa2.setCoeffs (vf::dsp::BiquadCoeffs::lowPass (192000.0, 20000.0, 1.31));
        d1.setCoeffs (vf::dsp::BiquadCoeffs::lowPass (192000.0, 20000.0, 0.54)); d2.setCoeffs (vf::dsp::BiquadCoeffs::lowPass (192000.0, 20000.0, 1.31));
        Proc proc = [&] (float* x, int n)
        {
            for (int i0 = 0; i0 < n; i0 += 4096)
            {
                const int m = std::min (4096, n - i0);
                for (int i = 0; i < m * 4; ++i) up[static_cast<size_t> (i)] = aa2.process (aa1.process (i % 4 == 0 ? 4.0f * x[i0 + i / 4] : 0.0f));
                amp->process (up.data(), m * 4);
                for (int i = 0; i < m * 4; ++i) up[static_cast<size_t> (i)] = d2.process (d1.process (up[static_cast<size_t> (i)]));
                for (int i = 0; i < m; ++i) x[i0 + i] = up[static_cast<size_t> (i * 4)];
            }
        };
        std::vector<float> warm (48000, 0.0f);
        proc (warm.data(), 48000);
        std::string label = std::string (argv[2]) + " ch" + argv[3] + " g" + argv[4];
        measure (label.c_str(), proc, 48000.0);
        return 0;
    }
    return 2;
}
