// Copyright 2026 QuantCore contributors.
// SPDX-License-Identifier: MIT
// Portable QuantCore 1.0 records and twelve-tone pitch semantics.

#ifndef BRAIDS_QUANTCORE_CORE_H_
#define BRAIDS_QUANTCORE_CORE_H_

#include <stddef.h>
#include <stdint.h>

namespace quantcore {

enum Domain {
  NONE = 0,
  HARMONY = 1,
  RECALL = 2,
  ALL_DOMAINS = HARMONY | RECALL
};

struct Harmony {
  bool enabled;
  uint8_t scale[12];
  uint8_t chord[3];
  int8_t root;
  int8_t fine_cents;

  void Clear();
  bool Equals(const Harmony& other) const;
};

struct Recall {
  uint8_t presence;
  uint8_t project;
  uint8_t program;
  uint8_t pattern;
  uint8_t snapshot;

  void Clear();
  bool program_present() const { return (presence & 2) != 0; }
};

uint16_t Crc16Update(uint16_t crc, uint8_t byte);
uint16_t Crc16(const uint8_t* bytes, size_t size);
bool DecodeHarmony(const uint8_t* bytes, size_t size, Harmony* state);
bool DecodeRecall(const uint8_t* bytes, size_t size, Recall* state);

// Incremental burst parser. Finish() is called ONLY at a physical LOW
// delimiter, never just because a CRC byte has arrived. Its returned bytes
// remain valid until Reset()/AddByte(). Unknown/error/excess bytes poison the
// complete burst. No search for an interior header is performed.
class BurstDecoder {
 public:
  void Init(uint8_t subscriptions = ALL_DOMAINS);
  void Reset();
  void AddByte(uint8_t byte);
  void FramingError() { poisoned_ = true; }
  uint8_t Finish() const;
  const uint8_t* bytes() const { return bytes_; }
  bool has_input() const { return count_ != 0 || poisoned_; }

 private:
  uint8_t bytes_[20];
  uint8_t count_;
  uint8_t expected_;
  uint8_t domain_;
  uint8_t subscriptions_;
  uint16_t crc_;
  bool poisoned_;
};

enum Selection {
  SCALE,
  CHORD,
  INTERSECTION
};

// Foreground-only quantizer. Configure precomputes a sorted pattern; subsequent
// queries use binary searches instead of scanning the mask in an audio ISR.
// Pitches/bounds are MIDI-coordinate semitones * 128 (Q7). Fine tuning is used
// at full cent precision for membership/boundary decisions, and the rendered
// output is rounded once to the nearest Q7 unit (max error 0.390625 cent).
// ROOT never translates either mask. Caller handles disabled harmony locally;
// false from Process... on enabled harmony means NO_NOTE.
class Quantizer {
 public:
  void Init();
  void Configure(const Harmony& harmony, Selection selection = SCALE);
  bool enabled() const { return enabled_; }
  bool empty() const { return count_ == 0; }
  bool Contains(int32_t identity) const;
  bool NextIdentity(int32_t at_or_above, int32_t* identity) const;
  bool PreviousIdentity(int32_t at_or_below, int32_t* identity) const;
  int32_t RenderedPitch(int32_t identity) const;
  int32_t fine_pitch() const;
  bool ProcessMeasured(int32_t input, int32_t lower, int32_t upper,
                       int32_t* output, int32_t* identity = NULL) const;
  bool ProcessLogical(int32_t input, int32_t lower, int32_t upper,
                      int32_t* output, int32_t* identity = NULL) const;

 private:
  bool Process(int32_t input, int32_t lower, int32_t upper, bool measured,
               int32_t* output, int32_t* identity) const;
  uint8_t LowerBound(uint8_t note) const;
  bool enabled_;
  int8_t fine_cents_;
  uint8_t count_;
  uint8_t notes_[96];
};

}  // namespace quantcore

#endif  // BRAIDS_QUANTCORE_CORE_H_
