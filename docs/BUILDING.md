# Building TONIC

TONIC is an app on [fm1-base](https://github.com/charlesvestal/fm1-base) (git submodule `base/`, pinned), which
builds it with felucca-bsp and JieLi's toolchain. Tested on macOS 14 (Apple silicon) on 2026-10-23.

## Get the source

```sh
git clone --recursive <this repository> digitone     # base/ and base/bsp/ are submodules
cd digitone
```

TONIC (0.27) builds on fm1-base `b0976f2` with two changes that are not upstream (the branch `tonic-up` of
[RahelPuest/fm1-base](https://github.com/RahelPuest/fm1-base/tree/tonic-up); the same changes are in
`tonic/tools/base-tonic.patch`):
- C++ o2 units (0.14, for Braids): `.cc` files compiled without exceptions, RTTI or a C++ library.
- `fastmath.h` always inlined: RAM code cannot call XIP functions (they are out of a call's reach); XIP code calls the
  RAM code through a pointer (`ENG_FAR`).

What TONIC 0.13-0.26 added to fm1-base itself is upstream's now: the audio loops in RAM (`.app_ram`, PLAT_RAM_CODE),
one SRAM budget (`ram_layout = "packed"`), the second core (`cpu2`), the stacks' static check (`stack_check`), and the
own Bluetooth stack (`ble_stack = "own"`).

On a fresh clone whose `base/` lacks the commits: `cd base && git checkout b0976f2 && git submodule update bsp &&
git am ../tonic/tools/base-tonic.patch`.

## Prerequisites

| What | Why | How |
| --- | --- | --- |
| Python 3 with Pillow and fontTools | fm1-base renders its fonts at build time | `pip3 install Pillow fonttools` |
| Pillow text shaping (libraqm / libfribidi) | the font generator uses OpenType features | Linux: `apt install libfribidi0`. macOS: build libfribidi (`pip3 install meson ninja`, then `meson setup build --prefix=$HOME/.local/fribidi && meson install -C build` in a fribidi 1.0.x source tree); `tonic/tools/env.sh` points `DYLD_LIBRARY_PATH` at it |
| Docker (macOS) | the JieLi toolchain is Linux x86-64; each tool runs in a `linux/amd64` container | Docker Desktop, running |
| JieLi toolchain | the pi32v2 compiler | `base/tools/get_toolchain.sh` (installs to `~/.jieli/toolchain`) |
| JieLi AC79 SDK | three files used in the package (Apache-2.0) | `git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK` |
| Emscripten (optional) | the browser emulator | `git clone https://github.com/emscripten-core/emsdk ~/emsdk && ~/emsdk/emsdk install latest && ~/emsdk/emsdk activate latest` |
| Node.js (optional) | the emulator's headless test | any recent |
| Rust ≥ 1.95 (optional) | fm1-emu, the instruction-level emulator | `rustup update stable` |

## Build

From `tonic/`:

```sh
. tools/env.sh                                 # toolchain, SDK, fribidi, emsdk
python3 ../base/tools/build.py .               # build/tonic.fwsc (identity FM-1_4700) + build/tonic.bin
python3 ../base/tools/build.py . --release 0.29  # build/tonic-0.29.fwsc (identity FM-1_47002900)
python3 ../base/tools/build.py . --host        # build/host/tonic_host (the simulator)
python3 ../base/tools/build.py . --web         # build/emu/ (the browser emulator)
tools/bench_build.sh                           # build/bench/tonic-bench.fwsc (CPU benchmark firmware)
python3 tools/gen_curves.py                    # regenerate core/curves.h from the OS tables (optional; needs the
                                               # analysis in research/, not in the public repository)
```

The device build checks the memory budget and prints it, e.g.
`image 435848 B; SRAM 482868 B of 491264: RAM code 35088 B, .data+.bss 118856 B, pool 328924 B; free 8396 B (8192 kept spare)`.

The installer site (installer page, emulator, manual, backup page):
`python3 ../base/tools/make_site.py . build/tonic-0.29.fwsc 0.29 OUT_DIR` (needs a release build). Copy the
earlier packages into `OUT_DIR/firmware/` to keep them downloadable (the landing page links `tonic-0.1.fwsc`,
`tonic-0.2.fwsc`, `tonic-0.3.fwsc`, `tonic-0.3.1.fwsc`, `tonic-0.3.2.fwsc`, `tonic-0.4.fwsc`, `tonic-0.4.1.fwsc`, `tonic-0.5.fwsc`, `tonic-0.5.1.fwsc`, `tonic-0.5.2.fwsc`, `tonic-0.6.fwsc`, `tonic-0.6.1.fwsc`, `tonic-0.7.fwsc`, `tonic-0.8.fwsc`, `tonic-0.9.fwsc`, `tonic-0.9.1.fwsc`, `tonic-0.9.2.fwsc`, `tonic-0.10.fwsc`, `tonic-0.10.1.fwsc`, `tonic-0.11.fwsc`, `tonic-0.12.fwsc`, `tonic-0.12.1.fwsc`, `tonic-0.13.fwsc`, `tonic-0.14.fwsc`, `tonic-0.15.fwsc`, `tonic-0.16.fwsc`, `tonic-0.17.fwsc`, `tonic-0.18.fwsc`, `tonic-0.19.fwsc`, `tonic-0.20.fwsc`, `tonic-0.21.fwsc`, `tonic-0.22.fwsc`, `tonic-0.23.fwsc`, `tonic-0.24.fwsc`, `tonic-0.25.fwsc`, `tonic-0.26.fwsc`, `tonic-0.27.fwsc` and `tonic-0.28.fwsc`).

## Tests

```sh
tools/eng_tests.sh                             # the engine tests (with the Braids units) and the golden renders
tools/host_tests.sh                            # every host script (tests/host/*.txt), their expectations
node tests/web_emu_test.mjs
tools/emu_tests.sh                             # the device binary in fm1-emu (see TEST_ENVIRONMENT.md)
build/host/tonic_host tests/host/sounds.txt build/out && python3 tools/sound_report.py build/out/sounds.wav
```

**The documentation's pictures.** `tools/doc_images.sh` makes all of them from the host simulator: the manual's
(`docs/img`, from `tests/host/manual_shots.txt` and the feature tests' set-ups), the ten themes in one picture
(`tests/host/doc_themes.txt`) and the README's `docs/screens.png`. It needs Pillow and fails on any unmet expectation.

**Optimizing without changing the sound.** Two checks that the output stays sample for sample the same:

```sh
GOLDEN=build/test/golden.txt build/test/eng_test build/test golden   # 5 scenes hashed (FM, VA, a kit, a pattern
                                               # with locks, arp, unison, retrig, stealing); the first run writes the
                                               # file, later ones compare. Run it alone: the other tests leave state
tools/perf_probe.sh TAG                        # the device binary in fm1-emu: the demo and the three benchmarks
                                               # (TONIC_BENCH 1 FM, 2 VA, 3 DRUM): CPU figures and audio hashes
python3 tools/pc_profile.py build/tonic.elf PCS.txt   # an fm1-live PC profile by function (see the script)
```

## Layout

| Path | What |
| --- | --- |
| `tonic/fm1.toml`, `tonic/app.h`, `tonic/app.c` | the build settings; the contract with fm1-base; the app's unity file |
| `tonic/core/` | parameters (`tonic.h`, `params.c`), mappings (`tables.c`, generated `curves.h`), patterns and their saved form (`pattern.c`) |
| `tonic/engine/` | the -O2 units: voices (`synth.c`), effects (`fx.c`), sequencer (`seq.c`), render and voice allocation (`engine.c`) |
| `tonic/app/` | storage, factory sounds and the demo, pages, input, screens, MIDI |
| `tonic/web/emu/` | TONIC's part of the emulator page (Web MIDI) |
| `tonic/tests/`, `tonic/tools/` | tests and scripts |
| `research/` | the reference research and the Digitone OS analysis (not in the public repository) |
| `docs/` | design, user guide, features, research, validation, install |
