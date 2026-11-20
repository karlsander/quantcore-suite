// Copyright 2026 QuantCore contributors.
// SPDX-License-Identifier: MIT
// Host conformance tests, including an independent sampled analog wire model.

#include "core.h"
#include "qwire.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

using namespace quantcore;

static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { \
  fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); abort(); \
} } while (0)

static const uint8_t kHarmony[] = {
  0x53,0xb5,0x5a,0xab,0xb5,0x5a,0xab,0xb5,0x5a,0xab,0xb5,0x5a,0xab,
  0x91,0,0,2,0,0x6e,0x10
};
static const uint8_t kDisabled[] = {
  0x52,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x88,0xbc
};

static std::vector<uint8_t> Seal(std::vector<uint8_t> bytes) {
  uint16_t crc = Crc16(&bytes[0], bytes.size());
  bytes.push_back(crc & 255);
  bytes.push_back(crc >> 8);
  return bytes;
}

static std::vector<uint8_t> HarmonyBytes(unsigned seed, int fine = 0) {
  std::vector<uint8_t> bytes(18, 0);
  bytes[0] = 0x53;
  for (unsigned i = 1; i != 17; ++i) {
    seed = seed * 1664525u + 1013904223u;
    bytes[i] = seed >> 24;
  }
  bytes[17] = fine & 255;
  return Seal(bytes);
}

static std::vector<uint8_t> RecallBytes(unsigned program) {
  std::vector<uint8_t> bytes(5, 0);
  bytes[0] = 0x82;
  bytes[2] = program;
  return Seal(bytes);
}

static void TestRecords() {
  CHECK(Crc16(reinterpret_cast<const uint8_t*>("123456789"),9) == 0x29b1);
  Harmony h;
  h.Clear();
  CHECK(DecodeHarmony(kHarmony,sizeof(kHarmony),&h));
  CHECK(h.enabled && h.root == 2 && h.fine_cents == 0);
  CHECK(h.chord[0] == 0x91);
  Harmony old = h;
  CHECK(!DecodeHarmony(kHarmony,19,&h));
  CHECK(h.Equals(old));
  uint8_t damaged[20];
  for (unsigned bit = 0; bit != 160; ++bit) {
    memcpy(damaged,kHarmony,20);
    damaged[bit / 8] ^= 1 << (bit % 8);
    CHECK(!DecodeHarmony(damaged,20,&h));
    CHECK(h.Equals(old));
  }
  CHECK(DecodeHarmony(kDisabled,20,&h));
  CHECK(!h.enabled);
  std::vector<uint8_t> invalid(kDisabled,kDisabled+18);
  invalid[2] = 1;
  invalid = Seal(invalid);
  CHECK(!DecodeHarmony(&invalid[0],20,&h));
  for (unsigned b = 0; b != 256; ++b) {
    std::vector<uint8_t> bytes = HarmonyBytes(b,int(b) - 128);
    CHECK(DecodeHarmony(&bytes[0],20,&h));
    CHECK(h.fine_cents == int(b) - 128);
    std::vector<uint8_t> r = RecallBytes(b);
    Recall recall;
    CHECK(DecodeRecall(&r[0],7,&recall));
    CHECK(recall.presence == 2 && recall.program == b);
  }
  for (unsigned presence = 0; presence != 16; ++presence) {
    std::vector<uint8_t> raw(5,0);
    raw[0] = 0x80 | presence;
    for (unsigned i = 0; i != 4; ++i) raw[i+1] = presence & (1<<i) ? 255 : 0;
    std::vector<uint8_t> r = Seal(raw);
    Recall recall;
    CHECK(DecodeRecall(&r[0],7,&recall));
    for (unsigned i = 0; i != 4; ++i) if (!(presence & (1<<i))) {
      std::vector<uint8_t> bad = raw;
      bad[i+1] = 1;
      bad = Seal(bad);
      CHECK(!DecodeRecall(&bad[0],7,&recall));
    }
  }
}

static void Add(BurstDecoder* decoder, const std::vector<uint8_t>& bytes) {
  for (size_t i=0;i!=bytes.size();++i) decoder->AddByte(bytes[i]);
}

