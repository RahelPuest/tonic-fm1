// SPDX-License-Identifier: MPL-2.0
// TONIC's part of the emulator's worklet: the page's MIDI packets into the firmware (web_midi_in, port USB).
globalThis.fm1App = {
  message(fm1, m) {
    if (m.midi !== undefined && fm1.ex.web_midi_in) fm1.ex.web_midi_in(0, m.midi >>> 0);
  },
};
