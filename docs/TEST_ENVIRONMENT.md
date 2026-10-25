# Testing TONIC without an FM-1

Three environments run TONIC away from the hardware. They are different kinds of thing, and they establish
different things. (The survey of every FM-1 emulator found, with the code each claim was checked against, is in
the research notes, `research/fm1/EMULATION.md`, not in the public repository.)

| | 1. Host simulator | 2. Browser emulator | 3. fm1-emu (instruction-level) |
| --- | --- | --- | --- |
| What it is | TONIC's C sources compiled for the Mac against fm1-base's simulated FM-1 (`base/host/plat_host.c`), driven by a script | the same simulator compiled to WebAssembly, in a page with a clickable FM-1 panel, audio in an AudioWorklet | joakimeriksson/fm1-emu: a pi32v2 CPU and FM-1 peripheral model that runs **the real `tonic.fwsc`** |
| Code under test | TONIC's app and engine (host compiler, `-ffp-contract=off`: the same float arithmetic as the device) | as 1 | **the device binary** (JieLi clang, linker script, XIP layout, ISRs, fm1-base's device layer) |
| Screen | PNG per `shot` | live, 30 fps | BMP per `--snap` |
| Panel | scripted presses, keys (with velocity), encoders, MASTER | mouse / touch / computer keyboard | scripted (`--keys`) |
| Audio | WAV | live | WAV (`--wav`) |
| MIDI | in (scripted), out (logged) | **in from any Web MIDI controller** (TONIC's `web/emu/app.js`) | TRS MIDI in only |
| Persistence | the store in RAM, kept across a scripted `reboot` | the store in `localStorage` | flash written during a run, not kept between runs (batch mode) |
| Timing / CPU | simulated ms; CPU shown is host time | share of real time on the computer | 1 instruction = 1 cycle at 240 MHz: **instruction counts of the real binary**; no cache or flash wait states |
| Not modelled | USB, the update path, flash-erase stalls, the second core | as 1 | USB (MIDI, audio, install), cycle-accurate timing, the codec's analog side |

Plus TONIC's own **engine tests** (`tonic/tests/eng_test.c`): the engine units driven directly, with numeric checks
(pitch, timing, conditions, locks, the saved form) and a CPU guard.

## Launching

All commands from `tonic/` after `. tools/env.sh` (see BUILDING.md for the prerequisites).

**Engine tests**

```sh
cc -std=gnu99 -O2 -ffp-contract=off -Icore -Iengine -I../base/include tests/eng_test.c core/*.c engine/*.c \
   -o build/test/eng_test && build/test/eng_test build/test      # WAVs in build/test/
```

**Host simulator**

```sh
python3 ../base/tools/build.py . --host
build/host/tonic_host tests/host/workflow.txt build/out           # the workflow checks
build/host/tonic_host tests/host/smoke.txt build/out              # screens and the demo as WAV
build/host/tonic_host tests/host/control.txt build/out            # CONTROL ALL, randomize, revert
build/host/tonic_host tests/host/midi.txt build/out               # MIDI in, CC, MIDI tracks out
build/host/tonic_host tests/host/song.txt build/out               # song mode
```

Script commands: `wait MS`, `press/release/tap BTN`, `key K down|up [VEL]`, `tapkey K`, `turn/spin ENC N`,
`master N`, `midi B0 B1 B2`, `wav FILE`/`wavstop`, `shot FILE.png`, `leds`, `reboot`, `expect WHAT VALUE`
(full list at the top of `base/host/plat_host.c`; TONIC's own `expect` names are in `app.c`).

**Browser emulator**

```sh
python3 ../base/tools/build.py . --web
python3 -m http.server -d build/emu 8790        # open http://localhost:8790/ , press "Switch on"
node tests/web_emu_test.mjs                     # the same WebAssembly, headless: boot, PLAY, a MIDI note
```

On the page: click the keys and buttons, drag the knobs, or play the computer keyboard; a MIDI controller plugged
into the computer plays TONIC (allow MIDI when the browser asks). The project is kept in the browser.

**fm1-emu with the real firmware**

```sh
git clone https://github.com/joakimeriksson/fm1-emu ../ref/fm1-emu && (cd ../ref/fm1-emu && cargo build --release -p fm1-emu -p fm1-live)
python3 ../base/tools/build.py .                                    # build/tonic.fwsc
../ref/fm1-emu/target/release/fm1-emu --fwsc build/tonic.fwsc --ms 7000 --keys '3000:PLAY' \
    --snap 2500,6900 --wav ../ref/fm1-emu/work/tonic.wav           # screens in ../ref/fm1-emu/work/
../ref/fm1-emu/target/release/fm1-live --fwsc build/tonic.fwsc     # a window: play it (JIT, real time)
tools/bench_build.sh   # build/bench/tonic-bench.fwsc: 8 heavy voices; its HOME screen shows the render's load
tools/upgrade_test.sh OLD.fwsc NEW.fwsc   # an update: a project made under OLD, booted by NEW
```

`--flash FILE` restores the whole chip, the app area too: booting another package on the same file runs the old
firmware. `upgrade_test.sh` moves only the project's areas (0x97000..0xE4FFF, 0xFC000..0xFDFFF).

fm1-emu needs a recent Rust (it uses `if let` guards). The batch runner is about 20 times slower than real time;
`fm1-live` compiles the firmware to native code and runs it in real time.

## What each one can establish

| Question | Established by | Remaining on hardware |
| --- | --- | --- |
| Synthesis, sequencing, UI logic, persistence format | engine tests, host simulator (scripted, repeatable) | — |
| Sound (spectra, envelopes, levels) | WAVs from the engine tests and the simulator | the codec and output stage, speaker |
| The device build boots, draws, plays, reads the panel | fm1-emu (real binary) | the real panel, LEDs |
| CPU load | fm1-emu instruction counts (a lower bound: no cache misses, no flash wait states; the real clock is 240 or 360 MHz, unmeasured) | the real load and xruns, `plat_cpu_pct` on the device |
| Flash writes and their audio gap | the store logic in the simulator | the length of the gap, flash wear |
| USB-MIDI, USB audio, installing and restoring | fm1-base's protocol tests only | everything |
