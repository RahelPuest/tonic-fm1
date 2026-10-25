# Research record: the Digitone, and what TONIC takes from it

> The working notes and analysis scripts this record cites (`research/…`) are kept outside the public repository;
> this page is the summary.

Labels: **VERIFIED** read directly in the manual, the OS image's code or data, or reproduced by a script;
**INFERRED** a strong interpretation of verified facts; **APPROX** TONIC's own choice where nothing better was
found; **UNRESOLVED** open. Working notes with full detail: `research/manual/DIGITONE_REFERENCE.md` (the manual and
release notes), `research/firmware/ANALYSIS.md` (the OS container), `research/firmware/ENGINE_RE.md` (the audio
engine), `research/fm1/` (the FM-1).

## 1. Sources

| Source | Version | Provenance |
| --- | --- | --- |
| Digitone User Manual | OS 1.44 (2026-09-30), OS 1.41 (2023-11-08), OS 1.30 (2020-06-13) | elektron.se (1.44 live; 1.41 live; 1.30 Internet Archive). Hashes: `research/manual/PROVENANCE.md` |
| Release notes | OS 1.00 to 1.44 | in the OS 1.44 zip (`Digitone_and_Digitone_Keys_OS1.44_readme`) |
| OS update files | **1.41, 1.42, 1.42A, 1.43, 1.44** (one combined Digitone / Digitone Keys file each) | 1.44 from elektron.se; 1.41–1.43 from Internet Archive captures of the official elektron.se URLs (now 404). SHA-256 of each zip, .syx and decoded image: `research/firmware/PROVENANCE.md`. Versions before 1.41 were never archived |
| Secondary | Elektronauts posts by Elektron staff (ratio lists, ratio offsets), Sound On Sound review | cited per finding in `DIGITONE_REFERENCE.md` |

Digitone II material was not used. Digitone Keys shares the OS file; its keybed-only features are ignored.

## 2. What the OS files are (VERIFIED)

- A `.syx` file is a series of Elektron SysEx messages (`F0 00 20 3C 0D 00 …`): control messages and 128-byte data
  packets, each 116 bytes 7-into-8 packed (101 raw bytes) plus an unidentified check byte.
- The decoded payload is an "ELE3" image (big-endian header, build number, version, a section table). Nothing is
  encrypted. The main application and two other sections are UCL NRV2B-compressed; the decompressor was
  reimplemented from the bootstrap's own routine and verified on all 12 compressed sections of the five versions.
- The CPU is a **ColdFire V4** (big-endian 68k family; ColdFire-only MVS/MVZ and EMAC instructions); the memory map
  matches the MCF5441x family (INFERRED chip). The main application is ~2.7 MB of code. A section added in 1.43 is
  ARM Thumb-2 firmware (INFERRED: the Outbox 8 accessory, added in the same release).
- **Section 7** (126 KB, identical in 1.41–1.44) is the audio engine: about 13 KB of ColdFire/EMAC code and its
  tables (ratio lists, an exp2 table, envelope tables, the 26 HARM waveforms, level and detune curves). Its
  routines and tables are the basis of §3.
- Versions compared: 1.42 → 1.42A is a 99.4 % identical patch; 1.43 adds ~311 KB (Outbox 8, slew on RND, micro
  timing for all trigs); the audio engine section did not change in any of the five.

What cannot be recovered from the files: Elektron's source, comments or names beyond the parameter-name strings;
the update file's check byte and image checksum algorithms (not needed to read them).

## 3. Behaviour findings used in TONIC

How they were obtained: section 7 was disassembled (`research/firmware/tools/cfdis*.py`, with an EMAC decoder),
then run in an emulator (Unicorn plus a model of the EMAC unit, `dsp_emu.py`); a bit-level Python model of one voice
written from the disassembly (`fm_model.py`) matched the OS's own code on 64 of 64 random parameter sets with zero
LSB error (`validate_model.py`). The main CPU's per-voice routines (filters, amp and filter envelopes, mixer, LFO)
were read in the main application and the numeric parts emulated (`main_emu.py`, `probe_*.py`). Full detail,
addresses and the table inventory: `research/firmware/ENGINE_RE.md`.