static void TestBursts() {
  BurstDecoder b;
  b.Init();
  std::vector<uint8_t> h(kHarmony,kHarmony+20);
  for (unsigned i=0;i!=20;++i) {
    b.AddByte(h[i]);
    if (i != 19) CHECK(b.Finish() == NONE);
  }
  CHECK(b.Finish() == HARMONY);
  b.AddByte(0);
  CHECK(b.Finish() == NONE);
  b.Reset();
  b.AddByte(0x51);
  Add(&b,h);
  CHECK(b.Finish() == NONE);
  b.Reset();
  Add(&b,h);
  b.FramingError();
  CHECK(b.Finish() == NONE);
  b.Reset();
  Add(&b,RecallBytes(0));
  CHECK(b.Finish() == RECALL);
  b.Init(RECALL);
  Add(&b,h);
  CHECK(b.Finish() == NONE);
  b.Reset();
  Add(&b,RecallBytes(255));
  CHECK(b.Finish() == RECALL);
  b.Init(HARMONY);
  Add(&b,RecallBytes(0));
  CHECK(b.Finish() == NONE);
  b.Reset();
  Add(&b,h);
  CHECK(b.Finish() == HARMONY);
  b.Init();
  Add(&b,std::vector<uint8_t>(kDisabled,kDisabled+20));
  CHECK(b.Finish() == HARMONY);
  for (unsigned field=1;field!=18;++field) {
    std::vector<uint8_t> invalid(kDisabled,kDisabled+18);
    invalid[field]=1;
    invalid=Seal(invalid);
    b.Reset();
    Add(&b,invalid);
    CHECK(b.Finish()==NONE);
  }
  for (unsigned presence=0;presence!=16;++presence) {
    std::vector<uint8_t> raw(5,0);
    raw[0]=0x80|presence;
    b.Reset();
    Add(&b,Seal(raw));
    CHECK(b.Finish()==RECALL);
    for (unsigned slot=0;slot!=4;++slot) if (!(presence&(1<<slot))) {
      std::vector<uint8_t> invalid=raw;
      invalid[slot+1]=255;
      b.Reset();
      Add(&b,Seal(invalid));
      CHECK(b.Finish()==NONE);
    }
  }
}

static int Mod(int value, int divisor) {
  int m = value % divisor;
  return m < 0 ? m + divisor : m;
}

static bool Allowed(const Harmony& h, Selection selection, int note) {
  bool s = (h.scale[Mod(note-60,96)/8] & (1 << Mod(note-60,8))) != 0;
  bool c = (h.chord[Mod(note-60,24)/8] & (1 << Mod(note-60,8))) != 0;
  if (!(h.chord[0] | h.chord[1] | h.chord[2])) selection = SCALE;
  return selection == SCALE ? s : selection == CHORD ? c : s && c;
}

static bool Oracle(const Harmony& h, Selection selection, int input,
                   int low, int high, bool measured, int* identity, int* pitch) {
  bool found = false;
  int64_t best = 0;
  int64_t target = int64_t(input)*100 - (measured ? h.fine_cents*128 : 0);
  for (int n=-300;n<=350;++n) {
    if (!Allowed(h,selection,n)) continue;
    int64_t rendered = int64_t(n)*12800 + int(h.fine_cents)*128;
    if (rendered < int64_t(low)*100 || rendered > int64_t(high)*100) continue;
    int64_t d = target-int64_t(n)*12800;
    if (d<0) d=-d;
    if (!found || d<best) {
      found=true;
      best=d;
      *identity=n;
      int fine = h.fine_cents*128;
      fine = fine<0 ? -((-fine+50)/100) : (fine+50)/100;
      *pitch=n*128+fine;
    }
  }
  return found;
}

