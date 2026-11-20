// Copyright 2026 QuantCore contributors. SPDX-License-Identifier: MIT
#pragma once
#include "../quantcore/module.h"

class QCApplet : public HemisphereApplet {
 public:
  bool wantsQWire() override { return true; }
 protected:
  void link() {
    gfxPrint(0, 52, qc::timerFault ? "Timer err" : (qc::haveH || qc::haveR ? "TR3 QWire" : "TR3 wait"));
  }
 public:
  void Start() override {}
  uint64_t OnDataRequest() override { return 0; }
  void OnDataReceive(uint64_t) override {}
  void OnEncoderMove(int) override {}
  void OnButtonPress() override {}
};

class QCDual : public QCApplet {
  uint8_t mode[2] = {}, channel = 0;
  qc::Voice voices[2];
 public:
  const char* applet_name() override { return "QC DualQ"; }
  void Controller() override {
    ForEachChannel(ch) {
      Out(ch, voices[ch].measured(In(ch), static_cast<quantcore::Selection>(mode[ch])));
    }
  }
  void View() override {
    ForEachChannel(ch) {
      gfxPrint(0, 16 + 14 * ch, channel == ch ? ">" : " ");
      gfxPrint(ch + 1); gfxPrint(" "); gfxPrint(qc::modeNames[mode[ch]]);
    }
    link();
  }
  void OnButtonPress() override { channel ^= 1; }
  void OnEncoderMove(int d) override { mode[channel] = constrain(mode[channel] + d, 0, 2); }
  uint64_t OnDataRequest() override { return mode[0] | (mode[1] << 2); }
  void OnDataReceive(uint64_t d) override {
    mode[0] = constrain(int(d & 3), 0, 2);
    mode[1] = constrain(int((d >> 2) & 3), 0, 2);
  }
  void SetHelp() override {
    help[HELP_CV1] = "Pitch 1"; help[HELP_CV2] = "Pitch 2";
    help[HELP_OUT1] = "Pitch 1"; help[HELP_OUT2] = "Pitch 2";
    help[HELP_EXTRA1] = "Press: channel";
    help[HELP_EXTRA2] = "Turn: mask source";
  }
};

class QCRoot : public QCApplet {
 public:
  const char* applet_name() override { return "QC Root"; }
  void Controller() override { Out(0, qc::rootCV()); Out(1, qc::fineCV()); }
  void View() override {
    gfxPrint(0, 16, "Root "); gfxPrint(int(qc::harmony.root));
    gfxPrint(0, 30, "Fine "); gfxPrint(int(qc::harmony.fine_cents)); gfxPrint("c");
    link();
  }
  void SetHelp() override {
    help[HELP_OUT1] = "Root CV"; help[HELP_OUT2] = "Fine CV";
    help[HELP_EXTRA1] = "0V = MIDI note 60";
  }
};

class QCRecall : public QCApplet {
  uint8_t slot = 0;
 public:
  const char* applet_name() override { return "QC Recall"; }
  void Controller() override {
    int id = qc::selector(slot);
    Out(0, id < 0 ? 0 : (id * 7680 + 127) / 255);
    GateOut(1, id >= 0);
  }
  void View() override {
    gfxPrint(0, 16, qc::slotNames[slot]);
    int id = qc::selector(slot);
    if (id < 0) gfxPrint(0, 30, "Absent");
    else { gfxPrint(0, 30, "ID "); gfxPrint(id); }
    link();
  }
  void OnEncoderMove(int d) override { slot = constrain(slot + d, 0, 3); }
  uint64_t OnDataRequest() override { return slot; }
  void OnDataReceive(uint64_t d) override { slot = d & 3; }
  void SetHelp() override {
    help[HELP_OUT1] = "ID CV"; help[HELP_OUT2] = "Present";
    help[HELP_EXTRA1] = "Turn: recall slot";
    help[HELP_EXTRA2] = "0..255 = 0..5V";
  }
};

class QCClock : public QCApplet {
  unsigned width[2] = {};
 public:
  const char* applet_name() override { return "QC Clock"; }
  void Controller() override {
    if (qc::pulse != qc::NoPulse) width[0] = 84;
    if (qc::pulse == qc::Restart) width[1] = 84;
    ForEachChannel(ch) {
      GateOut(ch, width[ch] != 0);
      if (width[ch]) --width[ch];
    }
  }
  void Unload() override { width[0] = width[1] = 0; }
  void View() override {
    gfxPrint(0, 16, "1 Quarter"); gfxPrint(0, 30, "2 Reset"); link();
  }
  void SetHelp() override {
    help[HELP_OUT1] = "Quarter"; help[HELP_OUT2] = "Reset";
    help[HELP_EXTRA1] = "TR3: QWire timing";
  }
};

class QCMask : public QCApplet {
  uint8_t index = 0;
 public:
  const char* applet_name() override { return "QC Mask"; }
  void Controller() override {
    auto& h = qc::harmony;
    unsigned chord = index % 24;
    GateOut(0, h.enabled && (h.scale[index / 8] & (1 << (index % 8))));
    GateOut(1, h.enabled && (h.chord[chord / 8] & (1 << (chord % 8))));
  }
  void View() override {
    gfxPrint(0, 16, "Scale "); gfxPrint(index);
    gfxPrint(0, 30, "Chord "); gfxPrint(index % 24); link();
  }
  void OnEncoderMove(int d) override { index = constrain(index + d, 0, 95); }
  uint64_t OnDataRequest() override { return index; }
  void OnDataReceive(uint64_t d) override { index = constrain(int(d & 127), 0, 95); }
  void SetHelp() override {
    help[HELP_OUT1] = "Scale"; help[HELP_OUT2] = "Chord";
    help[HELP_EXTRA1] = "Turn: bit above C4";
  }
};

class QCState : public QCApplet {
 public:
  const char* applet_name() override { return "QC State"; }
  void Controller() override {
    GateOut(0, qc::haveH && qc::harmony.enabled);
    GateOut(1, qc::haveR);
  }
  void View() override {
    gfxPrint(0, 16, !qc::haveH ? "H waiting" : qc::harmony.enabled ? "H enabled" : "H off");
    gfxPrint(0, 30, qc::haveR ? "R acquired" : "R waiting");
    link();
  }
  void SetHelp() override {
    help[HELP_OUT1] = "H on"; help[HELP_OUT2] = "R seen";
    help[HELP_EXTRA1] = "H enable / R acquired";
  }
};
