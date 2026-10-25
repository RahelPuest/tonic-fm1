# TONIC and the Digitone: feature comparison

Reference: the original Elektron Digitone (2018) at OS 1.44, its manual (OS 1.44, cross-checked with 1.41 and 1.30),
the release notes 1.00–1.44, and the OS images 1.41–1.44 (`docs/RESEARCH.md`). Digitone II features are not
targets.

**Implemented**: the behaviour is there and follows the reference as documented or recovered. **Approximated**: the
feature is there, but a curve, a range or a detail is TONIC's own (or only partly recovered). **Deferred**: possible on
the FM-1, not done yet. **Unavailable**: the FM-1 cannot do it.

## Synthesis

"OS" below: measured in the Digitone OS 1.44 (`docs/RESEARCH.md` §3), and TONIC follows the measurement.

| Digitone | TONIC | Status |
| --- | --- | --- |
| 8 voices, shared by the synth tracks | 12 voices shared by 4 tracks (8 before 0.11) | Implemented, extended |
| 4 operators C, A, B1, B2; 8 algorithms, two outputs X / Y, direct and enveloped outputs | the 8 routings as the OS's code runs them (algorithm 5: B1 feeds B2 and A, B2 feeds A, A feeds C) | Implemented (OS) |
| Feedback on the algorithm's feedback operator | β = 4·FDBK/128 rad on the previous enveloped sample | Implemented (OS) |
| Ratio lists C/B (19), A (36); B as one "clock-hands" knob | the same lists, B = B1 × 19 + B2 | Implemented (OS) |
| Ratio offsets −0.999…+0.999 (SYN1 page 2) | the same, added to the ratio | Implemented (OS) |
| HARM −26…+26, 26 additive shapes, interpolated | the OS's 26 spectra (sine partials 1/k, peak 1), regenerated; negative on C, positive on A and B1, B2 always sine | Implemented (OS) |
| DTUN on A and B2 | the OS's two offset curves (A to +0.5, B2 to +0.75), as knot curves within 0.4 % | Implemented (OS) |
| LEV A / LEV B (B macro to B1/B2) | index = 13.857 rad × level curve(LEV × envelope); the B macro's three segments | Implemented (OS) |
| Operator envelopes: ATK, DEC, END, delay, trigged / gated, reset | linear attack, exponential decay to END, latched at note-on, delay in 1/32 beats; knot curves within 2 % of the OS tables | Implemented (OS) |
| Phase reset OFF/ALL/C/A+B/A+B2 | the same; a reset also clears the filter | Implemented (OS) |
| Key scaling A, B1, B2 | (127 − t·KS)/128, t linear in Hz from C1 to C7 | Implemented (OS) |
| Overdrive → base-width → multimode → amp | the OS's waveshaper (½·f³(2gx), f(u) = u(2−|u|)), one-pole HP at BASE and LP at BASE+WIDTH, LP2 / HP (= x − LP2) / LP4 | Implemented (OS); TONIC runs at 44.1 kHz with no 96 kHz oversampling of the operators |
| Cutoff, resonance, LP4 | 5 Hz·2^(FREQ/10.5756); LP2 Q 0.63…21.7; LP4 4 one-poles with feedback 4·0.875·(RESO/128)², input compensation, tanh-like loop, peak ×8 at RESO 127 | Implemented (OS constants); LP2 coefficients by TONIC's own 2-pole, LP4 loop solution INFERRED |
| Filter envelope ADSR + delay, ENV ±64, key track 0–100 | ±128 cutoff units, key track 1 oct/oct at 100 from note 48; the OS's tables, attack 32× slower than the amp's (as the OS does) | Implemented (OS) |
| Amp ADSR (REL INF), AENR, drive, pan, volume, sends | the OS's tables (attack to 30 s, decay/release, 127 = ∞), (VOL/128)², parabolic pan, (send/128)² | Implemented (OS) |
| Parameter smoothing | one pole, τ 22 ms; set at once for a new sound and for locked parameters | Implemented (OS) |
| 2 LFOs per voice: SPD × MULT (synced or fixed 120 BPM), 7 waves, 5 modes, fade, start phase, RND slew (1.43) | SPD·2^MULT/512 cycles a beat; the OS's waves and fade law; depth ±2·DEP units (pitch: semitones); LFO2 can move LFO1 | Implemented (OS) |
| LFO destinations (Appendix C) | the synth list incl. the macros (PITCH, RATIO ALL, AB LEV …) | Implemented |
| Velocity, mod wheel, aftertouch, breath, pitch bend: 4 destinations each | the same | Implemented; depth scaling TONIC's (the OS's MOD SETUP mapping was not tabulated) |
| Velocity to volume OFF/LOG/LIN/EXP | the OS's curves ((e^{4.86x}−1)/(e^{4.86}−1) and its mirror) | Implemented (OS) |
| INIT sound and effect defaults | the OS's defaults (its parameter descriptors) | Implemented (OS) |
| Play modes POLY, POLY M.LFO, MONO, MONO LEG; note priority | the same | Implemented |
| Portamento: type, slope, amount, style, gating | the same | Implemented; time curve TONIC's |
| Unison (voices, spread), layers, locked voices, voice stealing, reuse | the same | Implemented; spread approximated |

