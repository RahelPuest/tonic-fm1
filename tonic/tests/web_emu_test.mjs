// SPDX-License-Identifier: MPL-2.0
// The browser emulator's WebAssembly, run in Node as the page's worklet runs it: boot, PLAY, a MIDI note, audio and
// screen out.   node tests/web_emu_test.mjs [build/emu/app.wasm]
import { readFileSync } from "node:fs";
const wasm = readFileSync(process.argv[2] || "build/emu/app.wasm");
let mem, t = 0n;
const wasi = new Proxy({ clock_time_get: (id, p, out) => { new DataView(mem.buffer).setBigUint64(out, t, true); return 0; },
                         fd_write: (fd, iov, n, out) => { new DataView(mem.buffer).setUint32(out, 0, true); return 0; } },
                       { get: (o, k) => (k in o ? o[k] : () => 0) });
const env = new Proxy({}, { get: () => () => 0 });
const { instance } = await WebAssembly.instantiate(wasm, { wasi_snapshot_preview1: wasi, env });
const ex = instance.exports;
mem = ex.memory;
if (ex._initialize) ex._initialize();
ex.web_master(3000);
ex.web_boot();
const B_PLAY = 10;
let fails = 0;
function run(ms) {                                  // the device's time moves with the audio, 128 frames at a time
  let peak = 0;
  for (let i = 0; i < Math.round(ms * 44.1 / 128); i++) {
    ex.web_render(128);
    t += 2902494n;
    const l = new Float32Array(mem.buffer, ex.web_out_l(), 128);
    for (const v of l) peak = Math.max(peak, Math.abs(v));
  }
  return peak;
}
function check(ok, what, v) { console.log(`${ok ? "PASS" : "FAIL"} ${what} ${v ?? ""}`); if (!ok) fails++; }
const quiet = run(1500);
check(quiet < 0.01, "boot: quiet", quiet.toFixed(4));
check(ex.web_blits() > 0, "boot: screen drawn", ex.web_blits());
ex.web_buttons(1 << B_PLAY); run(60); ex.web_buttons(0);
const playing = run(2000);
check(playing > 0.02, "PLAY: the demo sounds", playing.toFixed(3));
ex.web_buttons(1 << B_PLAY); run(60); ex.web_buttons(0);
run(2500);
const pkt = (st, d1, d2) => ((st >> 4) | st << 8 | d1 << 16 | d2 << 24) >>> 0;
ex.web_midi_in(0, pkt(0x90, 60, 110));           // a note on channel 1: track 1
const note = run(300);
ex.web_midi_in(0, pkt(0x80, 60, 0));
check(note > 0.01, "MIDI in: a note plays", note.toFixed(3));
const fb = new Uint16Array(mem.buffer, ex.web_fb(), 240 * 240);
let lit = 0;
for (const p of fb) if (p) lit++;
check(lit > 2000, "screen has content", lit);
process.exit(fails ? 1 : 0);
