# TONIC — design record

TONIC is custom firmware for the M-VAVE FM-1 that recreates the original Elektron Digitone's (2018, OS 1.4x)
synthesis, sequencing and workflow as far as the FM-1 allows. This file records the engineering decisions and why.

## Foundation

| Decision | Choice | Why |
| --- | --- | --- |
| Platform layer | [fm1-base](https://github.com/charlesvestal/fm1-base) @ `6c0b47e` (git submodule `base/`), on felucca-bsp `0f08fca` | MPL-2.0 platform used by shipping firmwares (CTL-1, Radio): boot, LCD, panel, LEDs, audio ISR, USB/TRS MIDI, A/B torn-write-safe flash store, update entry, installer, *and* a host simulator + browser emulator that run the same app code. Felucca (GPL-3.0) is a full multi-engine instrument; its UI/sequencer model is far from the Digitone, so reusing it would mean rewriting most of it anyway. |
| App code | written from scratch, `tonic/` | Digitone fidelity drives every data structure (patterns own sounds, p-lock pool, trig conditions, 1/384 micro timing). No Felucca code is copied, so the app's licence is free; chosen MPL-2.0 to match the base. |
| Toolchain | JieLi `pi32v2` clang (Linux x86-64) in a `linux/amd64` Docker container, driven by `base/tools/build.py` | The only compiler for the AC791N; fm1-base already scripts it. |

## Hardware budget (sources: the research notes on the FM-1 platform)

- CPU: one pi32v2 core used (the second core is beta in felucca-bsp and is not used here). Audio at 44.1 kHz,
  256-frame blocks (5.8 ms).
- RAM: 96 KB `.data/.bss`, 336 KB pool for large buffers.
- Flash data area 0x97000–0xDFFFF (73 sectors of 4 KB); each store object takes two sectors (A/B) and holds at most
  3840 bytes.
- No hardware CPU measurement is possible here (no FM-1 on the bench). The device binary runs in fm1-emu, which counts
  the real pi32v2 instructions: 8 heavy voices with every effect take 53 % of a 240 MHz core there (a lower bound: no
  cache or flash wait states). The engine tests keep a host-side guard (instructions per sample) against regressions.

## Instrument structure (Digitone → TONIC)

| Digitone (OS 1.4x) | TONIC |
| --- | --- |
| 8 voices, 4 synth tracks, voice stealing / reuse / locked voices / unison | 12 voices (0.11; 8 before) shared by 4 synth tracks, same allocation rules (CYCLE, TRACK, LO, HI; REUSE) |
| 4 MIDI tracks | any of the 4 tracks can be a MIDI track (MIDI TRK: its notes, chords and conditions go to USB-MIDI out); synth tracks can also echo their notes |
| 4-op FM (C, A, B1, B2), 8 algorithms with X/Y outputs, HARM, DTUN, FDBK, MIX, op envelopes A/B | same structure; routings, curves and tables from the OS's own code (`docs/RESEARCH.md` §3) |
| Overdrive → base-width filter → multimode filter → amp → pan → sends | same order |
| 2 LFOs per voice, 7 waves, 5 trig modes, SPD×MULT tempo table | same, with the OS's waves, fade law and depth scaling |
| Chorus, delay, reverb sends; master overdrive; mixer | same set; buffers sized to the pool |
| 128 patterns × 8 banks, 64 steps, per-track length/scale, p-locks (80 params), sound locks, trig conditions, micro timing 1/384, swing 50–80 % | 32 patterns (2 banks × 16), 64 steps, per-track length/scale, a p-lock pool sized to fit one flash object, sound locks from a 32-sound pool, all Digitone trig conditions, same micro timing and swing |
| Grid / live / step recording | all three |
| 16 trig keys | the FM-1's 16 white keys are the 16 trig keys |

## Sequencer timing

The sequencer runs inside the audio render, sample-accurately: each 256-frame block is split at event times.
Internal resolution is 1/384 of a whole note (24 ticks per 16th step), the Digitone's micro-timing unit. Each track
has its own tick accumulator (track scale), fed by the pattern tempo. Pattern edits happen in the main loop; the
render reads them. Shared structures are written so that every single store the render can observe is consistent
(32-bit lock entries, append-only lock pool with tombstones, pointer handoff for pattern changes).

## Parameter changes

Each voice holds its own copy of its sound's parameters, taken at note-on from the track's sound plus the trig's
locks. A knob turn updates the track's sound and every voice of that track that does not have the parameter locked.
A lock trig updates the sounding voices of its track at once. As in the OS, every parameter of a voice is smoothed by
a one-pole filter at control rate (every 32 samples, τ ≈ 22 ms, linear inside a block); the smoothing goes on through
a note-on, and is skipped for a voice that takes another sound and for the parameters the trig (or the one before)
locks.

## Storage

- One store object per pattern (sequence + its 4 sounds + FX/mixer), compactly serialized; the UI refuses an edit that
  would make a pattern exceed one object ("PATTERN FULL").
- Project object: sound pool (32), tempo, chain, settings. User sound library object (32 sounds).
- Flash writes stall the CPU (code runs from flash), so writes never happen while the sequencer plays. Edited patterns
  stay in RAM and are written when playback stops (after the effects ring out) or on SAVE.

## Screens

The screen keeps the Digitone OLED's layout but is drawn at the LCD's full 240 × 240 (`app/draw.c`, 0.4): a 1-bit
picture (7.2 KB, in the pool) and a colour index for each 4 × 4 cell (1.8 KB). The layout is written in units of
2 × 2 pixels (120 × 120, as before); fills take whole units, lines, frames and curves are one pixel thin, and the
fonts are full resolution (`tools/gen_oled_font.py`: S9 5 × 9, the 5 × 7 glyphs with two rows doubled; M14 and M28,
the 5 × 7 glyphs smoothed by Scale2x once and twice). Whatever is drawn gives its cells the current ink (`o_ink`,
a K_* index); the blit maps the indices through the theme's palette (THEMES: OLED, AMBER, PHOSPHOR, BLUE, PAPER;
GLO > DISPLAY). The tracks have their colours (header bar, steps, track keys, HOME, meters, mutes), the edited row's
names and locked values the accent.

