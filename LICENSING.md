# Licensing and attribution

**TONIC is MPL-2.0** (`LICENSE`): the app (`tonic/`) and the documentation (`docs/`). Every source file carries an `SPDX-License-Identifier: MPL-2.0` header.

## What a TONIC firmware contains

| Part | Author | Licence |
| --- | --- | --- |
| `tonic/` (the app: engine, sequencer, interface, factory sounds and patterns) | TONIC | MPL-2.0 |
| [fm1-base](https://github.com/charlesvestal/fm1-base) (`base/`, submodule): platform layer, build tools, host simulator, browser emulator, installer | Charles Vestal, Leo Kuroshita | MPL-2.0 (see `base/LICENSING.md`) |
| [felucca-bsp](https://github.com/hugelton/felucca-bsp) (`base/bsp/`, submodule): the FM-1 board support | Leo Kuroshita and contributors | MPL-2.0 |
| JieLi AC79 SDK files the package needs (`uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin`) | Zhuhai Jieli Technology | Apache-2.0; read from your SDK checkout at build time, not in this repository |
| Fonts (Barlow Semi Condensed, Terminus) | The Barlow Project Authors; Dimitar Toshkov Zhekov | SIL OFL 1.1 (`base/assets/fonts/`) |

TONIC's build does not enable Bluetooth or Wi-Fi, so none of JieLi's binary radio libraries are linked.

## Elektron

The Digitone is a product of Elektron Music Machines; "Digitone" and "Elektron" are their trademarks, used here only
to describe what TONIC was modelled on. TONIC is not affiliated with or endorsed by Elektron.

**Nothing of Elektron's is redistributed.** No code from the Digitone OS was copied, translated or linked: the OS
files were read to measure behaviour (parameter curves, routings, timing), and TONIC implements that behaviour in its
own code. The tables in `tonic/core/` are TONIC's: the HARM waveforms are regenerated from their measured partial
amplitudes, and the curves in `curves.h` are sparse knot approximations of measured mappings (tens of points instead of
the OS tables). The OS update files, their decoded images, disassembly and table dumps stay on the analyst's machine:
the analysis scripts and notes are not published. The Digitone manuals are not in the repository. The factory sounds and demo patterns are original.

## Credits

- Leo Kuroshita: Felucca and felucca-bsp. Charles Vestal: fm1-base and its tools. keremimo: MIDI clock sync in
  felucca-bsp.
- Joakim Eriksson: [fm1-emu](https://github.com/joakimeriksson/fm1-emu), the instruction-level FM-1 emulator used to
  test TONIC's device binary (not part of the firmware).
- The FM-1 community projects read during research: Felucca, fm1_open, fm1-custom-fw.
