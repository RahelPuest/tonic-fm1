// SPDX-License-Identifier: MPL-2.0
// TONIC's BRAIDS engine [0.14]: the C interface to Braids' MacroOscillator (braids_voice.h)
#include "braids/macro_oscillator.h"
#include "braids_voice.h"
namespace braids {                                // (the signature's abs(): freestanding, no <stdlib.h>)
static inline int32_t abs(int32_t x) { return x < 0 ? -x : x; }
}
#include "braids/signature_waveshaper.h"

using namespace braids;

static inline MacroOscillator* osc(void* st) { return reinterpret_cast<MacroOscillator*>(st); }

extern "C" unsigned br_state_size(void) { return (sizeof(MacroOscillator) + 3u) & ~3u; }
extern "C" unsigned br_delay_size(void) { return (sizeof(DelayLines) + 3u) & ~3u; }

extern "C" void br_init(void* st) {
  osc(st)->Init();
  osc(st)->set_delay_lines(0);
}

extern "C" void br_set_delay(void* st, void* delay) {
  osc(st)->set_delay_lines(reinterpret_cast<DelayLines*>(delay));
}

extern "C" int br_needs_delay(int model) {
  switch (model) {
    case MACRO_OSC_SHAPE_SAW_COMB:
    case MACRO_OSC_SHAPE_PLUCKED:
    case MACRO_OSC_SHAPE_BOWED:
    case MACRO_OSC_SHAPE_BLOWN:
    case MACRO_OSC_SHAPE_FLUTED:
      return 1;
    default:
      return 0;
  }
}

extern "C" void br_strike(void* st) { osc(st)->Strike(); }

extern "C" void br_render(void* st, int model, int pitch, int timbre, int color, const uint8_t* sync, int16_t* out,
                          int n) {
  static const uint8_t no_sync[24] = { 0 };
  MacroOscillator* o = osc(st);
  if (model < 0) model = 0;
  if (model >= BR_NMODEL) model = BR_NMODEL - 1;
  if (pitch < 0) pitch = 0;
  if (pitch > 32767) pitch = 32767;
  o->set_shape(static_cast<MacroOscillatorShape>(model));
  o->set_pitch(static_cast<int16_t>(pitch));
  o->set_parameters(static_cast<int16_t>(timbre), static_cast<int16_t>(color));
  while (n > 0) {                                 // (Braids renders up to 24 samples at a time)
    int k = n > 24 ? 24 : n;
    o->Render(sync ? sync : no_sync, out, static_cast<size_t>(k));
    out += k;
    if (sync) sync += k;
    n -= k;
  }
}

extern "C" unsigned br_shaper_size(void) { return (sizeof(SignatureWaveshaper) + 3u) & ~3u; }
extern "C" void br_shaper_init(void* ws, uint32_t seed) { reinterpret_cast<SignatureWaveshaper*>(ws)->Init(seed); }
extern "C" void br_shape(const void* ws, int16_t* buf, int n, int amount) {
  SignatureWaveshaper* w = const_cast<SignatureWaveshaper*>(reinterpret_cast<const SignatureWaveshaper*>(ws));
  for (int i = 0; i < n; ++i) {
    int32_t t = w->Transform(buf[i]);
    if (t > 32767) t = 32767;
    if (t < -32768) t = -32768;
    buf[i] = stmlib::Mix(buf[i], static_cast<int16_t>(t), static_cast<uint16_t>(amount));
  }
}
