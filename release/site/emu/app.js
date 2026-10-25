// SPDX-License-Identifier: MPL-2.0
// TONIC's part of the emulator page: Web MIDI in. Every input port's channel messages go to the firmware as they
// would come over USB (notes on channels 1-4 play tracks 1-4, the auto channel the active track; the Digitone's CC
// numbers move the sound; program change picks a pattern; clock syncs it when CLK IN is on).
let node = null;
function packet(d) {                       // a USB-MIDI event packet: cable 0, CIN from the status
  const st = d[0], cin = st >= 0xF8 ? 0x0F : st >> 4;
  return (cin | st << 8 | (d[1] || 0) << 16 | (d[2] || 0) << 24) >>> 0;
}
export async function attach(ctx) {
  node = ctx.node;
  if (!navigator.requestMIDIAccess) { ctx.status("No Web MIDI in this browser: the panel and the computer keyboard still play."); return; }
  try {
    const access = await navigator.requestMIDIAccess({ sysex: false });
    const hook = () => {
      let n = 0;
      for (const input of access.inputs.values()) {
        n++;
        input.onmidimessage = (e) => {
          const d = e.data;
          if (!d.length || d[0] === 0xFE || (d[0] >= 0xF0 && d[0] < 0xF8)) return;   // no active sensing, no SysEx
          node.port.postMessage({ type: "app", midi: packet(d) });
        };
      }
      ctx.status(n ? `MIDI in: ${n} port${n > 1 ? "s" : ""} connected.` : "No MIDI input connected.");
    };
    access.onstatechange = hook;
    hook();
  } catch (err) {
    ctx.status("MIDI not allowed: " + (err && err.message || err));
  }
}
export function message() {}