static void TestQuantizer() {
  Harmony h;
  h.Clear();
  h.enabled=true;
  h.scale[0]=1;
  h.chord[0]=1;
  Quantizer q;
  q.Init();
  q.Configure(h);
  CHECK(q.Contains(60));
  CHECK(q.Contains(-36));
  CHECK(q.Contains(156));
  CHECK(!q.Contains(72));
  CHECK(!q.Contains(48));
  int32_t note=0,out=0;
  CHECK(q.NextIdentity(-40,&note) && note==-36);
  CHECK(q.PreviousIdentity(-40,&note) && note==-132);
  q.Configure(h,CHORD);
  CHECK(q.Contains(60) && q.Contains(84) && !q.Contains(72));
  CHECK(q.NextIdentity(61,&note) && note==84);
  h.chord[0]=0;
  q.Configure(h,CHORD);
  CHECK(q.NextIdentity(61,&note) && note==156);
  h.scale[0]=0x11;
  q.Configure(h);
  CHECK(q.ProcessLogical(62*128,0,16383,&out,&note) && note==60);
  h.fine_cents=-128;
  q.Configure(h);
  CHECK(q.ProcessLogical(62*128,0,16383,&out,&note) && note==60);
  CHECK(q.ProcessMeasured(62*128,0,16383,&out,&note) && note==64);
  CHECK(out == 64*128-164);
  h.scale[0]=1;
  h.fine_cents=127;
  q.Configure(h);
  CHECK(!q.ProcessMeasured(60*128,0,60*128,&out,&note));
  h.fine_cents=0;
  q.Configure(h);
  CHECK(q.ProcessLogical(60*128,0,60*128,&out,&note) && note==60);
  h.Clear();
  h.enabled=true;
  q.Configure(h);
  CHECK(q.empty() && !q.ProcessLogical(0,0,16383,&out));
  h.enabled=false;
  q.Configure(h);
  CHECK(!q.enabled());

  unsigned seed=0x23a09f12;
  for (unsigned trial=0;trial!=12000;++trial) {
    std::vector<uint8_t> bytes=HarmonyBytes(trial,int(trial%256)-128);
    CHECK(DecodeHarmony(&bytes[0],bytes.size(),&h));
    Selection selection=static_cast<Selection>(trial%3);
    q.Configure(h,selection);
    seed=seed*1664525u+1013904223u;
    int input=int(seed%70000)-25000;
    seed=seed*1664525u+1013904223u;
    int lower=int(seed%22000)-16000;
    seed=seed*1664525u+1013904223u;
    int upper=lower+int(seed%23000);
    bool measured=(trial&1)!=0;
    int expected_note=0,expected_pitch=0;
    bool expected=Oracle(h,selection,input,lower,upper,measured,
                         &expected_note,&expected_pitch);
    bool actual=measured ? q.ProcessMeasured(input,lower,upper,&out,&note)
                          : q.ProcessLogical(input,lower,upper,&out,&note);
    CHECK(actual==expected);
    if (actual) {
      CHECK(note==expected_note);
      CHECK(out==expected_pitch);
      CHECK(out>=lower && out<=upper);
      CHECK(q.Contains(note));
    }
    h.root=static_cast<int8_t>(trial&255);
    q.Configure(h,selection);
    int32_t unchanged_out=0,unchanged_note=0;
    bool unchanged=measured ? q.ProcessMeasured(input,lower,upper,
                                                &unchanged_out,&unchanged_note)
                            : q.ProcessLogical(input,lower,upper,
                                               &unchanged_out,&unchanged_note);
    CHECK(unchanged==actual);
    CHECK(!actual || (unchanged_note==note && unchanged_out==out));
  }
}

struct Segment {
  double end;
  bool high;
  Segment(double e,bool h):end(e),high(h) { }
};

struct Wave {
  std::vector<Segment> segments;
  double end;
  double bit;
  double gap;
  explicit Wave(double speed=1.0,double interbyte_gap=0):
      end(0),bit(20.0/speed),gap(interbyte_gap) { }
  void Add(bool high,double ticks) {
    end+=ticks;
    segments.push_back(Segment(end,high));
  }
  void Byte(uint8_t byte,bool bad_stop=false) {
    Add(true,bit);
    for (unsigned i=0;i!=8;++i) Add((byte&(1<<i))==0,bit);
    Add(bad_stop,bit);
  }
  void Bytes(const std::vector<uint8_t>& bytes,int bad_stop=-1) {
    for (size_t i=0;i!=bytes.size();++i) {
      Byte(bytes[i],int(i)==bad_stop);
      if (i+1!=bytes.size()) Add(false,gap);
    }
  }
  void Start(double phase=0,int prefix=0) {
    Add(false,2400+phase);
    if (prefix) {
      Add(true,prefix==1?480:576);
      Add(false,prefix==1?192:96);
    }
    Add(true,48);
    Add(false,576);
  }
  void Finish() { Add(false,2400); }
  void Jitter(unsigned seed,double amount) {
    std::vector<Segment> merged;
    for (size_t i=0;i<segments.size();++i) {
      if (!merged.empty() && merged.back().high==segments[i].high) {
        merged.back().end=segments[i].end;
      } else {
        merged.push_back(segments[i]);
      }
    }
    segments=merged;
    for (size_t i=0;i+1<segments.size();++i) {
      if (segments[i].high == segments[i+1].high) continue;
      seed=seed*1664525u+1013904223u;
      // Independent +/- scheduling jitter at each real transition.
      segments[i].end += (seed&0x80000000u) ? amount : -amount;
    }
  }
};

