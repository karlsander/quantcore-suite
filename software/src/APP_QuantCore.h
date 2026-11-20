// Copyright 2026 QuantCore contributors. SPDX-License-Identifier: MIT
#pragma once
#include "quantcore/module.h"

namespace qcui {
void line(int y, const char* label) {
  graphics.setPrintPos(0, y);
  graphics.print(label);
}
void grid(const uint8_t* bits, unsigned rows) {
  for (unsigned row = 0; row < rows; ++row) {
    for (unsigned col = 0; col < 12; ++col) {
      unsigned n = row * 12 + col;
      int x = 2 + col * 10, y = 14 + row * 6;
      if (bits[n / 8] & (1 << (n % 8))) graphics.drawRect(x, y, 8, 4);
      else graphics.drawRect(x + 3, y + 2, 1, 1);
    }
  }
}

enum Field { Tempo, Enable, Root, Fine, ScalePreset, ScaleBit, ChordBit,
             Project, Program, Pattern, Snapshot, FieldCount };
static const char* const fields[] = {"Tempo BPM", "Harmony", "Root semitones", "Fine cents",
  "Scale preset", "Scale bit", "Chord bit", "Project", "Program", "Pattern", "Snapshot"};
static const char* const presets[] = {"Empty", "Major", "Minor", "Chromatic"};
static const uint16_t masks[] = {0, 0xab5, 0x5ad, 0xfff};
static int field, preset = 1, scaleBit, chordBit;

void edit(int delta) {
  auto& s = qc::settings;
  auto& h = s.harmony;
  switch (field) {
    case Tempo: s.bpm = constrain(s.bpm + delta, 20, 300); break;
    case Enable: h.enabled = delta > 0; break;
    case Root: h.root = constrain(h.root + delta, -128, 127); break;
    case Fine: h.fine_cents = constrain(h.fine_cents + delta, -128, 127); break;
    case ScalePreset: {
      preset = constrain(preset + delta, 0, 3);
      uint8_t scale[12] = {};
      for (unsigned n = 0; n < 96; ++n)
        if (masks[preset] & (1 << (n % 12))) scale[n / 8] |= 1 << (n % 8);
      noInterrupts();
      memcpy(h.scale, scale, sizeof(scale));
      interrupts();
      break;
    }
    case ScaleBit: scaleBit = constrain(scaleBit + delta, 0, 95); break;
    case ChordBit: chordBit = constrain(chordBit + delta, 0, 23); break;
    default: {
      auto& id = s.selectors[field - Project];
      id = constrain(id + delta, -1, 255);
    }
  }
}
void showLeader() {
  auto& s = qc::settings;
  line(0, "QuantCore Leader");
  line(12, qc::timerFault ? "Timer unavailable" : (qc::running ? "Run   A: QWire" : "Stop  A: QWire"));
  line(26, fields[field]); line(38, "");
  switch (field) {
    case Tempo: graphics.print(s.bpm); break;
    case Enable: graphics.print(s.harmony.enabled ? "Enabled" : "Disabled"); break;
    case Root: graphics.print(int(s.harmony.root)); break;
    case Fine: graphics.print(int(s.harmony.fine_cents)); break;
    case ScalePreset: graphics.print(preset < 0 ? "Custom" : presets[preset]); break;
    case ScaleBit: case ChordBit: {
      int n = field == ScaleBit ? scaleBit : chordBit;
      auto bits = field == ScaleBit ? s.harmony.scale : s.harmony.chord;
      graphics.print(n); graphics.print(bits[n / 8] & (1 << (n % 8)) ? " on" : " off");
      break;
    }
    default: {
      int id = s.selectors[field - Project];
      if (id < 0) graphics.print("Absent"); else graphics.print(id);
    }
  }
  line(54, field == ScaleBit || field == ChordBit ? "R:bit  Up:run/stop" : "L:field R:edit Up:run");
}
static unsigned page;
static qc::Voice voices[2];
static unsigned quarterGate;
void showFollower() {
  // The UI can be interrupted by the controller. Take coherent domain copies.
  noInterrupts();
  auto h = qc::harmony;
  int ids[4];
  for (int i = 0; i < 4; ++i) ids[i] = qc::selector(i);
  interrupts();
  switch (page) {
    case 0:
      line(0, "QuantCore  TR3 QWire");
      line(13, qc::timerFault ? "Timer unavailable" : !qc::haveH ? "Waiting for harmony" : h.enabled ? "Harmony enabled" : "Harmony disabled");
      line(25, "Root "); graphics.print(int(h.root)); graphics.print("  Fine "); graphics.print(int(h.fine_cents)); graphics.print("c");
      line(39, "A:scale B:chord");
      line(51, "C:root  D:quarter");
      break;
    case 1: line(0,"Scale  C..B x 8"); grid(h.scale, 8); break;
    case 2: line(0,"Chord  C..B x 2"); grid(h.chord, 2); break;
    case 3:
      line(0, qc::haveR ? "Recall" : "Waiting for recall");
      for (unsigned i = 0; i < 4; ++i) {
        line(13 + 12 * i, qc::slotNames[i]); graphics.setPrintPos(82, 13 + 12 * i);
        if (ids[i] < 0) graphics.print("--"); else graphics.print(ids[i]);
      }
      break;
    default:
      line(0, "QWire diagnostics");
      line(14, "Valid H "); graphics.print(int(qc::acceptedH()));
      line(26, "Valid R "); graphics.print(int(qc::acceptedR()));
      line(38, "Sample gaps "); graphics.print(int(qc::sampleGaps()));
      line(50, "Dropped "); graphics.print(int(qc::rejectedBursts()));
      break;
  }
}
}