| Behaviour | Finding | Status | In TONIC |
| --- | --- | --- | --- |
| Engine timing | 48 kHz out, operators at 96 kHz, 32-sample control blocks (1500 Hz) | VERIFIED | 44.1 kHz (the FM-1's rate), operators not oversampled; control every 32 samples (1378 Hz); all times are in seconds, so they carry over |
| Note to frequency | 440·2^((n−69)/12) through a 2049-point exp2 table | VERIFIED | equivalent |
| Ratio lists | C and B: 0.25, 0.5, 0.75, 1–16 (19); A: 0.25–5 in 0.25, 5.5–10 in 0.5, 11–16 (36) | VERIFIED | exact |
| RATIO B | one word, 2·(19·B1 + B2) | VERIFIED | B = 19·B1 + B2 |
| Ratio offsets | ±(word − 0x4000)/16384 added to the ratio (±0.999 shown) | VERIFIED | thousandths, added |
| DTUN | adds 0…+0.5 to A's ratio and 0…+0.75 to B2's, two curves, exponential to ~116 then large steps | VERIFIED (emulated) | knot curves within 0.4 % |
| HARM | 26 tables of sine partials at 1/k (saw-like, square-like, {1,5…}, bell {1,4…} families), each scaled to a peak of 1; negative HARM on C only, positive on A and B1 only; B2 always sine; the fraction crossfades tables | VERIFIED | spectra regenerated, same routing |
| Algorithms | the eight routings (render order, which outputs are enveloped, feedback operator); **algorithm 5: B1 is the feedback operator and feeds B2 and A; B2 feeds A; A feeds C** (the manual's figure reads B2→B1) | VERIFIED (trace + model) | exact |
| MIX | (K − m)·X + m·Y, K = 0.248 | VERIFIED | exact |
| Modulation index | 13.857 rad × T_lev(LEV × key scaling × \|modulator envelope\|): the envelope is applied before the level curve | VERIFIED | same (a dense copy of the curve) |
| LEV B macro | x = LEV/128: B1 = 8x/3 up to 0.375, 1.875 − 7x/3 up to 0.75, then 3.5x − 2.5; B2 = 0, 8x/3 − 1, then 1 (the manual's graph suggests breaks at 43 and 85; the code's are at 48 and 96) | VERIFIED | exact |
| Key scaling | (127 − t·KS)/128, t linear in Hz from C1 to C7 | VERIFIED | exact |
| Feedback | β = 4·FDBK/128 rad on the previous sample of the operator's enveloped output, no averaging | VERIFIED | exact |
| Operator envelopes | per block; attack linear over N/1500 s (20 s at 127); decay exponential towards END, −30 dB in the table's time (30 s at 127); ATK, DEC, delay latched at note-on; delay DEL/32 beats; TRIG on: one-shot, off: held until note-off; RESET on: restart, off: continue | VERIFIED | knot curves within 2 % |
| Phase reset | OFF, ALL, C, A+B1+B2, A+B2; a reset also clears the filter | VERIFIED | same |
| Drive | y = ½·f(f(f(2g·x))), f(u) = u(2 − \|u\|), g = 0.1535 + 0.8465·min(2(DRV/128)², 0.992); DRV 0 bypasses | VERIFIED | exact (at 44.1 kHz, no oversampling) |
| Base-width | one-pole LP at BASE + WIDTH, then one-pole HP at BASE, 5 Hz·2^(v/9.8) | VERIFIED | knot curve within 1 % |
| Cutoff | 5 Hz·2^(c/10.5756), c = FREQ + 2·(ENV − 64)·\|env\| + 0.8813·KT/100 per semitone from note 48 | VERIFIED (LP4 table exact; LP2 poles within 3.4 cents) | exact |
| LP2 / HP | 2-pole state-space filter; HP = x/4 − LP2; Q 0.63 / 1.19 / 3.29 / 8.65 / 21.7 at RESO 16 / 32 / 64 / 96 / 127 | VERIFIED (emulated coefficients) | TONIC's 2-pole SVF with a fit to those Q values (within ~10 %) |
| LP4 | 4 bilinear one-poles at fc, feedback 4r with r = 0.875·(RESO/128)², input ×(1 + 4r), a tanh-like curve in the loop; peak ×8 at RESO 127 | constants VERIFIED, structure INFERRED | same constants; TONIC's zero-delay solution gives ×7.9 |
| Filter envelope | per block; attack uses the amp attack table once a block, so it is 32× slower (ATK 32 = 1.5 s); decay/release exponential (t60 0.36 s at 32, 1.8 s at 64); delay table; always restarts from 0 | VERIFIED (intent unknown) | same |
| Amp envelope | per sample; linear attack (48 ms at 32, 30 s at 127); exponential decay/release (−60 dB in 1.0 s at 32, 5.5 s at 64, 127 = ∞); AENR: 32-sample fade, then the attack | VERIFIED | same (knot points) |
| Volume, velocity | (VOL/128)² × velocity curve: OFF 1, LIN v/127, EXP (e^{4.86x} − 1)/(e^{4.86} − 1), LOG its mirror | VERIFIED (the curves fit the OS's tables within 1.4·10⁻⁵) | exact |
| Pan | p = (PAN − 1)/252; gains 1 − 4p² and 4p(1 − p) (0.75 each at the centre) | VERIFIED curve, sides INFERRED | same |
| Sends, track level, pattern volume | sends (s/128)²; track level (TL/127)²; pattern volume 1.6384·(PVOL/128)², unity at 100 | VERIFIED (track level's identification INFERRED) | same |
| Smoothing | every voice parameter one-pole, 0.03 a block (τ ≈ 22 ms); not reset at a note-on, set at once for another sound and for parameters locked on this or the previous trig | VERIFIED | same; a silent voice starts on its values (INFERRED) |
| LFO | SPD·2^MULT/512 cycles a beat (MULT 12–23: at 120 BPM); waves TRI, SIN (parabolic), SQR, SAW (1 − 2p), EXP 2^(−12p), RMP (2p for p < ½, then 0), RND (new value each cycle, slew by SPH); fade (linear, full scale in) 3.0 ms·2^(−F/4) for F < 0 (−1: 3.6 ms, −64: 199 s) and 199 s·2^(−F/4) for F > 0 (fade-out); output 2·DEP units of the destination (pitch: semitones), clamped | VERIFIED | same |
| Defaults | INIT and effect defaults from the parameter descriptors (e.g. amp 8/32/96/24, VOL 110, operator DEC 32 END 127, PHRT ALL, LFO SPD 32 MULT 32) | VERIFIED | same |
| Trig LEN | 128 values: 0.125–1 step in 1/16, then 16 per doubling to 128, INF; shown as .125, 1/64, 1.06, 16.0, 100 … | VERIFIED (string list) | exact, same display |
| Trig conditions | 1 %…99 % (21 steps), 100 %, FILL, PRE, NEI, 1ST (each negated), 1:2…8:8 | VERIFIED | same list and order |
| LFO speed table, sequencer rules, swing 50–80 %, micro timing ±23/384 | as the manual | VERIFIED (manual; ranges also in the descriptors) | as described |

## 4. Unresolved

| Question | What is known | TONIC's choice |
| --- | --- | --- |
| Chorus, delay, reverb algorithms and parameter curves | entry points in the main application found (`ENGINE_RE.md` §5.4), not analysed | TONIC's own effects (APPROX) with the OS's parameter ranges and defaults |
| Master overdrive curve | an oversampled waveshaper, curve not decoded | a soft clipper (APPROX) |
| LP2 / HP coefficient formula | coefficients emulated and tabulated; closed form not derived | TONIC's SVF fitted to the measured Q |
| LP4 loop equations | constants and structure; the exact zero-delay solution and its correction table (128 × 128) not reproduced | TONIC's solution, same constants |
| Which pan gain is left | the curve is known; the channel order is inferred (PAN 0 = left) | as inferred |
| Velocity / mod wheel / aftertouch / breath depth scaling | the routing structure is known, the mapping not tabulated | TONIC's (depth × parameter span) |
| Portamento time curve, unison spread, voice stealing details | not analysed | TONIC's |
| Whether the 32× slower filter-envelope attack is intended | the code does it | follows the code |
| Exact EMAC rounding at the LSB | the model truncates as the emulator does; silicon behaviour not proven | irrelevant at TONIC's float precision |
| Operator oversampling | the OS runs operators at 96 kHz and decimates; TONIC runs them at 44.1 kHz | more aliasing at high ratios and index (the FM-1's CPU budget) |
| Digitone II | not examined | not a target |