// Independent first-order RC model, followed by a software Schmitt trigger.
// Units are 96kHz timer ticks, so tau=2.3904 models 24.9 microseconds.
class AnalogReader {
 public:
  AnalogReader(const Wave& wave,double tau,double lo,double hi,
               double source_low=0,double source_high=3.3):
      wave_(wave),tau_(tau),lo_(lo),hi_(hi),source_low_(source_low),
      source_high_(source_high),index_(0),time_(0),value_(source_low),
      high_(false) { }
  bool Read(double at) {
    while (index_ < wave_.segments.size() && wave_.segments[index_].end < at) {
      Advance(wave_.segments[index_].end);
      ++index_;
    }
    Advance(at);
    if (value_>=hi_) high_=true;
    else if (value_<=lo_) high_=false;
    return high_;
  }
 private:
  void Advance(double at) {
    double target=index_<wave_.segments.size() && wave_.segments[index_].high
        ? source_high_:source_low_;
    value_=tau_>0 ? target+(value_-target)*exp(-(at-time_)/tau_) : target;
    time_=at;
  }
  const Wave& wave_;
  double tau_,lo_,hi_,source_low_,source_high_;
  size_t index_;
  double time_,value_;
  bool high_;
};

static void SampleWave(const Wave& wave,QWireReceiver* receiver,
                       unsigned cadence_phase=0,double tau=0,
                       double lo=1.2,double hi=1.8,
                       double source_low=0,double source_high=3.3,
                       uint32_t begin=0,uint32_t clock_offset=0) {
  AnalogReader analog(wave,tau,lo,hi,source_low,source_high);
  const unsigned cadence[]={3,3,6};
  uint32_t t=begin;
  while (t < wave.end) {
    receiver->Sample(analog.Read(t),t+clock_offset);
    t+=cadence[cadence_phase];
    cadence_phase=(cadence_phase+1)%3;
  }
}