static void QCLeader_init() { qc::settings.defaults(); }
static constexpr size_t QCLeader_storageSize() { return 29; }
static size_t QCLeader_save(void* dest) {
  // Keep edited masks even while disabled. Store enable in the high bit of the tempo.
  auto saved = qc::settings;
  uint16_t tempo = saved.bpm | (saved.harmony.enabled ? 0x8000 : 0);
  saved.harmony.enabled = true;
  qc::Packet packet;
  packet.encode(saved, qc::Packet::Stopped);
  auto out = static_cast<uint8_t*>(dest);
  memcpy(out, packet.h, 20); memcpy(out + 20, packet.r, 7);
  out[27] = tempo; out[28] = tempo >> 8;
  return QCLeader_storageSize();
}
static size_t QCLeader_restore(const void* src) {
  auto bytes = static_cast<const uint8_t*>(src);
  qc::Settings next{};
  quantcore::Recall r;
  next.bpm = bytes[27] | ((bytes[28] & 0x7f) << 8);
  if (next.bpm < 20 || next.bpm > 300 ||
      !quantcore::DecodeHarmony(bytes, 20, &next.harmony) ||
      !quantcore::DecodeRecall(bytes + 20, 7, &r)) return QCLeader_storageSize();
  next.harmony.enabled = bytes[28] & 0x80;
  const int ids[] = {r.project, r.program, r.pattern, r.snapshot};
  for (unsigned i = 0; i < 4; ++i) next.selectors[i] = r.presence & (1 << i) ? ids[i] : -1;
  qc::settings = next;
  qcui::preset = -1;
  return QCLeader_storageSize();
}
static void QCLeader_handleAppEvent(OC::AppEvent e) {
  if (e == OC::APP_EVENT_RESUME) qc::enterLeader();
}
static void QCLeader_handleEncoderEvent(const UI::Event& e) {
  if (e.control == OC::CONTROL_ENCODER_L) qcui::field = constrain(qcui::field + e.value, 0, qcui::FieldCount - 1);
  if (e.control == OC::CONTROL_ENCODER_R) qcui::edit(e.value);
}
static void QCLeader_handleButtonEvent(const UI::Event& e) {
  if (e.type != UI::EVENT_BUTTON_PRESS) return;
  if (e.control == OC::CONTROL_BUTTON_UP) qc::running = !qc::running;
  if (e.control == OC::CONTROL_BUTTON_R) {
    auto& h = qc::settings.harmony;
    if (qcui::field == qcui::ScaleBit) {
      h.scale[qcui::scaleBit / 8] ^= 1 << (qcui::scaleBit % 8);
      qcui::preset = -1;
    }
    if (qcui::field == qcui::ChordBit) h.chord[qcui::chordBit / 8] ^= 1 << (qcui::chordBit % 8);
  }
}
static void QCLeader_isr() {}
static void QCLeader_loop() {}
static void QCLeader_menu() { qcui::showLeader(); }
static void QCLeader_screensaver() { qcui::showLeader(); }

static void QCFollower_init() {}
static constexpr size_t QCFollower_storageSize() { return 0; }
static size_t QCFollower_save(void*) { return 0; }
static size_t QCFollower_restore(const void*) { return 0; }
static void QCFollower_handleAppEvent(OC::AppEvent event) {
  if (event == OC::APP_EVENT_RESUME) qcui::quarterGate = 0;
}
static void QCFollower_loop() {}
static void QCFollower_menu() { qcui::showFollower(); }
static void QCFollower_screensaver() { qcui::showFollower(); }
static void QCFollower_handleButtonEvent(const UI::Event&) {}
static void QCFollower_handleEncoderEvent(const UI::Event& e) {
  qcui::page = constrain(int(qcui::page) + e.value, 0, 4);
}
static void QCFollower_isr() {
  qc::receive(true);
  int a = qcui::voices[0].measured(OC::ADC::pitch_value(ADC_CHANNEL_1), quantcore::SCALE);
  int b = qcui::voices[1].measured(OC::ADC::pitch_value(ADC_CHANNEL_2), quantcore::CHORD);
  OC::DAC::set_pitch(DAC_CHANNEL_A, a, 0);
  OC::DAC::set_pitch(DAC_CHANNEL_B, b, 0);
  OC::DAC::set_pitch(DAC_CHANNEL_C, qc::rootCV(), 0);
  auto& gate = qcui::quarterGate;
  if (qc::pulse != qc::NoPulse) gate = 84;
  OC::DAC::set_octave(DAC_CHANNEL_D, gate ? 5 : 0);
  if (gate) --gate;
}
