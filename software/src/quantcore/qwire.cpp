// Copyright 2026 QuantCore contributors.
// SPDX-License-Identifier: MIT

#include "qwire.h"

namespace quantcore {

void QWireReceiver::Init(uint8_t subscriptions) {
  subscriptions_ = subscriptions & ALL_DOMAINS;
  Reset();
}

void QWireReceiver::ResetAcquisition() {
  burst_.Init(subscriptions_);
  have_sample_ = false;
  previous_high_ = saw_high_ = low_active_ = low_delimited_ = false;
  synchronized_ = false;
  serial_state_ = serial_byte_ = 0;
  previous_ticks_ = low_since_ = next_sample_twice_ = 0;
}

void QWireReceiver::Reset() {
  ResetAcquisition();
  pending_ = 0;
  accepted_harmony_ = accepted_recall_ = rejected_bursts_ = 0;
  framing_errors_ = sample_overruns_ = 0;
}

void QWireReceiver::Discontinuity() {
  ++sample_overruns_;
  ResetAcquisition();
}

void QWireReceiver::Publish(uint8_t domain) {
  const uint8_t* bytes = burst_.bytes();
  if (domain == HARMONY) {
    const uint8_t* end = bytes + 20;
    volatile uint8_t* destination = pending_harmony_;
    while (bytes != end) *destination++ = *bytes++;
    ++accepted_harmony_;
  } else {
    const uint8_t* end = bytes + 7;
    volatile uint8_t* destination = pending_recall_;
    while (bytes != end) *destination++ = *bytes++;
    ++accepted_recall_;
  }
  pending_ |= domain;
}

void QWireReceiver::Delimiter() {
  if (synchronized_) {
    uint8_t domain = burst_.Finish();
    if (domain) Publish(domain);
    else if (burst_.has_input()) ++rejected_bursts_;
  }
  burst_.Reset();
  synchronized_ = true;
  serial_state_ = serial_byte_ = 0;
  low_delimited_ = true;
}

void QWireReceiver::Sample(bool high, uint32_t ticks) {
  if (have_sample_ && ticks - previous_ticks_ > 8) {
    // A pause is not evidence that the physical line stayed LOW. Require
    // another observed HIGH and complete LOW delimiter before decoding.
    Discontinuity();
  }
  if (!have_sample_) {
    have_sample_ = true;
    previous_ticks_ = ticks;
    previous_high_ = high;
    saw_high_ = high;
    low_active_ = !high;
    low_since_ = ticks;
    return;
  }

  if (high) {
    saw_high_ = true;
    low_active_ = false;
    low_delimited_ = false;
  } else {
    if (!low_active_) {
      low_active_ = true;
      // Conservative: the full 4.5 ms must follow an observed LOW sample.
      low_since_ = ticks;
    }
    if (saw_high_ && !low_delimited_ && ticks - low_since_ >= 432) {
      Delimiter();
    }
  }

  if (synchronized_) {
    const uint32_t now_twice = ticks * 2;
    const uint32_t edge_twice = previous_ticks_ + ticks;
    if (serial_state_ && AtOrAfter(now_twice, next_sample_twice_)) {
      // If an edge was observed on this sample, reconstruct which side of
      // that edge the requested bit centre lies on. No sample interval may
      // span two legal QWire bit edges at the declared maximum sample gap.
      bool bit_high = high;
      if (high != previous_high_ &&
          !AtOrAfter(next_sample_twice_, edge_twice)) {
        bit_high = previous_high_;
      }
      if (serial_state_ == 1) {
        if (!bit_high) {
          burst_.FramingError();
          ++framing_errors_;
          serial_state_ = 0;
        } else {
          serial_state_ = 2;
        }
      } else if (serial_state_ <= 9) {
        if (!bit_high) serial_byte_ |= 1 << (serial_state_ - 2);
        ++serial_state_;
      } else {
        if (bit_high) {
          burst_.FramingError();
          ++framing_errors_;
        } else {
          burst_.AddByte(serial_byte_);
        }
        serial_state_ = 0;
      }
      next_sample_twice_ += 40;  // One bit = 20 timer ticks.
    }
    if (!serial_state_ && high && !previous_high_) {
      serial_state_ = 1;
      serial_byte_ = 0;
      next_sample_twice_ = edge_twice + 20;  // Half a bit after start.
    }
  }
  previous_ticks_ = ticks;
  previous_high_ = high;
}

uint8_t QWireReceiver::TakeUpdates(Harmony* harmony, Recall* recall) {
  // Called under the adapter's IRQ critical section. Do not recompute CRC
  // here: the bounded ISR parser already validated the entire record.
  uint8_t result = pending_;
  if ((result & HARMONY) && harmony) {
    harmony->enabled = pending_harmony_[0] == 0x53;
    for (uint8_t i = 0; i != 12; ++i) harmony->scale[i] = pending_harmony_[i + 1];
    for (uint8_t i = 0; i != 3; ++i) harmony->chord[i] = pending_harmony_[i + 13];
    const uint8_t root = pending_harmony_[16];
    const uint8_t fine = pending_harmony_[17];
    harmony->root = static_cast<int8_t>(root < 128 ? root : int16_t(root) - 256);
    harmony->fine_cents = static_cast<int8_t>(fine < 128 ? fine : int16_t(fine) - 256);
  }
  if ((result & RECALL) && recall) {
    recall->presence = pending_recall_[0] & 0x0f;
    recall->project = pending_recall_[1];
    recall->program = pending_recall_[2];
    recall->pattern = pending_recall_[3];
    recall->snapshot = pending_recall_[4];
  }
  pending_ = 0;
  return result;
}

}  // namespace quantcore