static void TestWire() {
  const double speeds[]={0.99,1.0,1.01};
  const double gaps[]={0,24};  // Spec permits up to 0.25 ms extra LOW.
  unsigned waveform_cases=0;
  for (unsigned speed=0;speed!=3;++speed) {
    for (unsigned phase=0;phase!=48;++phase) {
      for (unsigned schedule=0;schedule!=3;++schedule) {
        for (unsigned analog=0;analog!=7;++analog) {
          std::vector<uint8_t> h=HarmonyBytes(phase*1000+schedule*13+speed,
                                             int(phase)-24);
          std::vector<uint8_t> r=RecallBytes(phase);
          Wave wave(speeds[speed],gaps[(phase+schedule)&1]);
          wave.Start(phase*0.125,phase%3);
          wave.Bytes(h);
          wave.Add(false,576);
          wave.Bytes(r);
          wave.Finish();
          QWireReceiver rx;
          rx.Init();
          const double high_levels[]={2.4,3.3,3.6};
          SampleWave(wave,&rx,schedule,analog?2.3904:0,
                     550.0/407.862,700.0/407.862,
                     analog>3?0.6:0,analog?high_levels[(analog-1)%3]:3.3);
          Harmony received,expected;
          Recall recall;
          CHECK(rx.TakeUpdates(&received,&recall)==ALL_DOMAINS);
          CHECK(rx.accepted_harmony()==1 && rx.accepted_recall()==1);
          CHECK(DecodeHarmony(&h[0],20,&expected));
          CHECK(received.Equals(expected));
          CHECK(recall.program==phase && recall.presence==2);
          CHECK(rx.TakeUpdates(&received,&recall)==0);
          ++waveform_cases;
        }
      }
    }
  }
  printf("wire phase/baud/RC/cadence cases: %u\n",waveform_cases);

  // Every byte value appears in H payload, with valid independently computed
  // records. UART framing must not depend on a friendly C-major bit pattern.
  for (unsigned value=0;value!=256;++value) {
    std::vector<uint8_t> raw(18,value);
    raw[0]=0x53;
    std::vector<uint8_t> bytes=Seal(raw);
    Wave wave(value&1?0.99:1.01);
    wave.Start(value/100.0);
    wave.Bytes(bytes);
    wave.Finish();
    QWireReceiver rx;
    rx.Init();
    SampleWave(wave,&rx,value%3,2.3904);
    Harmony h;
    Recall r;
    CHECK(rx.TakeUpdates(&h,&r)==HARMONY);
    CHECK(h.scale[0]==value && h.chord[2]==value);
  }

  // Check CRC, exact byte count, unknown-family poison, serial errors and the
  // independence of R from a preceding malformed H at real wire level.
  for (unsigned fault=0;fault!=184;++fault) {
    std::vector<uint8_t> h(kHarmony,kHarmony+20);
    Wave wave;
    wave.Start();
    if (fault<160) h[fault/8]^=1<<(fault%8);
    if (fault==160) wave.Byte(0x51); // Valid H hidden inside unknown burst.
    if (fault==161) h.push_back(0); // Correct CRC followed by extra data.
    if (fault==162) h.pop_back();
    wave.Bytes(h,fault>=164?int(fault-164):-1);
    if (fault==163) wave.Add(true,300); // Break after otherwise valid H.
    wave.Add(false,576);
    wave.Bytes(RecallBytes(47));
    wave.Finish();
    QWireReceiver rx;
    rx.Init();
    SampleWave(wave,&rx,0,2.3904);
    Harmony received;
    Recall recall;
    CHECK(rx.TakeUpdates(&received,&recall)==RECALL);
    CHECK(rx.accepted_harmony()==0 && rx.accepted_recall()==1);
    CHECK(recall.program==47);
  }

  // A completed CRC alone is not activation. Append a late extra byte before
  // the physical delimiter; the whole burst must be rejected.
  Wave extra;
  extra.Start();
  extra.Bytes(std::vector<uint8_t>(kHarmony,kHarmony+20));
  extra.Add(false,240);
  extra.Byte(0);
  extra.Finish();
  QWireReceiver late;
  late.Init();
  SampleWave(extra,&late);
  Harmony h;
  Recall r;
  CHECK(late.TakeUpdates(&h,&r)==NONE);

  // Joining at each point in a first H record must never create a partial H.
  // The independently delimited first R and next complete H still acquire.
  for (unsigned join=0;join!=400;++join) {
    Wave wave;
    wave.Start();
    uint32_t h_start=static_cast<uint32_t>(wave.end);
    wave.Bytes(std::vector<uint8_t>(kHarmony,kHarmony+20));
    wave.Add(false,576);
    wave.Bytes(RecallBytes(7));
    wave.Finish();
    wave.Add(true,48);
    wave.Add(false,576);
    wave.Bytes(std::vector<uint8_t>(kHarmony,kHarmony+20));
    wave.Add(false,576);
    wave.Bytes(RecallBytes(8));
    wave.Finish();
    QWireReceiver rx;
    rx.Init();
    SampleWave(wave,&rx,join%3,2.3904,1.2,1.8,0,3.3,
               h_start+join*10+1);
    CHECK(rx.TakeUpdates(&h,&r)==ALL_DOMAINS);
    CHECK(rx.accepted_harmony()==1);
    // Joining during the final LOW stop bit means no HIGH has yet been
    // observed; that follower intentionally skips the first R as well.
    CHECK(rx.accepted_recall()>=1 && rx.accepted_recall()<=2);
    CHECK(r.program==8);
  }

  // Unsigned sample timestamps wrap without disrupting reception.
  Wave wrap;
  wrap.Start();
  wrap.Bytes(std::vector<uint8_t>(kHarmony,kHarmony+20));
  wrap.Finish();
  QWireReceiver wrapping;
  wrapping.Init();
  SampleWave(wrap,&wrapping,0,2.3904,1.2,1.8,0,3.3,0,0xfffff000u);
  CHECK(wrapping.TakeUpdates(&h,&r)==HARMONY);

  // A real-time discontinuity must discard partial acquisition while keeping
  // an earlier, fully validated H mailbox intact. The R after its own guard
  // still acquires; partial H cannot bridge a flash or interrupt stall.
  Wave prior;
  prior.Start();
  prior.Bytes(std::vector<uint8_t>(kHarmony,kHarmony+20));
  prior.Finish();
  QWireReceiver paused;
  paused.Init();
  SampleWave(prior,&paused);
  CHECK(paused.accepted_harmony()==1);
  Wave interrupted;
  interrupted.Start();
  const uint32_t interrupt_at=static_cast<uint32_t>(interrupted.end)+2000;
  interrupted.Bytes(HarmonyBytes(123,56));
  interrupted.Add(false,576);
  interrupted.Bytes(RecallBytes(19));
  interrupted.Finish();
  AnalogReader input(interrupted,2.3904,550.0/407.862,700.0/407.862);
  const unsigned intervals[]={3,3,6};
  unsigned phase=0;
  bool interrupted_once=false;
  const uint32_t offset=static_cast<uint32_t>(prior.end);
  for (uint32_t t=0;t<interrupted.end;t+=intervals[phase],phase=(phase+1)%3) {
    if (!interrupted_once && t>=interrupt_at) {
      paused.Discontinuity();
      interrupted_once=true;
    }
    paused.Sample(input.Read(t),offset+t);
  }
  CHECK(paused.sample_overruns()==1);
  CHECK(paused.accepted_harmony()==1 && paused.accepted_recall()==1);
  CHECK(paused.TakeUpdates(&h,&r)==ALL_DOMAINS);
  Harmony original;
  CHECK(DecodeHarmony(kHarmony,20,&original));
  CHECK(h.Equals(original) && r.program==19);
  const uint32_t rejected_before=paused.rejected_bursts();
  paused.Discontinuity();
  CHECK(paused.sample_overruns()==2 && paused.rejected_bursts()==rejected_before);
  CHECK(paused.accepted_harmony()==1 && paused.accepted_recall()==1);

  // Scheduling jitter is a separately reported stress test. It is not used
  // to silently widen the protocol's measured bit-duration requirements.
  for (unsigned jitter_us=5;jitter_us<=20;jitter_us+=5) {
    unsigned failures=0,total=0;
    for (unsigned trial=0;trial!=600;++trial) {
      std::vector<uint8_t> bytes=HarmonyBytes(trial,int(trial%256)-128);
      Wave wave(speeds[trial%3]);
      wave.Start((trial%48)*0.125,trial%3);
      wave.Bytes(bytes);
      wave.Add(false,576);
      wave.Bytes(RecallBytes(trial%256));
      wave.Finish();
      wave.Jitter(trial*71317u+1,jitter_us*0.096);
      QWireReceiver rx;
      rx.Init();
      SampleWave(wave,&rx,trial%3,2.3904,550.0/407.862,700.0/407.862,
                 trial&1?0.6:0,trial&2?2.4:3.6);
      ++total;
      uint8_t updated=rx.TakeUpdates(&h,&r);
      if (updated!=ALL_DOMAINS) ++failures;
      if (updated&HARMONY) {
        Harmony expected;
        CHECK(DecodeHarmony(&bytes[0],20,&expected));
        CHECK(h.Equals(expected));
      }
      if (updated&RECALL) CHECK(r.presence==2 && r.program==trial%256);
    }
    printf("independent per-edge +/- %u us jitter stress: %u/%u rejected\n",
           jitter_us,failures,total);
  }
}

int main() {
  TestRecords();
  TestBursts();
  TestQuantizer();
  TestWire();
  printf("QuantCore host tests passed: %u checks\n",checks);
  return 0;
}
