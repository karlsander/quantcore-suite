// Copyright 2026 QuantCore contributors. SPDX-License-Identifier: MIT
#pragma once
#include "core.h"

namespace qc {
struct Settings {
  quantcore::Harmony harmony;
  // -1 releases a slot. IDs 0..255 are all usable.
  int16_t selectors[4];
  uint16_t bpm;
  void defaults();
};

// One transaction on a 24 kHz timebase. Each inverted 8N1 bit lasts 5 ticks.
struct Packet {
  uint8_t h[20], r[7];
  enum Kind { Stopped, Beat, Reset } kind;
  void encode(const Settings& settings, Kind next);
  unsigned length() const { return (kind == Stopped ? 0 : 168) + 1650; }
  bool high(unsigned tick) const;
};

// Times are transaction starts (7 ms before a musical boundary).
class Schedule {
 public:
  void init(uint32_t now);
  bool due(uint32_t now, uint32_t lastStopBitEnd, bool running);
  Packet::Kind kind() const;
  void launched(uint32_t now, uint16_t bpm);
  void retryLater(uint32_t now) { next = now + 200000; }
 private:
  uint32_t next = 0, lastBeat = 0;
  bool play = false, restart = true, haveBeat = false;
};

enum Pulse { NoPulse, Quarter, Restart };
class Prefix {
 public:
  Pulse sample(bool high, uint32_t us);
  void clear() { *this = Prefix{}; }
 private:
  uint32_t edge = 0;
  bool seen = false, previous = false, armed = false;
  Pulse candidate = NoPulse;
};
}
