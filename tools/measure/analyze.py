# Analyses a sweep run through a capture: latency, level and response in 1/3-octave bands.
# Usage: analyze.py <raw.f32> <name>
import json
import sys

import numpy as np

FS = 48000.0


def load(path):
    d = np.fromfile(path, dtype=np.float32)
    n = len(d) // 2
    return d[:n].astype(np.float64), d[n:].astype(np.float64)


def latency(x, y):
    n = len(x) + len(y)
    c = np.fft.irfft(np.fft.rfft(y, n) * np.conj(np.fft.rfft(x, n)), n)
    lag = int(np.argmax(np.abs(c)))
    return lag - n if lag > n // 2 else lag


def response(x, y):
    n = len(x)
    w = np.hanning(n)
    X = np.fft.rfft(x * w)
    Y = np.fft.rfft(y * w)
    f = np.fft.rfftfreq(n, 1.0 / FS)
    H = np.abs(Y) / np.maximum(np.abs(X), 1e-12)
    centres = [50 * 2 ** (i / 3.0) for i in range(0, 25)]
    centres = [c for c in centres if c <= 12000]
    out = {}
    for c in centres:
        lo, hi = c / 2 ** (1 / 6), c * 2 ** (1 / 6)
        sel = (f >= lo) & (f < hi)
        if sel.any():
            out[int(round(c))] = float(20 * np.log10(np.mean(H[sel]) + 1e-12))
    ref = out.get(1000, out[min(out, key=lambda k: abs(k - 1000))])
    return {k: round(v - ref, 2) for k, v in out.items()}


def main():
    raw, name = sys.argv[1], sys.argv[2]
    x, y = load(raw)
    result = {
        "name": name,
        "latency_samples": latency(x, y),
        "out_peak_dbfs": round(20 * np.log10(np.max(np.abs(y)) + 1e-12), 2),
        "out_rms_dbfs": round(20 * np.log10(np.sqrt(np.mean(y ** 2)) + 1e-12), 2),
        "response_db_rel_1k": response(x, y),
    }
    print(json.dumps(result, indent=1))


if __name__ == "__main__":
    main()