## Sequencer

| Digitone | TONIC | Status |
| --- | --- | --- |
| 4 synth tracks + 4 MIDI tracks | 4 tracks, each either a synth track or a MIDI track (USB-MIDI out); synth tracks can also echo their notes | Approximated: 4 tracks in all, not 4 + 4; no CC lanes on MIDI tracks |
| 128 patterns (8 banks × 16) | 32 (2 banks × 16) | Approximated (flash: one 3.8 KB object per pattern) |
| 64 steps, per-pattern or per-track length and scale (1/8X–2X), master and change length | the same | Implemented |
| Note trigs, lock trigs, chords | the same; up to 4 notes a trig | Implemented |
| Parameter locks (up to 80 parameters a pattern) | any parameter on the sound, trig and FX pages; the limit is the pattern's flash object (refused with PATTERN FULL) | Implemented, different limit |
| Sound locks from the sound pool (128) | from a 32-sound pool | Implemented, smaller pool |
| Trig conditions: %, FILL, !FILL, PRE, !PRE, NEI, !NEI, 1ST, !1ST, A:B | all, in the OS's list order | Implemented (OS list) |
| PROB (1.30) | the same; a condition overrides it | Implemented |
| Micro timing (1/384 steps) | ±23/384 | Implemented |
| Swing 50–80 % | the same | Implemented |
| Note length 0.125–128 steps, INF | the OS's 128-value list and its display | Implemented (OS) |
| Grid, live (quantize on/off), step recording (standard) | all three | Implemented; step recording JUMP mode deferred |
| Fill (held, cued for a pass) | both | Implemented |
| Chains | yes, kept with the project | Implemented |
| Song mode (1.40): rows of pattern, repeat, length, tempo, mute; END loop/stop; 16 songs | one song of up to 32 rows: pattern, repeat, mutes; END loop/stop | Implemented in part (one song; row length and row tempo follow the pattern) |
| Copy / paste / clear: trigs, pages, tracks, patterns | the same | Implemented |
| Temporary save / reload | yes | Implemented |
| Track and pattern transpose | both | Implemented |
| Arpeggiator: mode, speed, range, length, steps with offsets | yes | Implemented |
| Keyboard scales, fold, chords | 12 scales, fold, chords | Implemented (fewer scales) |
| Metronome | a click, accented on the bar | Implemented; pre-roll deferred |
| Mute (global / pattern) | track mutes | Implemented |
| (Not on the Digitone) undo / redo, help overlay, value pop-up, fine turns, row defaults, track shift, tap tempo | SCL+HOME, a button held alone, SCL+knob, page button+OCT−, black 6/7 in grid, HOME+black 8 | Added |
| (Not on the Digitone) retrig | SEQ › RTRG: RTRG, RTIM (1/1–1/80), RVEL (fade out / in), lockable per step; the repeats over the note's length | Added |
| (Not on the Digitone) a VA engine per sound | ENGINE FM / VA (EDIT, SYN2 2): two band-limited oscillators (SAW, SQR with PW, TRI, SIN), sub, noise, OSC2 tune, detune, hard sync, cross modulation, drift, pitch envelopes, a wavefolder and ring modulation (SHAPE, 0.6); the same filters, amp, LFOs and effects; 22 factory sounds | Added (the Digitone II has other machines; TONIC's VA is its own) |
| (Not on the Digitone) a DRUM engine per sound | ENGINE DRUM (EDIT, SYN2 2): a kit of 8 synthesized pads on one track, 31 models (BD 808 / 909 / FM / SUB / DIST / PUNCH, SD 808 / 909 / FAT / 606 / BRUSH, RIM, CLAP, CLAP 2, HH CL / OP, HH 909 CL / OP, HH FM, HH 606 with choke, CYMBAL, TOM, CONGA, COWBELL, CLAVE, SHAKER, MARACA, TAMBOURINE, ZAP, LASER, NOISE), TUNE DEC TONE LEV a pad (lockable per pad), the white keys as pads, the grid places the picked pad (up to 4 a step), a lane a pad in the piano roll; every pad and the whole kit as LFO / modulation destinations; the same filters, amp, LFOs and effects; 11 factory kits | Added (the Digitone II has drum machines of its own; TONIC's are its own) |
| (Not on the Digitone) momentary performance effects | HOME + SCL + white 1–9, while held: ROLL 1/4–1/32 (a sequencer loop roll that comes back in time), HALFTIME, TAPE STOP, LP / HP SWEEP, reverb FREEZE | Added |
| (Not on the Digitone) a euclidean generator | SEQ › EUCLID: LEN, HITS, ROT, 10 named rhythms; EUC ACC: accents, ghost notes, chance; EUC NOTE: notes walking the scale or the chord over 1–3 octaves; a kit's KIT 1-4 / 5-8: every pad's hits; rewrites the track (a kit: the picked pad) at each turn, undoable; the rhythm drawn as a circle | Added |
| (Not on the Digitone) chords per sound | ARP › CHORD: 22 chords (triads, sixths, sevenths, ninths, sus, quartal, cluster, SCL3 / SCL4 from the keyboard scale), INV, VOICE (close, open, drop 2, wide), BASS, STRUM with direction; on every note (keys, trigs, MIDI), with or without the arp; arp modes RAND and CHRD | Added |
| (Not on the Digitone) a master compressor | FX › COMP: AMT (threshold and ratio), ATK, REL, MIX (parallel), the gain reduction shown; lockable | Added |
| (Not on the Digitone) a piano roll | SEQ's third page: a keyboard (the sounding notes lit), shaded lanes, the notes as bars in the track's colour (retrigs cut in, soft hollow), a velocity lane, a gliding playhead; zoom 16/32/64, a held step edited in place | Added |
| Quantize after recording (amount) | GLO SCALE › MICRO: scales the track's micro timings toward the grid | Implemented (as a knob, not a menu) |
| CONTROL ALL (hold [MIDI] + change; [NO] reverts) | hold the page button (SEQ, EDIT, ENV, LFO) + turn: every track; SCL while still holding reverts | Implemented |
| Page randomize ([PARAMETER]+[YES]) | page button + REC: the active row's sound parameters; page button + SCL reverts | Approximated (the row of four, not the page of eight; no reload to saved state) |

## Effects and mixing

| Digitone | TONIC | Status |
| --- | --- | --- |
| Chorus (depth, speed, HPF, width, sends to delay and reverb) | the same | Approximated (algorithm and curves are TONIC's) |
| Delay (time in 128ths, ping-pong, width, feedback 0–198, HPF, LPF, reverb send, volume) | the same | Approximated; longest time ~1.7 s (RAM) |
| Reverb (pre-delay, decay, shelving freq/gain, HPF, LPF, volume) | a plate at half rate | Approximated (algorithm is TONIC's) |
| Track levels, pattern volume | (TL/127)², 1.6384·(PVOL/128)² (unity at 100) | Implemented (OS; the track-level word's identity INFERRED) |
| Master overdrive | a soft clipper | Approximated (the OS's curve not decoded) |
| (Not on the Digitone) sample-rate and bit reduction | ENV › LOFI: SRR (hold 1–64 samples), BRR (16–2 bits), per sound, lockable, before the filters | Added |
| (Not on the Digitone) DJ filter | FX › MIXER FILT: a 2-pole lowpass / highpass on the mix (78 Hz … off … 7.7 kHz); HOME held: K1 | Added |
| (Not on the Digitone) sidechain ducking | FX › DUCK: source track or a tempo division (1/4 … 1/1: volume shaping), depth, release 10 ms–1.3 s, the FX returns too; FX › PUMP: a kit source's pad, attack, a track kept out; the curve drawn; HOME held: K2 | Added |
| (Not on the Digitone, 0.12) per-pad drum controls | each pad's model-specific CTL A / B (BEND TICK, SNAP BODY, NOIS RESO, …) and PAN | Added |
| (Not on the Digitone, 0.12) insert effect per voice | OVER, HARD, FUZZ, RECT, FOLD, CRUSH with AMT, TONE, MIX; LFO / mod destinations | Added |
| (Not on the Digitone, 0.12) modulation envelope | DEL ATK DEC SUS REL to any destination, ± depth | Added |
| (Not on the Digitone, 0.12) the sound's scale | notes snap to SCALE / ROOT; chords by degrees (2ND … 7TH, TRIAD, 7TH+, 9TH+, SUS, QUART, POWER) | Added |
| Length per track | LEN and SCALE always per track (SEQ › TRACK); MLEN, CHLEN | Extended |
| External input mixer | — | Unavailable (no audio input on the FM-1) |
| Separate outputs, Overbridge | USB audio out of the master (fm1-base) | Partly unavailable |

## Storage, MIDI, hardware

| Digitone | TONIC | Status |
| --- | --- | --- |
| Projects (128), sound pool (128), +Drive (2048 sounds) | one project: 32 patterns, 32-sound pool, 32-sound user library, factory sounds; backup over USB | Approximated |
| Digitone project / sound / SysEx compatibility | none | Unavailable (not implemented, not claimed) |
| MIDI in/out DIN, USB | USB in/out, TRS in | DIN out unavailable |
| CC map (Appendix B) | the sound parameters' CC numbers, mute 94, level 95, program change | Implemented (CC MSB only; NRPN deferred) |
| MIDI clock in/out | yes | Implemented (in: tempo from the clocks' spacing) |
| 8 data knobs + LEVEL/DATA, 16 trig keys, arrow keys | 4 knobs + 3 encoders, 16 white + 11 black keys, 14 buttons | Adapted (see USER_GUIDE) |
| 128 × 64 OLED | 240 × 240 colour LCD: the OLED's layout at full resolution, track colours, page tabs, meters, moving envelope / LFO / filter pictures, 5 themes | Adapted |
