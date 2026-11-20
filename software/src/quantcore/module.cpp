// Copyright 2026 QuantCore contributors. SPDX-License-Identifier: MIT
#ifdef QUANTCORE_SUITE
#include <Arduino.h>
#include "../OC_DAC.h"
#include "../OC_digital_inputs.h"
#include "module.h"
#include "qwire.h"

namespace qc {
Settings settings;
volatile bool running, timerFault;
Pulse pulse;
const char* const slotNames[] = {"Project", "Program", "Pattern", "Snapshot"};
const char* const modeNames[] = {"Scale", "Chord", "Both"};
static quantcore::QWireReceiver receiver;
static Prefix prefix;
static volatile Pulse pendingPulse;
static IntervalTimer rxTimer, txTimer;
static uint32_t sampleTime, ticks96, fraction;
static bool receiving;

static void sample() {
  bool high = !digitalReadFast(TR3); // original trigger jack inverts at the MCU
  uint32_t now = micros();
  uint32_t elapsed = now - sampleTime;
  sampleTime = now;
  if (elapsed > 83) {
    receiver.Discontinuity();
    prefix.clear();
    elapsed = 0;
  }
  // Preserve fractional ticks across samples, including micros() rollover.
  fraction += elapsed * 12;
  ticks96 += fraction / 125;
  fraction %= 125;
  receiver.Sample(high, ticks96);
  Pulse event = prefix.sample(high, now);
  if (event != NoPulse) pendingPulse = event;
}

void receive(bool wanted) {
  if (wanted != OC::DigitalInputs::qwire_reserved)
    OC::DigitalInputs::reserve_qwire(wanted);
  if (wanted != receiving) {
    if (wanted) {
      receiver.Init();
      prefix.clear();
      sampleTime = micros();
      pendingPulse = NoPulse;
      receiving = rxTimer.begin(sample, 31.25f);
      timerFault = !receiving;
      rxTimer.priority(48);
    } else {
      rxTimer.end();
      receiving = false;
      releaseState();
    }
  }
  pulse = NoPulse;
  if (!receiving) return;
  quantcore::Harmony next;
  uint32_t mask;
  asm volatile("mrs %0, primask" : "=r"(mask) :: "memory");
  __disable_irq();
  unsigned domains = receiver.TakeUpdates(&next, &recall);
  pulse = pendingPulse;
  pendingPulse = NoPulse;
  if (!mask) __enable_irq();
  if (domains & quantcore::HARMONY) installHarmony(next);
  if (domains & quantcore::RECALL) haveR = true;
}

uint32_t acceptedH() { return receiver.accepted_harmony(); }
uint32_t acceptedR() { return receiver.accepted_recall(); }
uint32_t rejectedBursts() { return receiver.rejected_bursts(); }
uint32_t sampleGaps() { return receiver.sample_overruns(); }

// The OLED and the DAC share SPI0. Only the core ISR grants a transaction,
// after flushing OLED DMA. The TX timer exclusively owns SPI until the final stop bit.
static Packet packet;
static volatile bool busy;
static volatile bool leader;
static bool initializeLine, lastHigh;
static Schedule schedule;
static volatile uint32_t finished;
static uint16_t position, lowCode, highCode;

bool sending() { return busy; }
static void writeLine(bool high) {
  uint16_t code = high ? highCode : lowCode;
  if (DAC_CHANNEL_A == 0) set8565_CHA(code);
  else set8565_CHD(code); // flipped controls
}
static void transmit() {
  if (position == packet.length()) {
    txTimer.end();
    finished = micros();
    busy = false;
    return;
  }
  bool high = packet.high(position++);
  if (high != lastHigh) {
    writeLine(high);
    lastHigh = high;
  }
}

void leaveApp() {
  leader = running = false;
  receive(false);
  // App switching runs in the foreground. Finish before an EEPROM save can stall IRQs.
  while (busy) yield();
}
void enterLeader() {
  if (leader) return;
  running = false;
  initializeLine = true;
  OC::DAC::set_all_octave(0);
  lowCode = OC::DAC::get_zero_offset(DAC_CHANNEL_A);
  highCode = OC::DAC::pitch_to_dac(DAC_CHANNEL_A, 5069, 0); // 3.3 V
  leader = true;
}

bool ownsSPI() {
  if (busy) return true;
  if (!leader) return false;
  uint32_t now = micros();
  if (initializeLine) {
    writeLine(false);
    finished = micros();
    schedule.init(finished);
    initializeLine = false;
    return false;
  }
  if (!schedule.due(now, finished, running)) return false;
  packet.encode(settings, schedule.kind());
  position = 0;
  lastHigh = false;
  busy = true;
  txTimer.priority(48);
  if (!txTimer.begin(transmit, 1000000.0f / 24000)) {
    busy = false;
    timerFault = true;
    schedule.retryLater(now);
    return false;
  }
  timerFault = false;
  schedule.launched(now, settings.bpm);
  return true;
}
}
#endif
