// Copyright 2026 QuantCore contributors. SPDX-License-Identifier: MIT
#include "follow.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>

using namespace qc;

static void routing() {
  for (Options first : {ScaleOnly, ChordOnly, ScaleOrChord}) {
    for (Options second : {NoSource, ScaleOrChord}) {
      Routes routes{{first, second}};
      assert(!routes.wanted() && routes.pack() == 0);
      assert(routes.lanes() == (second == NoSource ? 1 : 2));
      for (unsigned bits = 0; bits < 16; ++bits) {
        routes.restore(bits);
        for (int lane = 0; lane < 2; ++lane) {
          int value = bits >> (2 * lane) & 3;
          Options opt = routes.options[lane];
          bool allowed = (value == BusChord && (opt == ScaleOrChord || opt == ChordOnly)) ||
                         (value == BusScale && (opt == ScaleOrChord || opt == ScaleOnly));
          assert(routes.source[lane] == (allowed ? value : Local));
        }
        assert(routes.wanted() == bool(routes.pack()));
        unsigned packed = routes.pack();
        routes.restore(packed);
        assert(routes.pack() == packed);
      }
      routes.restore(0);
      routes.nudge(0, 1);
      assert(routes.source[0] == (first == ChordOnly ? BusChord : BusScale));
      routes.nudge(0, 1);
      assert(routes.source[0] == (first == ScaleOnly ? BusScale : BusChord));
      routes.nudge(0, -2);
      assert(!routes.wanted());
      routes.nudge(1, 1);
      assert(routes.wanted() == (second != NoSource));
    }
  }
}

static unsigned compareNotes(const quantcore::Harmony& harmony) {
  const int origin = 60 * 128;
  unsigned comparisons = 0;
  for (auto source : {BusScale, BusChord}) {
    quantcore::Quantizer reference;
    reference.Configure(harmony, source == BusScale ? quantcore::SCALE : quantcore::CHORD);
    const auto& tones = notes(source);
    int count = 0;
    for (int note = 0; note < 150; ++note) {
      int cents = (note - 60) * 100 + harmony.fine_cents;
      if (reference.Contains(note) && cents >= -3600 && cents <= 7200) {
        assert(tones.firstAt(note) == count && count < tones.count);
        assert(tones.midi[count] == note);
        assert(tones.pitch(count) == reference.RenderedPitch(note) - origin);
        ++count;
      }
    }
    assert(count == tones.count);
    if (count) {
      assert(tones.shifted(0, -12) == tones.pitch(0));
      assert(tones.shifted(count - 1, 48) == tones.pitch(count - 1));
    }
    for (int cv = -6000; cv <= 10240; cv += 7) {
      for (bool measured : {false, true}) {
        int32_t expected = 0;
        const int lo = origin - 4608, hi = origin + 9216;
        bool valid = measured ? reference.ProcessMeasured(cv + origin, lo, hi, &expected)
                              : reference.ProcessLogical(cv + origin, lo, hi, &expected);
        int index = tones.nearest(cv, measured);
        assert(valid == (index >= 0));
        if (valid) assert(tones.pitch(index) == expected - origin);
        ++comparisons;
      }
    }
  }
  return comparisons;
}

int main() {
  routing();
  unsigned comparisons = 0;
  releaseState();
  for (int variant = 0; variant < 4; ++variant) {
    quantcore::Harmony h{};
    h.enabled = true;
    if (variant == 1) memset(h.scale, 0xff, sizeof(h.scale));
    if (variant >= 2) { h.scale[0] = 0x11; h.scale[11] = 0x80; }
    if (variant == 3) { h.chord[0] = 0x10; h.chord[2] = 0x08; }
    for (int fine : {-128, -50, 0, 25, 50, 127}) {
      h.fine_cents = fine;
      h.root = -31;
      installHarmony(h);
      auto revision = harmonyRevision();
      installHarmony(h);
      assert(revision == harmonyRevision());
      int oldRoot = rootCV();
      h.root = 37;
      installHarmony(h);
      assert(revision != harmonyRevision() && oldRoot != rootCV());
      comparisons += compareNotes(h);
    }
  }
  releaseState();
  assert(!notes(BusChord).count && !notes(BusScale).count);
  printf("Hemisphere routing and %u pitch comparisons passed.\n", comparisons);
}
