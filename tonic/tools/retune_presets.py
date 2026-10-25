#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""One-off: convert app/presets.inc from TONIC's provisional curves (before the firmware analysis) to the curves
recovered from the Digitone OS (core/tables.c): each value becomes the index that gives the same time, frequency
or modulation depth as before. Kept for the record of how the factory sounds were carried over."""
import csv, math, re, sys
from pathlib import Path
T = Path(__file__).resolve().parents[2] / "research" / "firmware" / "tables"
col = lambda f, c: [float(r[c]) for r in csv.DictReader(open(T / f))][:128]
op_atk = col("sec7_env_attack_step.csv", "attack_time_s(N/1500)")
op_dec = col("sec7_env_decay_coef.csv", "time_to_-30dB_s")
lvl = col("sec7_level_curve.csv", "value_Q31")
def pts(p, x):
    for (a, va), (b, vb) in zip(p, p[1:]):
        if x <= b:
            t = (x - a) / (b - a)
            return va * (vb / va) ** t if va > 0 and vb > 0 else va + (vb - va) * t
    return p[-1][1]
AMP = [(0, 1/48000), (1, 3/48000), (2, 6/48000), (3, 12/48000), (4, 21/48000), (5, 33/48000), (6, 48/48000),
       (7, 67/48000), (8, 90/48000), (9, 117/48000), (10, 149/48000), (16, .0092), (32, .048), (64, .262),
       (96, .714), (112, 1.5), (120, 9.0), (127, 30.0)]
FDEC = [(0, .0007), (16, .118), (32, .357), (64, 1.8), (96, 7.66), (126, 28.0)]
new = {"amp_atk": [pts(AMP, i) for i in range(128)], "amp_dec": op_dec, "flt_atk": [32 * pts(AMP, i) for i in range(128)],
       "flt_dec": [pts(FDEC, i) for i in range(128)], "op_atk": op_atk, "op_dec": op_dec}
old_env = lambda v: 0.0008 * 2 ** (v * 13.9 / 127)          # to 1 % (-40 dB)
old_op = lambda v: 0.0005 * 2 ** (v * 14.3 / 127)
def nearest(table, t):
    return min(range(128), key=lambda i: abs(math.log((table[i] + 1e-6) / (t + 1e-6))))
def conv(p, v):
    if p in ("P_EATK",): return nearest(new["amp_atk"], old_env(v))
    if p in ("P_EDEC", "P_EREL"): return 127 if (p == "P_EREL" and v >= 127) else nearest(new["amp_dec"], 0.75 * old_env(v))
    if p == "P_FATK": return nearest(new["flt_atk"], old_env(v))
    if p in ("P_FDEC", "P_FREL"): return 127 if (p == "P_FREL" and v >= 127) else nearest(new["flt_dec"], 1.5 * old_env(v))
    if p in ("P_AATK", "P_BATK"): return nearest(new["op_atk"], old_op(v))
    if p in ("P_ADEC", "P_BDEC"): return nearest(new["op_dec"], 0.75 * old_op(v))
    if p == "P_ALEV":
        x = v / 127; idx = 7.5 * x * x * (0.35 + 0.65 * x)
        return min(range(128), key=lambda i: abs(lvl[i] * 13.857 - idx))
    return v
def conv_x4(p, v):                                           # display values of the quarter-step parameters
    if p == "P_FREQ":
        hz = 27.5 * 2 ** (v / 12); return max(0, min(127, round(10.577 * math.log2(hz / 4.92))))
    if p == "P_FDBK":
        x = v / 120; return max(0, min(120, round(5 * x * x * 128 / 4)))
    if p == "P_VOL": return min(127, round(v * 1.28))
    if p == "P_FENV": return v                               # (+-64: the same scale)
    return v
def conv_bw(v):
    hz = 20 * 2 ** (v * 9.966 / 127); return max(0, min(127, round(16 * math.log(hz / 5) / math.log(3.1))))
src = Path(__file__).resolve().parents[1] / "app" / "presets.inc"
s = src.read_text()
def fix(m):
    p, val = m.group(1), m.group(2)
    if val.startswith("X4("):
        x = float(val[3:-1]); y = conv_x4(p, x)
        return f"{p}, X4({y:g})"
    if re.fullmatch(r"-?\d+", val):
        v = int(val)
        if p in ("P_BASE",): return f"{p}, {conv_bw(v)}"
        if p == "P_WDTH":                                    # (BASE + WIDTH is the lowpass: kept as a sum below)
            return f"{p}, {v}"
        return f"{p}, {conv(p, v)}"
    return m.group(0)
s2 = re.sub(r"\b(P_[A-Z0-9]+), (X4\([^)]*\)|-?\d+)", fix, s)
src.write_text(s2)
print("converted", len(re.findall(r'^PRESET', s2, re.M)), "presets")
