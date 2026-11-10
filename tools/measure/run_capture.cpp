// Runs a .nam capture over a log sine sweep and writes the mono output as raw float32.
#include <cstdio>
#include <cmath>
#include <vector>
#include <memory>
#include "NAM/get_dsp.h"

int main (int argc, char** argv)
{
    if (argc < 3) return 1;
    auto model = nam::get_dsp (std::filesystem::path (argv[1]));
    const double fs = 48000.0;
    const int block = 64;
    model->Reset (fs, block);
    // Log sweep 20 Hz - 20 kHz, 4 s, at -18 dBFS
    const int n = static_cast<int> (4.0 * fs);
    std::vector<NAM_SAMPLE> in (n), out (n);
    std::vector<float> inF (n), outF (n);
    const double f0 = 20.0, f1 = 20000.0, T = n / fs, k = std::log (f1 / f0) / T;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs;
        const double phase = 2.0 * M_PI * f0 * (std::exp (k * t) - 1.0) / k;
        in[i] = 0.126 * std::sin (phase);
    }
    for (int s = 0; s < n; s += block)
    {
        const int m = std::min (block, n - s);
        NAM_SAMPLE* ip[1] = { in.data() + s };
        NAM_SAMPLE* op[1] = { out.data() + s };
        model->process (ip, op, m);
    }
    for (int i = 0; i < n; ++i) { inF[i] = static_cast<float> (in[i]); outF[i] = static_cast<float> (out[i]); }
    FILE* f = std::fopen (argv[2], "wb");
    std::fwrite (inF.data(), sizeof (float), n, f);
    std::fwrite (outF.data(), sizeof (float), n, f);
    std::fclose (f);
    return 0;
}
