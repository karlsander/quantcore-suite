// Copyright 2026 QuantCore contributors. SPDX-License-Identifier: MIT
#pragma once
#include "wire.h"

namespace qc {
extern Settings settings;
extern volatile bool running, timerFault;
extern quantcore::Harmony harmony;
extern quantcore::Recall recall;
extern bool haveH, haveR;
extern Pulse pulse;
extern const char* const slotNames[4];
extern const char* const modeNames[3];

void receive(bool wanted);
void leaveApp();
void enterLeader();
// Called by the core ISR after display DMA has drained and before another SPI user starts.
bool ownsSPI();
bool sending();
uint32_t acceptedH();
uint32_t acceptedR();
uint32_t sampleGaps();
uint32_t rejectedBursts();
void releaseState();
void installHarmony(const quantcore::Harmony& next);
int selector(unsigned slot);
int rootCV();
int fineCV();
// One cache per continuous voice, evaluated in the normal app ISR.
class Voice {
 public:
  int measured(int input, quantcore::Selection mode);
 private:
  int lastInput = 0, output = 0;
  quantcore::Selection lastMode = quantcore::SCALE;
  uint32_t revision = 0;
};
}
