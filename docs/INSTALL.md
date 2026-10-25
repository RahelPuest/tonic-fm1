# Installing TONIC, and going back to the stock firmware

> **Status:** TONIC has **not yet been installed on a physical FM-1** by its authors. The package is built by
> fm1-base's toolchain and has run in the instruction-level emulator (fm1-emu), but the install path itself (USB-MIDI
> update, the loader, the flash write) is exercised only by fm1-base's protocol tests, not on hardware. Install at your
> own risk, and take the backup in step 0 first.

The procedures below are those of the fm1-base/felucca-bsp update tools that TONIC uses unchanged; every step is
traced to code in the research notes (`research/fm1/INSTALL_RESTORE.md`, with file and line references; not in the
public repository).

## What you need

- An M-VAVE FM-1 and a USB **data** cable. Close every other program that uses MIDI.
- Either **Chrome or Edge** (Web MIDI with SysEx) for the web installer, or **Python 3** with
  `pip3 install mido python-rtmidi` for the command line.
- The package: **`release/tonic-0.12.fwsc`** (identity `FM-1_47001200`, SHA-256 in `release/SHA256SUMS`; the earlier
  `release/tonic-0.1.fwsc` … `tonic-0.11.fwsc` stay beside it and install the same way), or your
  own build `tonic/build/tonic.fwsc` (identity `FM-1_4700`). The installer refuses a package for another model.
- For the way back: M-VAVE's **V15** package, `FM-1_015.fwsc`, SHA-256
  `db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a` (699,956 bytes). It is in M-VAVE's own
  repository (`m-vave/fm1_open`, `sdk/firmware/FM-1_015.fwsc`). Since 2026-10-21 M-VAVE's download URL serves V16
  instead, which most community tools refuse; keep V15.

## 0. Before the first install: a full backup (recommended)

TONIC's saved work lives in the flash area that the stock firmware uses for its own settings and patches. Going back
to stock later does not bring your old stock patches back. The only way to keep them is a dump of the whole 1 MiB
flash, taken before the first custom install, with the FM-1 Transporter or MvaveFM1Unbricker (`fm1_unbrick.py
backup`).

## 1. Install

The FM-1 must be running a firmware with the update entry: the stock firmware, or a Felucca-family or fm1-base
firmware. The community's advice is to go back to stock before installing a different custom firmware.

**Command line** (from the repository root):

```sh
python3 base/tools/fm1_install.py --info                 # the connected FM-1's identity
python3 base/tools/fm1_install.py release/tonic-0.12.fwsc # check, stage, write, verify
```

**Web installer:** serve the site and open it from `localhost` (Web MIDI needs a secure context):

```sh
cd release/site && python3 -m http.server 8000             # the prebuilt site (or make your own:
#   python3 base/tools/make_site.py tonic tonic/build/tonic-0.12.fwsc 0.12 /tmp/tonic-site)
# open http://localhost:8000/ in Chrome or Edge, connect the FM-1, press Install
```

What happens (both ways): the host sends the handshake and checks the model; the running firmware pulls the package
over USB-MIDI, checks it, and stages the update loader at 0xE0000; it restarts into the loader, which checks the chip
key and the whole package's CRC before it erases anything, then writes only the app area 0x4000–0x92FFF, verifying
each sector, and restarts. A power cut during the write leaves the update record in place; running the installer
again finishes it. Do not unplug while the screen shows the update.

On the first start TONIC writes its demo project (four patterns, a sound pool) to the flash; this takes a moment.

## 2. Updating TONIC

Install the new `tonic.fwsc` the same way, from TONIC itself. TONIC's project (patterns, sounds, settings) is kept: the
update writes only the app area.

**0.12 changes the saved format** (pattern pack version 4, sound banks version 4). 0.12 reads every older project
and converts it, but once 0.12 has saved, **an older firmware no longer reads the patterns and the sound library**
(they come up empty). Take a USB backup before updating if you may want to go back.

A USB backup of the project, before an update: `python3 base/tools/fm1_backup.py backup FILE` (fm1-base's backup tool;
`restore FILE` puts it back; stop the sequencer first).

## 3. Back to the stock firmware

From TONIC (running normally or in its SAFE MODE):

```sh
python3 base/tools/fm1_install.py FM-1_015.fwsc
```

or the web installer's "back to the stock firmware" button. TONIC's update entry stages M-VAVE's own loader, which
writes the complete stock image including its boot head.

Afterwards, on stock: the factory voices are back (TONIC never writes the factory-voice area 0xEA000–0xFBFFF), but the
stock **patch and settings store will read TONIC's data as garbage** (it shares the area 0x97000–0xE4FFF). Run
**System > Reset Patches** on the stock firmware; if presets still look wrong, use M-VAVE's workbench
(open.m-vave.com/fm1-workbench) factory-voice restore. Your own pre-TONIC stock patches come back only from the
backup of step 0.

## 4. If the FM-1 no longer starts

- fm1-base's boot guard: two failed starts in a row bring up SAFE MODE (USB on: install again from there); four bring
  up the chip's own update mode (the computer sees `WL82 UBOOT1.00`, USB 4C4A:8057).
- From `WL82 UBOOT1.00`: `base/bsp/tools/fm1_rescue.py` (macOS, `sudo`, pyusb) or MvaveFM1Unbricker
  (`fm1_unbrick.py restore FM-1_015.fwsc`, Windows/Linux) write the stock app area back. Both require V15.
- Nothing enumerates at all: the FM-1 Transporter (an RP2040 on the USB data lines) forces the ROM's update mode.

These recovery routes are documented and used by the community; none has been exercised with TONIC.
