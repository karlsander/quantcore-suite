// Copyright 2026 QuantCore contributors.
// SPDX-License-Identifier: MIT

#ifndef BRAIDS_QUANTCORE_QWIRE_H_
#define BRAIDS_QUANTCORE_QWIRE_H_

#include "core.h"

namespace quantcore {

// Raw-sample QWire data receiver. It does not consume clock/reset prefixes:
// harmony/recall followers are independent of timing, and TRIG stays local.
//
// Sample timestamps are a monotonic unsigned counter at exactly 96000 Hz
// (wrapping is supported). Pass FRESH physical level samples, never repeatedly
// submit a stale ADC value as if it were freshly observed. Max accepted gap is
// 8 ticks; actual adapter cadence/analog delay still needs qualification.
// This class interpolates observed edges to half the interval between adjacent
// samples and samples UART bit centres using those neighboring levels.
//
// Sample() is suitable for a single receive ISR. It performs incremental CRC
// per byte, at most one bit decision per sample, and at delimiter copies at
// most 20 bytes. It performs no allocation, synthesis, recall, or mask search.
// TakeUpdates() MUST run with that ISR disabled and compiler memory barriers
// supplied by the target adapter. All mailbox storage is volatile. The two
// domains coalesce independently; no pending-next-clock state is present.
class QWireReceiver {
 public:
  void Init(uint8_t subscriptions = ALL_DOMAINS);
  void Reset();
  // Explicit loss of sampling continuity (flash stall, missed IRQ/deadline,
  // ADC pipeline restart). Drop only partial acquisition. Already validated
  // mailboxes and diagnostic counters remain intact; a fresh HIGH followed
  // by a complete LOW delimiter is required before another burst can apply.
  void Discontinuity();
  void Sample(bool physical_high, uint32_t ticks);
  uint8_t TakeUpdates(Harmony* harmony, Recall* recall);

  uint32_t accepted_harmony() const { return accepted_harmony_; }
  uint32_t accepted_recall() const { return accepted_recall_; }
  uint32_t rejected_bursts() const { return rejected_bursts_; }
  uint32_t framing_errors() const { return framing_errors_; }
  uint32_t sample_overruns() const { return sample_overruns_; }

 private:
  void ResetAcquisition();
  void Delimiter();
  void Publish(uint8_t domain);
  static bool AtOrAfter(uint32_t a, uint32_t b) {
    return static_cast<int32_t>(a - b) >= 0;
  }

  BurstDecoder burst_;
  uint8_t subscriptions_;
  bool have_sample_;
  bool previous_high_;
  bool saw_high_;
  bool low_active_;
  bool low_delimited_;
  bool synchronized_;
  uint32_t previous_ticks_;
  uint32_t low_since_;
  // Serial state: 0 idle; 1 start check; 2..9 data bits; 10 stop check.
  uint8_t serial_state_;
  uint8_t serial_byte_;
  uint32_t next_sample_twice_;
  volatile uint8_t pending_;
  volatile uint8_t pending_harmony_[20];
  volatile uint8_t pending_recall_[7];
  volatile uint32_t accepted_harmony_;
  volatile uint32_t accepted_recall_;
  volatile uint32_t rejected_bursts_;
  volatile uint32_t framing_errors_;
  volatile uint32_t sample_overruns_;
};

}  // namespace quantcore

#endif  // BRAIDS_QUANTCORE_QWIRE_H_
