// Copyright 2026 QuantCore contributors.
// SPDX-License-Identifier: MIT

#include "core.h"

#include <limits.h>
#include <string.h>

namespace quantcore {
namespace {

int64_t FloorDiv(int64_t value, int64_t divisor) {
  // All practical Braids pitch coordinates use this inexpensive 32-bit path.
  // Keep the wide fallback so the public API has defined behavior even for
  // extreme signed inputs; avoid software 64-bit division in the audio loop.
  if (value >= INT32_MIN && value <= INT32_MAX && divisor <= INT32_MAX) {
    int32_t v = static_cast<int32_t>(value);
    int32_t d = static_cast<int32_t>(divisor);
    int32_t quotient = v / d;
    if (v % d < 0) --quotient;
    return quotient;
  }
  int64_t quotient = value / divisor;
  if (value % divisor < 0) --quotient;
  return quotient;
}

int64_t CeilDiv(int64_t value, int64_t divisor) {
  return -FloorDiv(-value, divisor);
}

int8_t SignedByte(uint8_t value) {
  return static_cast<int8_t>(value < 128 ? value : int16_t(value) - 256);
}

bool Canonical(const uint8_t* bytes, uint8_t domain) {
  if (domain == HARMONY) {
    if (bytes[0] == 0x52) {
      for (uint8_t i = 1; i != 18; ++i) {
        if (bytes[i] != 0) return false;
      }
    }
    return true;
  }
  for (uint8_t i = 0; i != 4; ++i) {
    if (!(bytes[0] & (1 << i)) && bytes[i + 1] != 0) return false;
  }
  return true;
}

}  // namespace

void Harmony::Clear() { memset(this, 0, sizeof(*this)); }

bool Harmony::Equals(const Harmony& other) const {
  return enabled == other.enabled && root == other.root &&
      fine_cents == other.fine_cents &&
      memcmp(scale, other.scale, sizeof(scale)) == 0 &&
      memcmp(chord, other.chord, sizeof(chord)) == 0;
}

void Recall::Clear() { memset(this, 0, sizeof(*this)); }

uint16_t Crc16Update(uint16_t crc, uint8_t byte) {
  crc ^= static_cast<uint16_t>(byte) << 8;
  for (uint8_t i = 0; i != 8; ++i) {
    crc = crc & 0x8000 ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                       : static_cast<uint16_t>(crc << 1);
  }
  return crc;
}

uint16_t Crc16(const uint8_t* bytes, size_t size) {
  uint16_t crc = 0xffff;
  while (size--) crc = Crc16Update(crc, *bytes++);
  return crc;
}

bool DecodeHarmony(const uint8_t* bytes, size_t size, Harmony* state) {
  if (size != 20 || (bytes[0] != 0x52 && bytes[0] != 0x53) ||
      !Canonical(bytes, HARMONY) ||
      Crc16(bytes, 18) != (bytes[18] | (uint16_t(bytes[19]) << 8))) {
    return false;
  }
  state->enabled = bytes[0] == 0x53;
  memcpy(state->scale, bytes + 1, 12);
  memcpy(state->chord, bytes + 13, 3);
  state->root = SignedByte(bytes[16]);
  state->fine_cents = SignedByte(bytes[17]);
  return true;
}

bool DecodeRecall(const uint8_t* bytes, size_t size, Recall* state) {
  if (size != 7 || (bytes[0] & 0xf0) != 0x80 ||
      !Canonical(bytes, RECALL) ||
      Crc16(bytes, 5) != (bytes[5] | (uint16_t(bytes[6]) << 8))) {
    return false;
  }
  state->presence = bytes[0] & 0x0f;
  state->project = bytes[1];
  state->program = bytes[2];
  state->pattern = bytes[3];
  state->snapshot = bytes[4];
  return true;
}

void BurstDecoder::Init(uint8_t subscriptions) {
  subscriptions_ = subscriptions & ALL_DOMAINS;
  Reset();
}

void BurstDecoder::Reset() {
  count_ = expected_ = domain_ = 0;
  poisoned_ = false;
  crc_ = 0xffff;
}

void BurstDecoder::AddByte(uint8_t byte) {
  if (poisoned_) return;
  if (count_ == 0) {
    if (byte == 0x52 || byte == 0x53) {
      domain_ = HARMONY;
      expected_ = 20;
    } else if ((byte & 0xf0) == 0x80) {
      domain_ = RECALL;
      expected_ = 7;
    }
    if (!(domain_ & subscriptions_)) {
      poisoned_ = true;
      return;
    }
  }
  if (count_ >= expected_) {
    poisoned_ = true;
    return;
  }
  // Validate canonical payload incrementally. In particular, do not scan a
  // disabled H's seventeen zero bytes at the terminal delimiter, when the
  // same audio ISR also has to publish its mailbox.
  if (byte != 0 && count_ != 0) {
    if ((domain_ == HARMONY && bytes_[0] == 0x52 && count_ < 18) ||
        (domain_ == RECALL && count_ <= 4 &&
         !(bytes_[0] & (1 << (count_ - 1))))) {
      poisoned_ = true;
      return;
    }
  }
  bytes_[count_] = byte;
  if (count_ < expected_ - 2) crc_ = Crc16Update(crc_, byte);
  ++count_;
}

uint8_t BurstDecoder::Finish() const {
  if (poisoned_ || expected_ == 0 || count_ != expected_) return NONE;
  if (crc_ != (bytes_[expected_ - 2] |
               (uint16_t(bytes_[expected_ - 1]) << 8))) return NONE;
  return domain_;
}

void Quantizer::Init() {
  enabled_ = false;
  fine_cents_ = 0;
  count_ = 0;
}

void Quantizer::Configure(const Harmony& harmony, Selection selection) {
  enabled_ = harmony.enabled;
  fine_cents_ = harmony.fine_cents;
  count_ = 0;
  if (!enabled_) return;
  const bool chord_present = (harmony.chord[0] | harmony.chord[1] |
                              harmony.chord[2]) != 0;
  if (!chord_present) selection = SCALE;
  for (uint8_t i = 0; i != 96; ++i) {
    const bool in_scale = (harmony.scale[i >> 3] & (1 << (i & 7))) != 0;
    const uint8_t chord_i = i % 24;
    const bool in_chord =
        (harmony.chord[chord_i >> 3] & (1 << (chord_i & 7))) != 0;
    const bool allowed = selection == SCALE ? in_scale :
        (selection == CHORD ? in_chord : in_scale && in_chord);
    if (allowed) notes_[count_++] = i;
  }
}

uint8_t Quantizer::LowerBound(uint8_t note) const {
  uint8_t lo = 0;
  uint8_t hi = count_;
  while (lo < hi) {
    uint8_t mid = lo + ((hi - lo) >> 1);
    if (notes_[mid] < note) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

bool Quantizer::Contains(int32_t identity) const {
  if (count_ == 0) return false;
  int64_t relative = int64_t(identity) - 60;
  uint8_t offset = static_cast<uint8_t>(relative - FloorDiv(relative, 96) * 96);
  uint8_t index = LowerBound(offset);
  return index != count_ && notes_[index] == offset;
}

bool Quantizer::NextIdentity(int32_t at_or_above, int32_t* identity) const {
  if (count_ == 0) return false;
  int64_t relative = int64_t(at_or_above) - 60;
  int64_t cycle = FloorDiv(relative, 96);
  uint8_t offset = static_cast<uint8_t>(relative - cycle * 96);
  uint8_t index = LowerBound(offset);
  if (index == count_) {
    index = 0;
    ++cycle;
  }
  int64_t result = 60 + 96 * cycle + notes_[index];
  if (result > INT32_MAX) return false;
  *identity = static_cast<int32_t>(result);
  return true;
}

bool Quantizer::PreviousIdentity(int32_t at_or_below, int32_t* identity) const {
  if (count_ == 0) return false;
  int64_t relative = int64_t(at_or_below) - 60;
  int64_t cycle = FloorDiv(relative, 96);
  uint8_t offset = static_cast<uint8_t>(relative - cycle * 96);
  uint8_t index = LowerBound(offset);
  if (index == count_ || notes_[index] != offset) {
    if (index == 0) {
      --cycle;
      index = count_;
    }
    --index;
  }
  int64_t result = 60 + 96 * cycle + notes_[index];
  if (result < INT32_MIN) return false;
  *identity = static_cast<int32_t>(result);
  return true;
}

int32_t Quantizer::fine_pitch() const {
  // Fine is a signed byte: |value| <= 16384. Keep this 32-bit so rendering
  // each voice never invokes a software 64-bit division on the Cortex-M3.
  const int32_t value = int32_t(fine_cents_) * 128;
  return value < 0 ? -((-value + 50) / 100) : (value + 50) / 100;
}

int32_t Quantizer::RenderedPitch(int32_t identity) const {
  // The caller is responsible for using a representable final identity.
  return static_cast<int32_t>(int64_t(identity) * 128 + fine_pitch());
}

bool Quantizer::ProcessMeasured(int32_t input, int32_t lower, int32_t upper,
                                int32_t* output, int32_t* identity) const {
  return Process(input, lower, upper, true, output, identity);
}

bool Quantizer::ProcessLogical(int32_t input, int32_t lower, int32_t upper,
                               int32_t* output, int32_t* identity) const {
  return Process(input, lower, upper, false, output, identity);
}

bool Quantizer::Process(int32_t input, int32_t lower, int32_t upper,
                        bool measured, int32_t* output, int32_t* identity) const {
  if (count_ == 0 || lower > upper) return false;
  const int64_t fine = int64_t(fine_cents_) * 128;
  const int32_t minimum = static_cast<int32_t>(
      CeilDiv(int64_t(lower) * 100 - fine, 12800));
  const int32_t maximum = static_cast<int32_t>(
      FloorDiv(int64_t(upper) * 100 - fine, 12800));
  if (minimum > maximum) return false;
  const int64_t target = int64_t(input) * 100 - (measured ? fine : 0);
  int32_t below = static_cast<int32_t>(FloorDiv(target, 12800));
  int32_t above = static_cast<int32_t>(CeilDiv(target, 12800));
  if (below > maximum) below = maximum;
  if (above < minimum) above = minimum;
  bool have_below = PreviousIdentity(below, &below) && below >= minimum;
  bool have_above = NextIdentity(above, &above) && above <= maximum;
  if (!have_below && !have_above) return false;
  int32_t selected;
  if (!have_above) selected = below;
  else if (!have_below) selected = above;
  else selected = target - int64_t(below) * 12800 <=
                  int64_t(above) * 12800 - target ? below : above;
  *output = RenderedPitch(selected);
  if (identity) *identity = selected;
  return true;
}

}  // namespace quantcore
