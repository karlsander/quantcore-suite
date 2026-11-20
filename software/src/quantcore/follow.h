// Copyright 2026 QuantCore contributors. SPDX-License-Identifier: MIT
#pragma once
#include "module.h"

namespace qc {
enum Source : uint8_t { Local, BusScale, BusChord };
enum Options : uint8_t { NoSource, ScaleOnly, ChordOnly, ScaleOrChord };

// One nibble per hemisphere, in the eight spare bits of its preset globals.
struct Routes {
  Options options[2];
  Source source[2] = {Local, Local};

  int lanes() const { return options[1] == NoSource ? 1 : 2; }
  bool wanted() const { return source[0] != Local || source[1] != Local; }
  uint8_t pack() const { return source[0] | (source[1] << 2); }
  void restore(unsigned bits);
  void nudge(int lane, int direction);
};

// Ascending, tuned, in-range notes. Positions and degree offsets index this
// list, so sparse and multi-octave masks retain their original intervals.
struct NoteList {
  uint8_t midi[109]; // inclusive -3..+6 V, 108 semitones
  int count = 0;
  void rebuild(const quantcore::Quantizer& q);
  int firstAt(int identity) const;
  int nearest(int cv, bool measured) const;
  int pitch(int index) const;
  int shifted(int index, int degrees) const;
};

const NoteList& notes(Source source);
uint32_t harmonyRevision();
}