- Under the header: the group's pages as tabs, the shown one in the accent (a window with < > when they do not fit).
- Left: the four tracks' meters in their colours and the master's (green, yellow, red; −42 … 0 dB), labels under them.
- Moving pictures from the track's newest voice (`engine_env_view`, `engine_lfo_phase`, `engine_filter_view`): a dot
  on the envelopes (operator A / B, filter, amp) where the sound is, on the LFO wave at its phase; the filter's
  response (LP2, HP2, LP4 as the engine's) with the cutoff now, the set one dotted; the algorithm's lines as thick as
  LEV × envelope.
- The LCD gets the picture in ten bands of 24 rows; a band goes only when its pixels or colours changed (a hash).
- Cost: 6 KB of the delay line (its longest time about 1.7 s).

## Undo

Two more pattern copies in the pool: the pattern as it last settled (no edit for 0.7 s) and the state before that.
SCL+HOME puts the pattern before the latest edits in place through the same pointer handoff as a pattern change,
keeping what it replaced for a redo. The copies cost 11.4 KB, taken from the delay line (its longest time is now about
1.8 s; 1.7 s since the full-resolution screen).

## Sound tools (TONIC's own)

- **Retrig** runs in the sequencer (`seq.c rt_block`): a note trig with RTRG on keeps its notes, velocity and lock
  key, and plays them again every RTIM (1/N note in track ticks, so the track's speed scales it) until its LEN has
  passed; each repeat is a note of 0.9 × the interval. The next note trig of the track, PLAY and a pattern change
  end it. RVEL scales the velocity linearly over the length.
- **LO-FI** is per voice, after the overdrive and before base-width and the multimode filter: bit reduction rounds to
  a quantum of 2^(BRR·14/127 − 15), then a fractional sample-and-hold takes a new sample every 2^(SRR·6/127)
  samples. Both are skipped at 0.
- **Ducking** works on the track levels: the source track's note-ons (`eng_voice_on`, so keys, trigs, retrigs and
  MIDI tracks alike) set an envelope to 1, which falls with REL's time constant; the other tracks' level targets are
  multiplied by 1 − DPTH·env, and the existing ~3 ms level ramp is the attack. With FX ON the effect returns get the
  same gain, ramped over each block.
- **DJ filter**: a TPT state-variable filter (Q 1) on the master after the overdrive; its knob glides (~7 ms) and
  the coefficients are recomputed once a control block.
- **Saved form**: LO-FI and retrig added parameter ids in the middle of the id space, so the pattern format went to
  version 2; a version 1 pattern's locks are moved up on loading (`pattern_unpack`). Sounds and the FX block keep
  their ids (they grew at their ends).

## More drums and destinations (0.9)

- Ten drum models on the existing paths (the kick's sine, the snare's tones and noise, the metal, the shaker), and
  an FM hat of its own; closed hats (HH CL, HH 9 CL, HH 606) choke the open ones.
- 22 LFO / modulation destinations appended (`D_SRR` … `D_KLEV`, `NDEST` 80): LO-FI, pads 5-8, the kit's four
  macros (added to every pad in `drv`). The DEST parameters' ranges grew to them; LFO1's knob skips LFO1's own
  parameters. Existing sounds sound the same (golden renders).

## Chords and the euclidean generator (0.8)

- `chord_notes` (`params.c`) builds a note's chord from the sound (CHORD, INV, VOICE, BASS) and the track's keyboard
  scale. `eng_notes_on` (`engine.c`) starts several notes as one id (they end together: a key's release ends them
  by that id), each voice of a strummed chord waiting its turn in control blocks (`voice_t.wait`). `eng_chord_on`
  is what the keys, the trigs and the retrigs call: the chord when the sound has one (not a kit), else the note
  alone. The arpeggiator expands its held notes the same way (`arp_list`); CHRD hands the whole list to
  `eng_notes_on` each step. Four setup bytes (`S_CHVOIC` …), and the arp modes RANDOM and CHORD, appended.
- The euclidean generator (`ui.c`, `euc_*`) keeps its settings per track and pad in `ui` (EU_*); named rhythms are
  16-bit masks with their length. On a melodic track it writes velocity and trig conditions; on a kit, whose steps
  hold several pads, the pad's LEV locked per step, and its dice rolled at each write.

## The momentary effects (0.7)

`eng.pfx` holds the effects the keys hold (the UI sets bits, the render reads them). ROLL lives in the sequencer
(`seq.c`): each track's real position runs on silently while the events come from a stretch around where the roll
began (`r0`, `rpos`), so a roll ends in time; no audio is buffered. HALF and TAPE scale the sequencer's ticks
(`eng_speed`); TAPE also shifts every voice's pitch by 12·log2(speed) (`eng_pitch`) and fades the output's last
quarter. The sweeps offset the DJ filter's value, FREEZE sets the reverb to DEC INF with the treble kept and fades
its input out (`fx_freeze`). With nothing held the samples are 0.6.1's (golden renders, device benchmarks).

## 0.6's additions

- **Euclid** (`ui.c`, `euc_*`): Bjorklund's distribution as (i × HITS) mod LEN < HITS, rotated; the generator writes
  trigs (a kit: its picked pad joins or leaves steps), so the result is ordinary pattern data, and undo covers it.
  Its settings live in `ui` (per track and pad, not saved).
- **Arp chords** (`seq.c`, `arp_chord` / `arp_list`): the held notes are expanded into their chords each time the
  arpeggiator picks a note (up to 32); SCALE 3 / 4 stack every other note of the track's keyboard scale
  (`KSCALE_MASK`, now in `params.c`). Two setup bytes, `S_ARPCHD`, `S_ARPINV`.
- **Compressor** (`engine.c`): after the effects, a peak detector a control block, the gain computed in dB a block
  and ramped over its 32 samples; ~1 % CPU when on, nothing when AMT is OFF. Four FX parameters appended
  (`P_CPAMT` …), so the pattern format stays version 3 and 0.5.x reads 0.6's patterns.
- **FOLD / RING** (`synth.c`, `va_block`): in two of the drum pads' parameter places, whose default 0 keeps every
  VA sound from before unchanged; a triangle folder after the mix; OSC1 × OSC2 added. The LFO destinations RAT A
  and RAT ALL (unused under VA) reach them.
- **New drum models**: CLAP 2 (four bursts), TAMB (the metal higher, a second shake), MARACA (the shaker's path,
  short), LASER (the kick's path with a wide sweep).
- Checked: the golden renders and the device benchmarks give the same samples as 0.5.2 with the new things off.

## Optimizations (0.5.2)

The sound is the same sample for sample (`docs/BUILDING.md`: the golden renders and `tools/perf_probe.sh`); only
how it is computed changed:
- A voice's engine state (FM operators, the VA oscillators' settings, the drum) shares one union in `voice_t`: a
  voice plays one engine at a time. What has to outlive a note (VA's free-running phases and drift, the noise
  seed) stays outside it; `voice_engine` clears the union when a voice changes engine. Two scratch buffers reuse
  memory that is free when they run (`build_harm` a voice's HARM table at boot, `track_shift` the store's buffer).
  RAM: −3 KB.
- `voice_control`'s modulation array stays zero between calls: a call sets back what it wrote, instead of clearing
  all 58 destinations each block; FM and VA voices skip the drum pads' parameters while gliding.
- The overdrive clamps only its first stage (after it |u| ≤ 1, where the clamps change nothing).
- `fx_params` recomputes the effects' coefficients only when a value or the tempo moved (the DJ filter's glide
  runs every block as before).

