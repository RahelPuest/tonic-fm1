#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Per-sound figures for a render of tests/host/sounds.txt (2.46 s a sound): peak and RMS level, DC, clipping,
spectral centroid and the share of energy above 5 kHz, attack time to -3 dB of the peak, and the level 1 s after the
first note (sustain / decay).   tools/sound_report.py build/out/sounds.wav [names.txt]"""
import array
import math
import re
import sys
import wave
from pathlib import Path

wav = sys.argv[1]
names = re.findall(r'^PRESET\(\w+, "([^"]+)"', (Path(__file__).parent.parent / "app" / "presets.inc").read_text(), re.M)
w = wave.open(wav)
sr = w.getframerate()
a = array.array("h", w.readframes(w.getnframes()))
L = [x / 32768 for x in a[0::2]]
seg = int(2.468 * sr)                    # 2.46 s of waits + ~1 ms for each other command (measured)
start = int(0.06 * sr)                   # (the WAV starts at the script's wav command)
print(f"{'#':>2} {'sound':14s} {'peak':>7s} {'rms':>7s} {'DC':>6s} {'clip':>4s} {'cent':>6s} {'>5k':>5s} {'atk':>6s} {'@1s':>7s}")
for i, n in enumerate(names):
    s = L[start + i * seg: start + i * seg + int(2.3 * sr)]
    if not s:
        break
    pk = max(abs(x) for x in s) or 1e-9
    rms = math.sqrt(sum(x * x for x in s) / len(s)) or 1e-9
    dc = sum(s) / len(s)
    clip = sum(1 for x in s if abs(x) > 0.99)
    # spectrum: 8192 samples from 20 ms, Hann window
    import numpy as np
    x = np.array(s[int(0.02 * sr): int(0.02 * sr) + 8192])
    x = np.pad(x, (0, 8192 - len(x)))
    m = np.abs(np.fft.rfft(x * np.hanning(8192))) ** 2
    f = np.fft.rfftfreq(8192, 1 / sr)
    tot = m.sum() or 1e-12
    cent = float((f * m).sum() / tot)
    hi = float(m[f > 5000].sum() / tot)
    env = [max(abs(v) for v in s[k:k + 64]) for k in range(0, len(s) - 64, 64)]
    pk_env = max(env) or 1e-9
    atk = next((k * 64 / sr for k, v in enumerate(env) if v >= pk_env * 0.707), 0)
    at1 = env[int(1.0 * sr / 64)] if len(env) > int(sr / 64) else 0
    print(f"{i:2d} {n:14s} {20 * math.log10(pk):6.1f}dB {20 * math.log10(rms):6.1f}dB {dc:6.3f} {clip:4d} "
          f"{cent:6.0f} {hi:5.2f} {atk * 1000:5.0f}ms {20 * math.log10(at1 / pk_env + 1e-9):6.1f}dB")
