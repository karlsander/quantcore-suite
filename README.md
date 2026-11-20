# QuantCore Suite

Firmware for the original four-channel µO_C with a Teensy 3.2 and the standard DAC8565 output circuit. QC02 includes a QWire leader, a fullscreen follower, six QuantCore utility applets and native harmony following in eight performance applets. The factory Hemisphere pair is **QC DualQ** and **QC Root**. Saved pairs take precedence.

The app menu contains **Hemispheres**, **QuantCore Leader**, **QuantCore**, **Setup / About**, **Calibr8or**, **References**, **Scale Editor**, **Waveform Editor** and **Backup / Restore**. The Hemisphere bank includes the upstream T32 performance selection plus additional quantizers, logic, tuning and MIDI utilities.

## Patch two modules

1. Open **QuantCore Leader** on the first µO_C. Patch **output A** to **TR3** on the second. QWire uses the ordinary panel jacks. Output A targets calibrated 0 V and 3.3 V.
2. Open **QuantCore** on the second µO_C. Turn either encoder to inspect the scale, chord, recall or reception counters. Harmony and recall arrive while the leader is stopped.
3. On the leader, turn the left encoder to **Root semitones** and the right encoder to **+2**. With fine tuning at zero, follower output **C** moves to D, approximately **+0.167 V**. The scale stays C major. Press **Up** on the leader to start quarter-note pulses on follower output **D**.

The suite implements QuantCore 1.0 over QWire. The USB port retains Phazerville MIDI and backup functions. Physical waveform timing, loaded output levels, input thresholds and interoperability with another module still need bench measurements.

## Fullscreen apps

**QuantCore Leader** starts stopped at the saved tempo, initially 120 BPM. Turn the left encoder to choose a field and the right encoder to edit. **Up** toggles playback at 20–300 BPM. Each start sends a reset-marked first quarter. While stopped, the leader publishes both complete records every 200 ms. A start requested during a publication waits for its final stop bit, 25 ms of quiet and legal beat spacing before sending the 7 ms prefix.

The harmony editor provides enable, signed root and fine cents, four scale presets, all 96 scale bits and all 24 chord bits. On a bit field, turn the right encoder to select the bit and press it to toggle. Bit zero is MIDI note 60. Presets replace the scale across all eight octaves. The four recall fields each span **Absent**, then **0–255**. The normal app-menu save stores the edited state and tempo. Playback always starts stopped. Outputs B–D rest at zero in Leader.

**QuantCore** continuously quantizes CV1 to the scale on **A** and CV2 to the chord on **B**. **C** outputs tuned root. **D** gives a roughly 5 ms pulse per validated quarter, including the reset-marked quarter. On the mask pages, each row is one octave from C through B. Filled cells are enabled. The diagnostics page counts accepted records, sampling gaps and discarded bursts. Prefixes and markers can produce expected discarded bursts.

## Hemisphere applets

Either slot can run any QC applet. Both slots share one receiver. TR3 carries QWire whenever a QC utility, a native following mode or an active SeQuant progression containing a QC step needs it. Ordinary gate and clock routing regains TR3 when both slots stop using QWire. A new session waits for fresh records. A silent cable retains the last valid state.

| Applet | First output | Second output | Control |
| --- | --- | --- | --- |
| QC DualQ | Quantized CV1 | Quantized CV2 | Press to select channel, turn for Scale, Chord or Both (intersection) |
| QC Root | Root pitch including fine tuning | Fine tuning alone as a pitch offset | Direct continuous outputs |
| QC Recall | Selected ID scaled linearly from 0–255 to 0–5 V | Presence gate | Turn for project, program, pattern or snapshot. Present ID zero raises the gate at 0 V on the first output |
| QC Clock | Quarter pulse | Reset pulse on a reset-marked quarter | Both pulse together on a restart |
| QC Mask | Selected scale bit as a gate | Corresponding chord bit as a gate | Turn through bits 0–95. The chord index repeats every 24 positions |
| QC State | Harmony enabled gate | Valid recall acquired gate | Display distinguishes waiting, enabled and explicitly disabled harmony |

Recall is available for inspection and CV control. Its IDs do not load or overwrite Hemisphere presets. The two masks retain their full 96- and 24-semitone periods. An empty chord makes Chord and Both use the scale. Enabled harmony with no in-range candidate holds the last CV. Disabled or unacquired harmony passes the measured CV through. Root and fine outputs return to zero while harmony is disabled.

The pitch adapter uses **0 V = MIDI note 60**, **1 V/octave**, a calibrated working range of **−3 to +6 V**, and O_C pitch units of 1/128 semitone. It chooses among in-range sounding notes and applies fine tuning once. Root output saturates at those working limits. For the nominal Hemisphere voltages, use unity attenuation, zero slew and standard 1 V/octave output scaling.

## Follow harmony in a performance applet

Double-click the Hemisphere select button for the applet you want to patch. Turn the left encoder clockwise to select its I/O settings, then scroll the right encoder past the two outputs to **Harmony**, or **Voice 1** and **Voice 2**. Press the right encoder, turn to **QC Scale** or **QC Chord**, then press again. For **SeQuant**, choose a source separately in each step’s scale field.

