// Copyright 2026 QuantCore contributors. SPDX-License-Identifier: MIT
#include "follow.h"
#include <algorithm>

namespace qc {
bool haveH, haveR;
quantcore::Harmony harmony;
quantcore::Recall recall;
static quantcore::Quantizer quantizers[3];
static uint32_t revision = 1;
static NoteList scaleNotes, chordNotes;

uint32_t harmonyRevision() { return revision; }
const NoteList& notes(Source source) { return source == BusChord ? chordNotes : scaleNotes; }

constexpr int cvMin = -3 * 1536, cvMax = 6 * 1536;

void Routes::restore(unsigned bits) {
  for (int i = 0; i < 2; ++i, bits >>= 2) {
    Source candidate = static_cast<Source>(bits & 3);
    bool scale = candidate == BusScale && (options[i] == ScaleOnly || options[i] == ScaleOrChord);
    bool chord = candidate == BusChord && (options[i] == ChordOnly || options[i] == ScaleOrChord);
    source[i] = scale || chord ? candidate : Local;
  }
}
void Routes::nudge(int lane, int direction) {
  if (lane < 0 || lane >= lanes() || !direction) return;
  int next = std::max(0, std::min(2, int(source[lane]) + direction));
  if (options[lane] == ChordOnly) next = direction > 0 ? BusChord : Local;
  if (options[lane] == ScaleOnly) next = std::min(next, int(BusScale));
  source[lane] = static_cast<Source>(next);
}

void NoteList::rebuild(const quantcore::Quantizer& q) {
  count = 0;
  // With signed 8-bit cents the complete DAC range intersects only these
  // two cycles of the canonical 96-note pattern (origins -36 and 60).
  for (int origin : {-36, 60}) {
    for (int i = 0; i < q.pattern_size(); ++i) {
      int identity = origin + q.pattern_note(i);
      int cents = (identity - 60) * 100 + harmony.fine_cents;
      if (cents >= -3600 && cents <= 7200) midi[count++] = identity;
    }
  }
}
int NoteList::firstAt(int identity) const {
  return std::lower_bound(midi, midi + count, identity) - midi;
}
int NoteList::nearest(int cv, bool measured) const {
  if (!count) return -1;
  // Compare in exact Q7-cents. A measured input already includes tuning.
  int target = (60 * 128 + cv) * 100 - (measured ? harmony.fine_cents * 128 : 0);
  int upper = firstAt(target / 12800 + (target % 12800 > 0));
  if (!upper) return 0;
  if (upper == count || 2 * target <= (midi[upper-1] + midi[upper]) * 12800) return upper - 1;
  return upper;
}
int NoteList::pitch(int index) const {
  return (int(midi[index]) - 60) * 128 + fineCV();
}
int NoteList::shifted(int index, int degrees) const {
  return pitch(std::max(0, std::min(count - 1, index + degrees)));
}

void releaseState() {
  haveH = haveR = false;
  scaleNotes.count = chordNotes.count = 0;
  harmony.Clear();
  recall.Clear();
  ++revision;
}

void installHarmony(const quantcore::Harmony& next) {
  if (haveH && next.Equals(harmony)) return;
  harmony = next;
  ++revision;
  for (unsigned i = 0; i < 3; ++i)
    quantizers[i].Configure(harmony, static_cast<quantcore::Selection>(i));
  scaleNotes.rebuild(quantizers[quantcore::SCALE]);
  chordNotes.rebuild(quantizers[quantcore::CHORD]);
  haveH = true;
}

int selector(unsigned slot) {
  if (!haveR || !(recall.presence & (1 << slot))) return -1;
  const uint8_t ids[] = {recall.project, recall.program, recall.pattern, recall.snapshot};
  return ids[slot];
}
int fineCV() {
  int cents = harmony.enabled ? harmony.fine_cents : 0;
  return (cents * 128 + (cents < 0 ? -50 : 50)) / 100;
}
int rootCV() {
  return harmony.enabled ? std::max(cvMin, std::min(cvMax, harmony.root * 128 + fineCV())) : 0;
}
int Voice::measured(int input, quantcore::Selection mode) {
  if (revision == qc::revision && lastInput == input && lastMode == mode) return output;
  revision = qc::revision;
  lastInput = input;
  lastMode = mode;
  if (!harmony.enabled) {
    output = input;
  } else {
    constexpr int origin = 60 * 128;
    int32_t rendered;
    if (quantizers[mode].ProcessMeasured(origin + input, origin + cvMin, origin + cvMax, &rendered))
      output = rendered - origin;
  }
  return output;
}

}