## The DRUM engine (TONIC's own)

- `S_ENGINE` ENG_DR makes a sound a kit of 8 pads. A note picks its pad by its white key (`pad_of`: C4 … C5 are pads
  1–8, the white keys counting on mod 8); the seq leaves a kit's notes untransposed. Each pad has a model (setup bytes
  `S_DM1..8`) and TUNE DEC TONE LEV: pads 1–4 in SYN slots (`DRP`; each range inside the slot's own, q 1, so they keep
  locks, smoothing and LFO destinations), pads 5–8 in 16 parameters of their own (`P_D5TUN … P_D8LEV`, appended to
  the sound's parameters).
- The voice (`synth.c`): `dr_start` latches the pad and model at the hit, `dr_control` turns the pad's parameters
  into frequencies, per-sample envelope factors and SVF coefficients each control block, `dr_block` makes the
  samples, the model's loop chosen once a block: sines with a falling pitch (kicks, toms, congas, zap), tones and
  filtered noise (snares), pings (rim, clave), three noise bursts (clap), six detuned squares through a bandpass and
  a highpass (the 808's metal: hats, cymbal), two squares through a bandpass (cowbell). After it the voice is the FM
  voice's. The AMP envelope starts open; the voice ends when the drum has died away.
- Voices (`engine.c`): a pad takes its own voice again (the old hit fades in the fader); a closed hat kills the
  track's open hats. Mono modes and unison do not apply to a kit.
- Saved form: the 16 parameters moved every later id up, so patterns went to version 3 (versions 1 and 2 are moved
  up on loading). The sound banks went to version 3 too: a whole `sound_t` (264 B) no longer fits 16 to an object,
  so each sound is stored in its compact form (`sound_to_bits`: each parameter in the bits of its range, 169 B).
  Banks of versions 1 and 2 are still read. Older builds cannot read either version 3.
- Cost: 16 parameters × 8 voices plus the drum state, about 3 KB of RAM; the sounds in the pool area, 2 KB of the
  delay line (its longest time now about 1.68 s).

## The VA engine (TONIC's own)

- A sound's `S_ENGINE` (setup byte, saved with the sound; old sounds read as FM) picks FM or VA. Under VA the SYN
  pages' parameters (P_ALGO … P_PHRT) hold the VA parameters (`core/tonic.h` V_*): `param_desc(engine, p)` gives
  each one's name, range and default, and every VA range lies inside the parameter's own with the same scale, so the
  saved form, the locks, the smoothing and the clamps are unchanged. `sound_set_engine` switches and resets those
  parameters to the engine's defaults.
- The voice (`synth.c`): `va_control` turns the parameters into increments and levels each control block (pitch
  envelopes from the operator envelopes A and B, detune, drift as a glided random target); `va_block` makes OSC1's
  phases first (its wraps restart OSC2 under SYNC), then OSC2, OSC1 phase-modulated by OSC2 (XMOD), the sub and
  the noise, each wave chosen once a block. Saw and square are PolyBLEP band-limited; the square has its DC removed.
  After the oscillators the voice is the FM voice's: drive, LO-FI, base-width, multimode filter, amp.
- The LFO destinations keep their numbers; under VA the ones on SYN parameters show and scale as the VA
  parameters (`dest_name`, `lfo_unit`, the modulation pages' spans).
