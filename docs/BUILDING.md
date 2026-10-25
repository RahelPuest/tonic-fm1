# Building TONIC

TONIC is an app on [fm1-base](https://github.com/charlesvestal/fm1-base) (git submodule `base/`, pinned), which
builds it with felucca-bsp and JieLi's toolchain. Tested on macOS 14 (Apple silicon) on 2026-10-23.

## Get the source

```sh
git clone --recursive <this repository> digitone     # base/ and base/bsp/ are submodules
cd digitone
```

Since 0.10.1 TONIC needs one change to fm1-base that is not upstream: a `.lowram` section (zeroed RAM in what
`.ram_text` leaves of RAMTEXT, `PLAT_LOWRAM`) that holds the HARM tables and the reverb pre-delay. It is the local
branch `tonic-lowram` of `base/` (commit `cd85753`, on fm1-base `6c0b47e`), and the same change is in
`tonic/tools/base-lowram.patch`: on a fresh clone whose `base/` lacks the commit, run
`cd base && git checkout 6c0b47e && git apply ../tonic/tools/base-lowram.patch`. Without it the linker has no
place for `.lowram` (an orphan section): check that the build's RAM line reads about 68 KB, not 98 KB.

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
python3 ../base/tools/build.py . --release 0.12.1 # build/tonic-0.12.1.fwsc (identity FM-1_47001201)
python3 ../base/tools/build.py . --host        # build/host/tonic_host (the simulator)
python3 ../base/tools/build.py . --web         # build/emu/ (the browser emulator)
tools/bench_build.sh                           # build/bench/tonic-bench.fwsc (CPU benchmark firmware)
python3 tools/gen_curves.py                    # regenerate core/curves.h from the OS tables (optional; needs the
                                               # analysis in research/, not in the public repository)
```

The device build checks the memory budget and prints it, e.g.
`image 174360 B; RAM .data+.bss 84628 B of 98304; pool 334752 B of 344064`.

The installer site (installer page, emulator, manual, backup page):
`python3 ../base/tools/make_site.py . build/tonic-0.12.1.fwsc 0.12.1 OUT_DIR` (needs a release build). Copy the
earlier packages into `OUT_DIR/firmware/` to keep them downloadable (the landing page links `tonic-0.1.fwsc`,
`tonic-0.2.fwsc`, `tonic-0.3.fwsc`, `tonic-0.3.1.fwsc`, `tonic-0.3.2.fwsc`, `tonic-0.4.fwsc`, `tonic-0.4.1.fwsc`, `tonic-0.5.fwsc`, `tonic-0.5.1.fwsc`, `tonic-0.5.2.fwsc`, `tonic-0.6.fwsc`, `tonic-0.6.1.fwsc`, `tonic-0.7.fwsc`, `tonic-0.8.fwsc`, `tonic-0.9.fwsc`, `tonic-0.9.1.fwsc`, `tonic-0.9.2.fwsc`, `tonic-0.10.fwsc`, `tonic-0.10.1.fwsc`, `tonic-0.11.fwsc` and `tonic-0.12.fwsc`).

## Tests

```sh
cc -std=gnu99 -O2 -ffp-contract=off -Icore -Iengine -I../base/include tests/eng_test.c core/*.c engine/*.c \
   -o build/test/eng_test && build/test/eng_test build/test
for t in workflow smoke midi song control roll qol audio va presets perflock ui drums new06 pfx euc08 mod09; do build/host/tonic_host tests/host/$t.txt build/out; done
node tests/web_emu_test.mjs
tools/emu_tests.sh                             # the device binary in fm1-emu (see TEST_ENVIRONMENT.md)
build/host/tonic_host tests/host/sounds.txt build/out && python3 tools/sound_report.py build/out/sounds.wav
```

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
