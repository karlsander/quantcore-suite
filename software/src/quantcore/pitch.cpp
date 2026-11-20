// Copyright 2026 QuantCore contributors. SPDX-License-Identifier: MIT
#include "module.h"
#include <algorithm>

namespace qc {
bool haveH, haveR;
quantcore::Harmony harmony;
quantcore::Recall recall;
static quantcore::Quantizer quantizers[3];
static uint32_t revision = 1;
constexpr int cvMin = -3 * 1536, cvMax = 6 * 1536;

void releaseState() {
  haveH = haveR = false;
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