| Applet | What follows |
| --- | --- |
| MiniArp | Arpeggiates the full 24-note chord window in its existing directions. Pedal mode alternates tuned ROOT with chord tones, including rootless chords. CV1 and the octave setting move the chord window. Gate length stays local. |
| Carpeggio | Maps the 16 Cartesian cells to chord degrees around ROOT. A changed mask refreshes the imprint at the current position. Fine and root changes preserve degree edits and shuffle order. |
| Chordnate | A outputs tuned ROOT continuously. B quantizes CV2 to CHORD, continuously or sampled by its own trigger. ROOT can lie outside the chord. |
| Strum | CV1 selects the starting chord tone. Its six editable intervals count enabled chord tones. Direction, spacing and step mode keep working. An empty note set suppresses pitch triggers. |
| ProbMeloD | Each attack takes its weighted or looped melody relative to ROOT. Voice 1 can follow SCALE and voice 2 can follow SCALE or CHORD independently. |
| TB-3PO | Anchors the bassline to ROOT and fits each new pitch to SCALE. It keeps the pattern, accents, gate lengths and slides as harmony changes. |
| TwoRings | Each pitch output can follow SCALE or CHORD independently, including blend and pitched-trigger modes. The registers keep running. Gate and modulation outputs retain their native behavior. |
| SeQuant | Choose **QC S** or **QC C** in any step’s scale field. Its Root output follows tuned ROOT on bus steps. The existing clock, CV addressing and progression modes still select the step. |

Sources save with the on-device Hemisphere preset. The legacy 18-byte Hemisphere SysEx message carries the two applet payloads only. Older presets open in Local mode. Before the first valid harmony record, or when the leader disables harmony, the performance applets use their local behavior. SeQuant bus steps use a chromatic fallback with their saved local root. Empty CHORD falls back to SCALE. When both are empty, pitches hold and the local sequencers can keep moving.

## Build and verify

From the repository root with PlatformIO installed, run the host tests and build the default original-µO_C target.

```sh
software/test/quantcore/run.sh
cd software
pio run -e quantcore_t32
```

The Teensy Loader image is `software/.pio/build/quantcore_t32/quantcore-suite-T32-*.hex`. The other PlatformIO environments are inherited upstream configurations. The maintained GitHub workflow runs the host tests and builds the suite image.

The host tests exercise byte and pitch vectors, waveform phase and baud variation, independent record corruption, and the suite transmitter connected to its receiver at the 32 kHz sample rate. They also cover start during stopped refresh, reset spacing, clock rollover, CV bypass/hold, fine tuning and selector presence. The following tests compare the applet pitch tables against the reference quantizer for both measured CV and generated notes, and exercise source selection and preset encoding. On hardware, scope output A during OLED redraws, test TR3 cold joins and measure pitch and recall outputs with a calibrated meter. QWire publication temporarily reserves the shared DAC/display SPI bus. The display resumes between transactions.

This fork uses Phazerville’s T32 hardware branch and vendors the MIT-licensed QuantCore record, quantizer and sampled receiver implementation. [Phazerville T32 baseline](https://github.com/djphazer/O_C-Phazerville/tree/4b9843251cc26b933940e5f02aca1d78c104b758) · [QuantCore 1.0 specification and reference source](https://github.com/karlsander/quantcore/tree/374beda68fe422dc53a8024464eef69a3593a019)

## Credits

Many minds before me have made this project possible. Attribution is present in the git commit log and within individual files.

Thanks & Shoutouts:
* **[Paul Stoffregen](https://github.com/PaulStoffregen)** (PJRC) for Teensy 4.x driver code, new hardware designs, and lots of support!
* **[beau-seidon](https://github.com/beau-seidon)** for polyphonic MIDI handling, **ProbMeloD** mask rotation, **WTVCO**, and free-flowing enthusiasm.
* **[qiemem](https://github.com/qiemem)** (Bryan Head) for **Ebb&LFO** and its _tideslite_ backend, the Audio Applet framework, and many other things.
* **[Logarhythm1](https://github.com/Logarhythm1)** for the incredible **TB-3PO** sequencer, as well as **Stairs**.
* **[herrkami](https://github.com/herrkami)** and **Ben Rosenbach** for their work on **BugCrack**.
* **[benirose](https://github.com/benirose)** also gets massive props for **DrumMap**, **Shredder** and the **ProbDiv / ProbMeloD** applets.

And, of course, thank you to **[Chysn](https://github.com/Chysn)** (RIP) for the clever applet framework from which we've all drawn inspiration - what a legend!

This is a fork of [Benisphere Suite](https://github.com/benirose/O_C-BenisphereSuite) which is a fork of [Hemisphere Suite](https://github.com/Chysn/O_C-HemisphereSuite) by Jason Justian (aka Chysn / [Beige Maze](https://soundcloud.com/beige-maze)).

ornament**s** & crime**s** was a collaborative firmware project by Patrick Dowling (aka **pld**), mxmxmx, and Tim Churches (aka **bennelong.bicyclist**), considerably extending the original firmware for the o_C / ASR eurorack module, designed by **mxmxmx**.

http://ornament-and-cri.me/

## License

Except where otherwise noted in file headers, all code herein is generally considered MIT licensed. However, there are some GPLv3 bits included, so the whole thing is also subject to compliance with the GPL. [More info here](https://ornament-and-cri.me/licensing/).
