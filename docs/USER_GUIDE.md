# TONIC user guide

TONIC turns the FM-1 into a four-track FM groovebox modelled on the original Elektron Digitone: four synth tracks
sharing twelve voices, a four-operator FM engine with the Digitone's eight algorithms, a sequencer with parameter locks,
sound locks, trig conditions and micro timing, an arpeggiator, chorus, delay and reverb.

If you know the Digitone, the short version: **the 16 white keys are the trig keys; REC is grid recording; REC+PLAY
is live recording; SCL (the second page button) is FUNC; the page buttons open the Digitone's parameter pages, four knobs at a time.**

## The controls

| FM-1 control | In TONIC |
| --- | --- |
| **K1–K4** | The four parameters of the active row. With a step held: that step's parameter locks. |
| **SELECT** knob | The next / previous row of the page group. On HOME: tempo. On SAVE: the list. |
| **ALGORITHM** knob | The active track, T1–T4. |
| **PRESETS** knob | The track's sound (factory, then user library), a list window opens at the track's own sound: turning moves the cursor, OCT+ loads, OCT− closes; SCL+PRESETS: by group. HOME: the pattern. With a step held: a sound lock. Step recording: the active step. SAVE: the slot. |
| **MASTER** | Volume. |
| **SEQ** | TRIG page: NOTE, VEL, LEN, PROB / FLT.T, LFO.T, PTIM, PORT. Pressed a third time: the **piano roll** (below); a fourth: **RTRG** (retrig: on, rate, velocity fade). |
| **EDIT** | SYN1 (ALGO, C, A, B / HARM, DTUN, FDBK, MIX), SYN1 2 (ratio offsets), SYN2 (operator envelopes A and B), SYN2 2 (delays, trig mode, reset, phase reset, **ENGINE** FM / VA / DRUM). Under VA: OSC, OSC 2, ENV, ENV 2; under DRUM: the kit's pads, LEVEL, KIT (below). |
| **ENV** | FLTR (envelope / FREQ, RESO, TYPE, ENV), FLTR 2 (delay, BASE, WIDTH, key track), AMP (ADSR / DRV, PAN, VOL, AENR), AMP 2 (chorus, delay, reverb sends), LOFI (SRR sample-rate reduction, BRR bit reduction). |
| **LFO** | LFO1, LFO2 (SPD, MULT, FADE, DEST / WAVE, SPH, MODE, DEP). |
| **FX** | CHORUS, DELAY, REVERB, MIXER (track levels, master overdrive, pattern volume, the DJ filter FILT), DUCK and PUMP (sidechain ducking, volume shaping), COMP. |
| **ARP** | The arpeggiator (MODE, SPD, RNG, LEN / STEPS and each step's offset). |
| **GLO** | TEMPO (BPM, swing, tempo mode, metronome, clock, quantize, velocity), SCALE (length mode, track length, speed, transpose, master and change length, MICRO: quantize after recording), VOICE (stealing, reuse, locked voices, unison, spread, layer), SETUP (key scaling, octave, play mode, note priority, velocity to volume, pitch bend), PORTA, VEL MOD / MODWHEEL / AFTERTCH / BREATH / PITCHBND (four destinations each), SONG, KEYBOARD (scale, root, fold, chord), MIDI (channels, MIDI tracks), DISPLAY (THEME: OLED, AMBER, PHOSPHOR, BLUE, PAPER). |
| **HOME** | Tap: the overview (patterns, tracks, levels with K1–K4). **Hold: the performance layer** (below); **tap twice: lock it** (a tap leaves). |
| **SAVE** | The save page. **SCL+SAVE**: save everything now. |
| **PLAY** | Start / stop. SCL+PLAY: stop and cut the effects' tails. |
| **REC** | Grid recording on / off. **REC+PLAY**: live recording (PLAY again while holding REC: quantize on/off). **SCL+REC**: step recording. |
| **OCT− / OCT+** | Keyboard octave. SCL+OCT: transpose the track. A step held: its note (SCL: by octaves). Grid: the step page. |
| **SCL** | The Digitone's FUNC key (see each section). |

Pressing a page button again moves to the group's next row; SCL+page button goes back. Each page shows its eight
parameters in two rows; the lit row is the one K1–K4 turn. A slow knob turn moves the finest step (a quarter step on
the parameters the Digitone shows with two decimals); a fast turn moves in whole steps.

## Making a sound

1. Pick the track with ALGORITHM, then turn PRESETS to start from a factory sound, or SAVE > INIT SOUND for a plain
   sine.
2. **EDIT, SYN1.** ALGO picks how the operators C, A, B1 and B2 connect (the picture below the page shows it, the
   feedback operator marked). C, A and B set their frequency ratios (B moves B2 first, then B1, like the hands of a
   clock). HARM changes the operators' waveforms (negative: C; positive: A and B1). DTUN detunes A and B2, slightly
   up to about 64 and strongly above. FDBK is the feedback amount. MIX fades between the algorithm's two outputs X and Y.
3. **EDIT, SYN2.** The two operator envelopes: A for operator A, B for B1 and B2. LEV A and LEV B set how much they
   modulate: nothing happens until a LEV is up. LEV B shares itself between B1 and B2 (B1 alone below 43, B2 takes over
   up to 85, both above). SYN2 2: an envelope delay, trigged (ADE) or gated (ASDE), whether a new note restarts it,
   and which operators' phases restart.
4. **ENV.** The filters and the amp: a base-width band (BASE, WIDTH) and a multimode filter (LP2, HP2, LP4) with its
   own envelope, then the amp envelope, drive, pan and volume, and the sends to the effects.
5. **LFO.** Two LFOs per voice; SPD × MULT sets the rate (synced to the tempo, or to a fixed 120 BPM with the
   dotted multipliers); MODE: free, trigged, held, one-shot, half. LFO2 can also move LFO1. DEP, as on the
   Digitone, moves the destination by 2 of its units per DEP step: on PITCH, DEP 0.25 is a half-semitone vibrato and
   1.00 is ±2 semitones (the knob moves in 1/16 steps near zero).
6. Play it on the keys (velocity-sensitive; GLO > TEMPO > VEL sets fixed velocity instead). GLO > SETUP and the
   modulation pages route velocity, mod wheel, aftertouch, breath and pitch bend from MIDI to any parameter.
7. Keep it: SAVE > SOUND > LIBRARY (32 user slots, chosen with PRESETS) or SOUND > POOL (16 slots: the sounds a
   pattern can sound-lock).

## Making a pattern

**Grid recording** (REC lit): the white keys are steps 1–16 of the page.

- Press an empty step: a note trig at the track's NOTE. Press a trig briefly: it goes. Hold it longer to edit it.
- **Hold a step and turn K1–K4: parameter locks** for that step only. Locked values show inverted. The TRIG page's
  NOTE, VEL and LEN set the step's own note, velocity and length; with a step held PROB becomes **COND**, the trig
  condition (1%…99%, FILL, !FILL, PRE, !PRE, NEI, !NEI, 1ST, !1ST, A:B).
- Hold a step and turn PRESETS: a **sound lock** from the pool.
- SCL+white key: a **lock trig** (locks without a note).
- Black keys in grid mode: 1–4 the four 16-step pages (hold one and press a white key to set the track's length),
  6 / 7 micro timing earlier / later (1/384 note steps) for the held steps, 9 COPY, 10 PASTE, 11 CLEAR (held steps:
  their locks; else the page; with SCL: the whole track).
- OCT−/OCT+ with a step held: its note up or down (SCL: an octave).

**Editing all tracks at once** (CONTROL ALL): hold the page's button (SEQ, EDIT, ENV or LFO) and turn a knob: the
value goes to all four tracks. Press SCL before letting go of the button to undo. **Randomize**: hold the page button
and press REC: the four sound parameters of the active row get new random values (SCL, still holding, undoes it).
A short tap of the page button steps to its next row as before.

**Live recording** (hold REC, press PLAY): play the keys; notes are written with their timing (quantized if set) and
length; chords on one step become chords. Turning a knob records locks. PLAY leaves live recording, PLAY again stops.
To quantize afterwards: GLO, SCALE (second row), K4 **MICRO** shows the track's largest micro timing; turning it down
shrinks every offset on the track in proportion, 0 puts all trigs on the grid.

**Piano roll** (SEQ, third page): the active track's notes, steps across and pitch upwards; each note is a bar as
long as it sounds, chord notes stack, micro timing shifts them, soft notes (velocity under 64) are hollow, lock
trigs are ticks at the bottom, and the playhead is a line. **K1 VIEW**: AUTO fits the track's notes, or turn it to
choose the lowest note shown. **K2 ZOOM**: 16 steps (the step page the keys reach), 32 or 64 (the line on top marks
the keys' page). **K3 NOTE / K4 LEN**: with a step held in grid recording, its note and length; the bar moves as you
turn.

**Step recording** (SCL+REC): the keys are a keyboard again; play a note or a chord and it is written to the active
step, which then moves on. PRESETS moves the active step; knob turns lock the active step.

**Lengths and speed:** GLO > SCALE: per-pattern or per-track length (2–64 steps), speed (1/8X–2X), master length
and change length. Different track lengths make polymeters.

**Swing, tempo:** GLO > TEMPO, or SELECT on HOME.

**Song:** GLO > SONG. ROW picks the row to edit (a higher row lengthens the song), PTN its pattern, REPEAT how
often it plays, MUTE which tracks are muted in it; on the second row SONG switches song mode on, ROWS sets the
song's length, END loops or stops at the end. With song mode on, PLAY starts the song from its first row.

## Sound tools (TONIC's own, not on the 2018 Digitone)

- **Retrig** (SEQ, fourth page RTRG): RTRG ON makes every note trig of the track repeat over its length (LEN) at
  RTIM (1/1 … 1/80 notes; 1/16 is a step). RVEL fades the repeats: negative values to silence at −128, positive ones
  in from silence at +127. All three can be locked per step, so one trig can roll and the next play once. Each repeat
  is a short note; set LEN for how long the roll lasts.
- **LO-FI** (ENV, page LOFI): SRR holds each sample for longer (aliasing, grit; it sweeps smoothly), BRR rounds to
  fewer bits (16 bits down to 2). Both come after the overdrive and before the filters, so the filter can tame them.
  Lockable, per sound.
- **DJ filter** (FX > MIXER, K3 FILT): one knob on the whole mix, after the master overdrive: below 0 a lowpass (to
  78 Hz at −64), above 0 a highpass (to 7.7 kHz at +63), 0 off.
- **Ducking** (FX > DUCK): DUCK picks a source track (OFF, T1–T4). Each of its notes pushes the other tracks down by
  DPTH, and they come back with REL (10 ms … 1.3 s). FX ON ducks the chorus, delay and reverb returns too. The source
  can be a MIDI track: a silent track of kick trigs ducks the pads without sounding. Since 0.10 DUCK can also be a
  division of the tempo (1/4, 1/8, 1/16, 1/2, 1/1): volume shaping with no kick. FX > PUMP: PAD (a kit source: only that
  pad ducks, e.g. the BD), ATK (the dip comes in over 2–250 ms), KEEP (a track never ducked). Both pages draw the curve.
- On the **performance layer** (HOME held) K1 is the DJ filter and K2 the ducking depth; the screen shows both.

- **VA engine** (EDIT, SYN2 2, K4 ENGINE: VA): two oscillators instead of the FM operators. OSC: WAV1, WAV2 (SAW,
  SQR, TRI, SIN, OFF), SUB (a square an octave down) / TUNE (OSC2, ±24 semitones), DETN, PW (pulse width), MIX.
  OSC 2: SYNC, NOIS, XMOD (OSC2 modulates OSC1), DRFT (analog drift). ENV: envelope A with PENV (pitch of both),
  envelope B with MENV (OSC2's pitch: sync sweeps); RST ON restarts the oscillators on each note. Switching ENGINE
  sets the SYN pages to its defaults; filters, amp, LFOs and effects stay. Factory sounds: the VA … group (22 sounds); SCL+PRESETS jumps
  straight there (it steps the groups: KEYS, BELLS, BASS, LEADS, PADS, DRUMS, TEXTURES, VA, KITS, INIT).
- **DRUM engine** (K4 ENGINE: DRUM, or a KITS preset): the track becomes a kit of 8 synthesized pads. The white keys
  play pads 1–8 (C4 … C5; keys 9–16 again); a pad played moves the knobs to its row. The SYN pages: a row a pad
  (MODEL, TUNE, DEC, TONE), LEVEL (the 8 levels), KIT (ENGINE). 17 models: BD 808 / 909 / FM, SD 808 / 909, RIM,
  CLAP, HH CL / HH OP (a closed hat chokes the open one), CYMBAL, TOM, CONGA, COWBEL, CLAVE, SHAKER, ZAP, NOISE. Each
  pad is a one-shot; a pad hit again takes its own voice. In grid recording a step key places the picked pad (the one
  last played, black 8 + a white key in the grid, SCL + OCT, OCT while playing, or NOTE on the TRIG page; shown at the
  bottom right, e.g. GRID 4 CH); a step holds up to 4 pads, a short tap takes the picked one out. The piano
  roll shows a lane a pad. Locks work per pad (e.g. 2 SD's TUNE on one step). Factory kits: 808, 909, FM, ELECTRO,
  TECHNO, LOFI, PERC, TRAP, MODERN (0.6: CLAP 2, TAMB, MARACA, LASER), HARD, DEEP (0.9: BD SUB / DIST / PNCH, SD FAT /
  606 / BRSH, HH 9 CL / 9 OP / FM / 606). LFO and modulation destinations (0.9): every pad, ALL TUNE / DEC / TONE /
  LEV, and SRR / BRR on any sound.
- **SHAPE** (VA, 0.6): FOLD (a wavefolder) and RING (ring modulation OSC1 × OSC2), 0–63; both 0 is the VA as before.
- **EUCLID** (SEQ's fifth page on, 0.6 / 0.8): LEN, HITS, ROT, STYLE (named rhythms); EUC ACC: ACC, AROT, GHOST,
  PROB; EUC NOTE (a melodic track): NOTES (FIXED, UP, UP DN, RAND, CHORD), RANGE; a kit: KIT 1-4 / 5-8, every
  pad's HITS. Each turn writes the track (a kit: the picked pad); undo takes it back; the circle shows the rhythm.
- **CHORD** (ARP group, 0.6 / 0.8): CHORD (22 chords, SCL3 / SCL4 from the keyboard scale), INV, VOICE (CLOSE, OPEN,
  DROP2, WIDE), BASS / STRUM (ms), DIR: every note of the sound becomes that chord, with the arp off too (keys, trigs,
  MIDI); with the arp on it walks the chord's notes. Arp modes RAND and CHRD (the whole chord every step).
- **Momentary effects** (0.7): on the performance layer, SCL + white key 1–9, as long as the key is held: 1–4 ROLL
  1/4 … 1/32 (the pattern goes on in time afterwards), 5 HALFTIME, 6 TAPE STOP, 7 LP SWEEP, 8 HP SWEEP, 9 FREEZE.
- **COMP** (FX's last row, 0.6): AMT (OFF … 127), ATK, REL, MIX; the gain reduction shows under the knobs.

## Handy things

- **Undo:** SCL + a short tap on HOME takes back the latest edits of the pattern (all that happened within a moment
  counts as one); the same again redoes them. It keeps one step, and starts afresh with each pattern.
- **Help:** hold any button on its own for a second: the screen lists what it does, alone and with the others.
- **Value pop-up:** a turned knob shows its parameter's name and value large for a moment.
- **Fine turns:** hold SCL while turning K1–K4: the smallest step, however fast you turn.
- **Row defaults:** hold the page's button and press OCT−: the four parameters of the row go back to their defaults
  (SCL, still holding, takes that back).
- **Shift a track:** in grid recording, black key 6 / 7 with no step held moves all the track's trigs (and their locks)
  one step earlier / later, round its length.
- **Tap tempo:** hold HOME and tap black key 8 in time; two taps set the tempo, up to four are averaged.

## Performing

**Hold HOME** for the performance layer (the screen shows it):

- White keys: patterns 1–16 of the bank. A pattern changes at the end of the current one (or its change length).
  Several keys pressed in turn while holding HOME make a **chain**.
- Black keys: 1–4 **mute** T1–T4, 5 **FILL** while held, 6 FILL for the next pass of the pattern, 7 the other bank (A/B),
  8 **tap tempo**, 9 **temporary save**, 10 **reload** (back to the temporary save, or to the saved pattern), 11 restart.
- K1: the **DJ filter**, K2: the **ducking depth** (with DUCK set to a track).

Also: track levels with K1–K4 on HOME, track transposition with SCL+OCT and pattern transposition with OCT while
holding HOME, the arpeggiator on any sound (ARP), the mixer page
(FX > MIXER) for levels and master overdrive.

## Saving and coming back

TONIC keeps your work by itself: what you change is written to the flash **once the sequencer has stopped and the
sound has died away** (a flash write stops the CPU for a moment, so nothing is written while it plays; SAVE glows
while something is unsaved). SCL+SAVE writes at once. When you switch the FM-1 on, the last pattern is back, with
everything as it was.

The SAVE page (SELECT picks a row, PRESETS a slot, SAVE does it; the destructive rows ask for a second SAVE, OCT
cancels): save the project now; the track's sound to the library or the pool; a pool sound to the track; init the
sound; reload the pattern from the flash; copy, paste or clear a pattern; load the demo project.

A project is 32 patterns (A01–A16, B01–B16), a 16-sound pool and a 32-sound user library. Back it up to a computer
with `python3 base/tools/fm1_backup.py backup FILE` (and `restore FILE`).

## MIDI

USB MIDI in and out (the FM-1's TRS jack is MIDI in only). Channels 1–4 play tracks 1–4 (GLO > MIDI > TRK CH); the
auto channel plays the active track. CC numbers follow the Digitone's for the sound parameters (CC 90 ALGO, 23 FREQ,
7 VOL, …: Appendix B of its manual), 94 mutes, 95 is the track level; program change picks a pattern. The sequencer's
notes go out on each track's MIDI channel (GLO > MIDI), and MIDI clock in and out can be switched on (GLO > TEMPO).
**EXT** (GLO > MIDI; MIDI TRK before 0.9.1) makes the active track a MIDI track: its trigs, chords, locks of NOTE/VEL/LEN, conditions,
micro timing and arpeggios play an external instrument over USB-MIDI, and no internal voice.
