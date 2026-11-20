// Copyright 2026 QuantCore contributors. SPDX-License-Identifier: MIT
#include "wire.h"
#include <string.h>

namespace qc {
void Settings::defaults() {
  harmony.Clear();
  harmony.enabled = true;
  // C major, repeated through eight octaves.
  for (unsigned i = 0; i < 96; ++i)
    if ((0xab5 >> (i % 12)) & 1) harmony.scale[i / 8] |= 1 << (i % 8);
  for (auto& id : selectors) id = -1;
  bpm = 120;
}

static void checksum(uint8_t* bytes, unsigned size) {
  uint16_t crc = quantcore::Crc16(bytes, size - 2);
  bytes[size - 2] = crc;
  bytes[size - 1] = crc >> 8;
}

void Packet::encode(const Settings& settings, Kind next) {
  kind = next;
  memset(h, 0, sizeof(h));
  const auto& harmony = settings.harmony;
  h[0] = 0x52 | harmony.enabled;
  if (harmony.enabled) {
    memcpy(h + 1, harmony.scale, 12);
    memcpy(h + 13, harmony.chord, 3);
    h[16] = harmony.root;
    h[17] = harmony.fine_cents;
  }
  checksum(h, sizeof(h));
  r[0] = 0x80;
  for (unsigned i = 0; i < 4; ++i) {
    int id = settings.selectors[i];
    r[i + 1] = id < 0 ? 0 : id;
    if (id >= 0) r[0] |= 1 << i;
  }
  checksum(r, sizeof(r));
}

static bool uart(const uint8_t* bytes, unsigned tick) {
  unsigned bit = tick / 5;
  unsigned pos = bit % 10;
  if (pos == 0) return true;
  if (pos == 9) return false;
  return !(bytes[bit / 10] & (1 << (pos - 1)));
}

bool Packet::high(unsigned tick) const {
  if (kind != Stopped) {
    if (tick < 168) return tick < (kind == Reset ? 144u : 120u);
    tick -= 168;
  }
  if (tick < 156) return tick < 12; // 500 us marker + 6 ms guard
  tick -= 156;
  if (tick < 1000) return uart(h, tick);
  tick -= 1000;
  if (tick < 144) return false;
  tick -= 144;
  return tick < 350 && uart(r, tick);
}

void Schedule::init(uint32_t now) {
  *this = Schedule{};
  next = now + 30000;
}

bool Schedule::due(uint32_t now, uint32_t lastStopBitEnd, bool running) {
  if (running != play) {
    play = running;
    restart = true;
    next = now;
  }
  return int32_t(now - next) >= 0 && now - lastStopBitEnd >= 25000 &&
         (!play || !haveBeat || now - lastBeat >= 200000);
}

Packet::Kind Schedule::kind() const {
  return play ? (restart ? Packet::Reset : Packet::Beat) : Packet::Stopped;
}

void Schedule::launched(uint32_t now, uint16_t bpm) {
  if (play) {
    haveBeat = true;
    lastBeat = now;
    restart = false;
  }
  next = now + (play ? 60000000u / bpm : 200000u);
}

Pulse Prefix::sample(bool high, uint32_t us) {
  if (!seen) {
    seen = true;
    previous = high;
    edge = us;
    return NoPulse;
  }
  if (high == previous) return NoPulse;
  uint32_t width = us - edge;
  edge = us;
  previous = high;
  if (high) {
    Pulse result = NoPulse;
    if (candidate == Quarter && width >= 1800 && width <= 2200) result = Quarter;
    if (candidate == Restart && width >= 800 && width <= 1200) result = Restart;
    candidate = NoPulse;
    armed = width >= 20000;
    return result;
  }
  if (armed && width >= 4750 && width <= 5250) candidate = Quarter;
  if (armed && width >= 5750 && width <= 6250) candidate = Restart;
  armed = false;
  return NoPulse;
}
}
