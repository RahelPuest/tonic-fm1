// Copyright 2012 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Fast 16-bit pseudo random number generator.

#ifndef STMLIB_UTILS_RANDOM_H_
#define STMLIB_UTILS_RANDOM_H_

#include "stmlib/stmlib.h"

namespace stmlib {

#if !(defined(PLAT_HOST) || defined(__APPLE__) || defined(__EMSCRIPTEN__) || defined(__linux__) || defined(_WIN32))
extern "C" uint32_t fm1_cpu1_ustack[], fm1_cpu1_sstack_top[];
#endif

class Random {
 public:
  // TONIC [0.16]: a state per core (the second core renders voices too): which one, by the stack this runs on
  // (0.27: the platform's second core's, felucca-bsp fm1_cpu1.S)
  static inline uint32_t& rs() {
#if defined(PLAT_HOST) || defined(__APPLE__) || defined(__EMSCRIPTEN__) || defined(__linux__) || defined(_WIN32)
    return rng_state_[0];
#else
    volatile char here;
    uintptr_t a = reinterpret_cast<uintptr_t>(&here), lo = reinterpret_cast<uintptr_t>(fm1_cpu1_ustack);
    return rng_state_[a - lo < reinterpret_cast<uintptr_t>(fm1_cpu1_sstack_top) - lo];
#endif
  }
  static inline uint32_t state() { return rs(); }

  static inline void Seed(uint32_t seed) {
    rs() = seed;
  }

  static inline uint32_t GetWord() {
    uint32_t& s = rs();
    s = s * 1664525L + 1013904223L;
    return s;
  }
  
  static inline int16_t GetSample() {
    return static_cast<int16_t>(GetWord() >> 16);
  }

  static inline float GetFloat() {
    return static_cast<float>(GetWord()) / 4294967296.0f;
  }

 private:
  static uint32_t rng_state_[2];

  DISALLOW_COPY_AND_ASSIGN(Random);
};

}  // namespace stmlib

#endif  // STMLIB_UTILS_RANDOM_H_
