// Copyright 2026 QuantCore contributors. SPDX-License-Identifier: MIT
#include "module.h"
#include "qwire.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <initializer_list>

using namespace qc;

static void scheduling(uint32_t start) {
  Schedule s;
  s.init(start);
  assert(!s.due(start + 24000, start, true));
  assert(s.due(start + 30000, start, false));
  assert(s.kind() == Packet::Stopped);
  s.launched(start + 30000, 120);
  assert(!s.due(start + 229999, start + 98750, false));
  assert(s.due(start + 230000, start + 98750, false));
  s.launched(start + 230000, 120);
  // A Start arrives during a stopped publication. Hardware completes it before polling.
  uint32_t end = start + 230000 + 68750;
  assert(!s.due(end + 24999, end, true));
  uint32_t first = end + 25000;
  assert(s.due(first, end, true) && s.kind() == Packet::Reset);
  s.launched(first, 300);
  end = first + 75750;
  assert(!s.due(first + 199999, end, true));
  assert(s.due(first + 200000, end, true) && s.kind() == Packet::Beat);
  s.launched(first + 200000, 120);
  end = first + 275750;
  assert(s.due(end + 25000, end, false));
  assert(!s.due(first + 399999, end, true));
  assert(s.due(first + 400000, end, true) && s.kind() == Packet::Reset);
}

static void wave(const Packet& p, unsigned phase, unsigned expected) {
  // Simulate the 32 kHz input sampler and an independent microsecond clock.
  quantcore::QWireReceiver rx;
  rx.Init();
  Prefix prefix;
  unsigned clocks = 0, resets = 0;
  for (unsigned t = phase; t < 2400 + p.length() * 4 + 720; t += 3) {
    bool high = t >= 2400 && p.high((t - 2400) / 4);
    rx.Sample(high, t);
    Pulse pulse = prefix.sample(high, uint32_t(0xffff8000u + t * 125 / 12));
    if (pulse != NoPulse) {
      assert(t >= 2400 + 672 && t <= 2400 + 674);
      ++clocks;
      resets += pulse == Restart;
    }
  }
  assert(clocks == unsigned(p.kind != Packet::Stopped));
  assert(resets == unsigned(p.kind == Packet::Reset));
  quantcore::Harmony h, expectedH;
  quantcore::Recall r;
  assert(rx.TakeUpdates(&h, &r) == expected);
  if (expected & quantcore::HARMONY) {
    assert(quantcore::DecodeHarmony(p.h, 20, &expectedH));
    assert(h.Equals(expectedH));
  }
  if (expected & quantcore::RECALL) assert(r.presence == 5 && r.project == 0 && r.pattern == 255);
}

static void outputs() {
  Settings s;
  s.defaults();
  Voice voice;
  releaseState();
  assert(voice.measured(127, quantcore::SCALE) == 127);
  s.harmony.root = 2;
  s.harmony.fine_cents = 25;
  s.harmony.chord[0] = 0x91;
  installHarmony(s.harmony);
  assert(rootCV() == 288 && fineCV() == 32);
  assert(voice.measured(128, quantcore::SCALE) == 32);
  assert(voice.measured(12 * 128, quantcore::CHORD) == 7 * 128 + 32);
  // Re-evaluate an unchanged input on a new publication.
  s.harmony.fine_cents = -25;
  installHarmony(s.harmony);
  assert(voice.measured(12 * 128, quantcore::CHORD) == 7 * 128 - 32);
  memset(s.harmony.scale, 0, 12);
  installHarmony(s.harmony);
  assert(voice.measured(0, quantcore::INTERSECTION) == 7 * 128 - 32);
  s.harmony.root = 127;
  installHarmony(s.harmony);
  assert(rootCV() == 6 * 1536);
  assert(voice.measured(6 * 1536, quantcore::CHORD) <= 6 * 1536);
  recall = {5, 0, 0, 255, 0};
  haveR = true;
  assert(selector(0) == 0 && selector(1) == -1 && selector(2) == 255 && selector(3) == -1);
  releaseState();
  assert(rootCV() == 0 && selector(2) == -1);
  assert(voice.measured(0, quantcore::INTERSECTION) == 0);
}

int main() {
  outputs();
  scheduling(1000);
  scheduling(0xffff0000);
  Settings s;
  s.defaults();
  s.harmony.chord[0] = 0x91;
  s.harmony.root = 2;
  s.harmony.fine_cents = -25;
  s.selectors[0] = 0;
  s.selectors[2] = 255;
  Packet p;
  const uint8_t h[] = {0x53,0xb5,0x5a,0xab,0xb5,0x5a,0xab,0xb5,0x5a,0xab,0xb5,0x5a,0xab,0x91,0,0,2,0xe7,0xa7,0x9d};
  const uint8_t r[] = {0x85,0,0,0xff,0,0x74,0x13};
  for (auto kind : {Packet::Stopped, Packet::Beat, Packet::Reset}) {
    p.encode(s, kind);
    assert(!memcmp(h, p.h, sizeof(h)) && !memcmp(r, p.r, sizeof(r)));
    for (unsigned phase = 0; phase < 3; ++phase) wave(p, phase, quantcore::ALL_DOMAINS);
    for (unsigned bit = 0; bit < 27 * 8; ++bit) {
      Packet bad = p;
      if (bit < 160) bad.h[bit / 8] ^= 1 << (bit % 8);
      else bad.r[(bit - 160) / 8] ^= 1 << (bit % 8);
      wave(bad, bit % 3, bit < 160 ? quantcore::RECALL : quantcore::HARMONY);
    }
  }
  s.harmony.enabled = false;
  p.encode(s, Packet::Stopped);
  quantcore::Harmony disabled;
  assert(quantcore::DecodeHarmony(p.h, 20, &disabled) && !disabled.enabled);
  Prefix prefix;
  prefix.sample(false, 0);
  prefix.sample(true, 25000);
  prefix.sample(false, 31000);
  assert(prefix.sample(true, 33000) == NoPulse); // reset HIGH + ordinary LOW
  puts("Suite waveform, scheduling, canonical records and domain isolation passed.");
}
