# MIDI Voice Splitter — Arduino Mega 2560

**Turns one MIDI channel into four.** Play a chord on a single-channel keyboard and the notes come out
split across four MIDI channels — the lowest note takes the lowest free channel, and **every note keeps
its channel until it really stops sounding**. No voice stealing, no re-balancing, no stuck notes.

```
Keyboard (ch 11) --> MIDI IN [shield] --> Arduino Mega 2560 --> MIDI OUT [shield] --> your gear (listens on 13-16)
```

Useful when your gear is **monophonic per track/part** (many samplers and drum machines play one note per
track), when you want a **multitimbral synth to act as a polysynth**, or simply to spread a chord over
several sounds/layers.

---

## Table of contents

- [Features](#features)
- [How voice assignment works](#how-voice-assignment-works)
- [Hardware](#hardware)
- [Wiring — read this first](#wiring--read-this-first)
- [The ON/OFF switch](#the-onoff-switch)
- [Uploading the sketch](#uploading-the-sketch)
- [Configuration](#configuration)
- [MIDI behaviour reference](#midi-behaviour-reference)
- [Modes](#modes)
- [Verification / tested](#verification--tested)
- [Troubleshooting](#troubleshooting)
- [Limitations](#limitations)
- [License](#license)

---

## Features

- **1 in → 4 out**: notes arriving on one input channel are re-emitted on four output channels, **one note
  per channel, max 4 notes at a time**.
- **Fixed assignment**: a note's channel never changes while it sounds. New notes only take *free* channels.
- **Lowest free channel first**: C3→ch13, E3→ch14, G3→ch15, C4→ch16.
- **Sustain pedal (CC64)** handled locally: a physical Note Off doesn't cut the note, and the voice keeps
  its channel until the pedal is released.
- **Impossible to hang a note**: at most one voice instance per (input channel, note), and every Note On
  sent has exactly one matching Note Off.
- **Velocity preserved**, Note On with velocity 0 treated as Note Off.
- **No aggressive voice stealing**: a 5th simultaneous note is simply dropped (configurable).
- **Robust MIDI parser**: running status, real-time bytes interleaved inside other messages, SysEx and
  System Common passthrough.
- **Selectable modes**: voice splitter, MIDI thru, MIDI monitor (analyser), and a 2-zone keyboard split.

---

## How voice assignment works

The sketch keeps a table of **4 voices**, one per output channel (13, 14, 15, 16 by default):

| field | meaning |
|---|---|
| `active` | a Note On has been sent and its Note Off hasn't yet |
| `held` | the key is physically down |
| `sustained` | key released while the sustain pedal was down (still sounding) |
| `note`, `vel`, `inCh`, `outCh`, `order` | note, velocity, source channel, assigned channel, allocation order |

**The table is indexed by (input channel, note)** — not by note alone. That single decision is what makes
stuck notes impossible:

- **New note** → takes the **lowest free output channel** and keeps it until its Note Off.
- **Note already sounding** → reuses that same voice and channel (or is ignored if the key is still down).
- **Note Off** → always sent on the channel stored *inside the voice*, so it can never go out on a
  different channel than its Note On.

### Example

```
C3 -> ch13        C4 -> ch16        (C3, E3 and G3 are not touched)
E3 -> ch14
G3 -> ch15
```

Release E3 → its Note Off goes out on **ch14** and that channel becomes free. The next new note takes
**ch14** (the lowest free channel), not ch16.

By default (`CHORD_WINDOW_MS 0`) allocation is immediate, in arrival order — which is what you want,
because keyboards send chords low-to-high. If your keyboard sends them high-to-low and you want strict
pitch ordering, set `CHORD_WINDOW_MS 15`: notes arriving within 15 ms are grouped and assigned sorted by
pitch (at the cost of 15 ms latency).

---

## Hardware

- **Arduino Mega 2560** (works on an Uno too, same pins; the Mega just keeps the USB free if you move MIDI
  to `Serial1`).
- A **MIDI shield/module** with a 6N138 (or similar) opto input and a 5-pin DIN input/output — the classic
  cheap "MIDI shield" clone with IN / OUT / THRU and a RUN/PROG switch.
- **5 male-male dupont jumper wires** — see the warning below.
- A MIDI keyboard and whatever gear will receive the notes.

### MIDI shield pinout used

| Signal | Mega pin |
|---|---|
| MIDI IN | **D0 / RX0** (through the shield's ON/OFF switch) |
| MIDI OUT | **D1 / TX0** |
| MIDI THRU | hardware copy of IN (the sketch doesn't need it) |

---

## Wiring — read this first

> ⚠️ **Many Arduino clones (and many shields) use female headers on both sides.** Two female headers cannot
> mate: the shield sits on top, *looks* perfectly mounted, and **makes no electrical contact at all**. The
> USB keeps working (it doesn't go through the shield), which makes this maddening to debug: MIDI goes
> nowhere, the upload fails for no obvious reason, and even the shield's RESET button does nothing.

If your board has female headers, join them with **5 male-male dupont wires** (or male breakaway pin
strips used as an adapter), connecting **by name**:

```
        ARDUINO MEGA 2560                        MIDI SHIELD
   ┌───────────────────────────┐          ┌───────────────────────────┐
   │ 5V    ●───────────────────┼──────────┼───● 5V                    │
   │ GND   ●───────────────────┼──────────┼───● GND                   │
   │ 0/RX  ●───────────────────┼──────────┼───● 0 / RX                │
   │ 1/TX  ●───────────────────┼──────────┼───● 1 / TX                │
   │ RESET ●───────────────────┼──────────┼───● RESET      (optional) │
   └───────────────────────────┘          └───────────────────────────┘
                                                       │
                                          MIDI IN ●────┴────● MIDI OUT
                                         (keyboard)        (to your gear, listens on 13-16)
```

- On the Mega, `5V`/`GND` are in the power header (`RESET 3.3V 5V GND GND VIN`) and `0`/`1` in the digital
  header next to `AREF GND 13 12 11…`.
- `RESET` is optional: it only makes the shield's own reset button work.
- **Connect to the shield's `OUT`, not its `THRU`.** The THRU outputs the original notes on the input
  channel by hardware, which would duplicate every voice.
- Do the wiring **with the USB unplugged**, and double-check `5V`↔`5V` and `GND`↔`GND`.

## The ON/OFF switch

The shield's switch decides whether the MIDI input circuit is connected to the Arduino's RX pin:

| Position | Effect |
|---|---|
| **OFF** (a.k.a. PROG) | MIDI IN disconnected from RX → **you can upload sketches**; the PC can talk to the chip |
| **ON** (a.k.a. RUN) | MIDI IN connected → **MIDI works**, but uploading fails (`stk500v2_getsync() failed`) because the MIDI circuit loads the RX line |

So: **switch OFF to upload, switch ON to play.** This is normal for this kind of shield, not a fault.

---

## Uploading the sketch

1. Put the shield's switch in **OFF**.
2. Arduino IDE → *Tools* → Board **Arduino Mega or Mega 2560**, Processor **ATmega2560**, and the COM port.
3. Upload. (The sketch folder and the `.ino` must have the same name — that's why the file lives in
   `MIDI_VoiceSplitter_Mega2560/`.)
4. Put the switch back to **ON** to play.

**Serial Monitor: 31250 baud**, not 115200 — the MIDI output shares D0/D1 with the USB link.

---

## Configuration

Everything lives in **section 1** of the `.ino`:

| Constant | Default | What it does |
|---|---|---|
| `SPLITTER_MODE` | `MODE_VOICE_SPLIT` | splitter / thru / monitor / 2-zone split |
| `DEBUG` | `0` | `0` = production (nothing is sent over the MIDI cable). `1` = logs, wrapped in SysEx so MIDI gear ignores them. |
| `INPUT_CHANNEL` | `11` | Channel of the **notes** to split (0 = accept notes from every channel) |
| `OUTPUT_CH_FIRST` / `OUTPUT_CH_LAST` | `13` / `16` | Output channels = polyphony (4 voices). Change to `1`/`8` for 8 voices, etc. |
| `CHORD_WINDOW_MS` | `0` | `0` = immediate; `15` = group + sort the chord by pitch (15 ms latency) |
| `DUP_NOTE_MODE` | `DUP_NOTE_IGNORE` | Repeated note before its Note Off: ignore it, or `DUP_NOTE_RETRIGGER` to re-send the Note On on the **same** channel |
| `OVERFLOW_MODE` | `OVERFLOW_DROP_NEWEST` | 5th note: drop it, or `OVERFLOW_STEAL_OLDEST` to steal the oldest voice |
| `FORWARD_CC64` | `0` | Sustain is handled locally and not forwarded |
| `FORWARD_SYSTEM_RESET` | `0` | Don't forward the spurious `0xFF` (System Reset) that this wiring injects when the PC opens the serial port |
| `NOTE_OFF_STYLE` | `0` | `0` = `0x80` Note Off with release velocity; `1` = Note On with velocity 0 |
| `MIDI_IO_MODE` | `MIDI_IO_SERIAL0` | `D0/D1`, `Serial1` (D19/D18), `Serial2`, `Serial3` or SoftwareSerial |

> The source comments are in Spanish (the author's language). The constants above are the full reference.

---

## MIDI behaviour reference

| Incoming | What the sketch does |
|---|---|
| Note On / Note Off (input channel) | Allocated / released as described above |
| **Pitch Bend, Channel Pressure, CC (except 64), Program Change** | Sent to **all output channels**. Controllers are accepted from *any* input channel, because keyboards often send the wheels on a different channel than the notes. |
| **Polyphonic Aftertouch** | Carries a note number → sent only on the channel where that note is sounding; dropped otherwise |
| **CC64 (Sustain)** | Handled internally, not forwarded (`FORWARD_CC64 1` to also forward it) |
| **CC120 / CC123** (all sound off / all notes off) | Releases every voice with its proper Note Off and broadcasts the CC |
| **CC121** (reset all controllers) | Releases the pedal and any sustained voices |
| **SysEx, System Common, Clock/Start/Stop/Continue** | Passed through untouched |
| **Real-time bytes (0xF8-0xFF)** | Forwarded immediately; they never disturb the parser state |
| **`0xFF` System Reset** | Filtered out by default (see `FORWARD_SYSTEM_RESET`) |

---

## Modes

Set `SPLITTER_MODE`:

| Mode | What it does |
|---|---|
| `MODE_VOICE_SPLIT` | The voice splitter (default) |
| `MODE_THRU` | Plain MIDI THRU (byte for byte) |
| `MODE_MONITOR` | MIDI analyser: prints everything it receives, on any channel, to the serial port at 31250 baud. Very handy to check wiring, the keyboard's channel, etc. |
| `MODE_SPLIT_ZONES` | Keyboard split in two zones: notes below `SPLIT_POINT_NOTE` go to the lower channels, the rest to the upper ones |

---

## Verification / tested

Tested on an Arduino Mega 2560 (genuine, `VID_2341&PID_0010`) with a generic 6N138 MIDI shield and a
5-pin MIDI keyboard.

**Build** (`arduino-cli 1.5.1`, core `arduino:avr 1.8.8`, board `arduino:avr:mega`, `--warnings all`):

```
Sketch uses 3050 bytes (1%) of program storage space.
Global variables use 244 bytes (2%) of dynamic memory.
warnings: none
```

**Controlled functional test — 16/16 byte-exact:**

| Test | Expected | Result |
|---|---|---|
| C3, E3, G3, C4 on the input channel | `9C 30 64`, `9D 34 64`, `9E 37 64`, `9F 3C 64` | ✅ |
| 5th simultaneous note | nothing (dropped) | ✅ |
| Note Off of E3 | `8D 34 40` (its own channel) | ✅ |
| New note after that | `9D 3E 64` (reuses the lowest free channel) | ✅ |
| Sustain ON + physical Note Off | nothing (note keeps sounding) | ✅ |
| Sustain OFF | `8C 30 00` and `8E 37 00` | ✅ |
| Pitch Bend arriving on another channel | replicated to all 4 outputs | ✅ |
| `0xFF` System Reset | nothing (filtered) | ✅ |
| CC123 | `BC/BD/BE/BF 7B 00` | ✅ |

**Live test with a real keyboard (25 s of playing):** 4648 bytes, **99.85 % of them valid MIDI messages**
(Note On 188, Note Off 183, CC 412, Pitch Bend 764, spread over the four output channels), **0 System
Resets**, and every Note On closed by its Note Off.

Thirteen build configurations are compiled (the four modes, `DEBUG 1`, monitor with real-time bytes,
SoftwareSerial, Serial1, chord window, duplicate/steal/CC64 variants, 8 and 16 voices, all-channels input).

---

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| Upload fails with `stk500v2_getsync() failed` | The shield's switch is in **ON**. Put it in OFF. |
| `cannot set com-state for \\.\COM5` / "device is not functioning" | Stuck USB-serial state: unplug the USB and plug it back in (different port, no hub). |
| Nothing arrives from the keyboard | Check the 5 dupont wires (especially `0/RX`, `5V`, `GND`), that the cable is in the shield's **IN**, and that the keyboard transmits on the channel set in `INPUT_CHANNEL`. Use `SPLITTER_MODE MODE_MONITOR` + `DEBUG 1` and open the monitor at **31250** — you'll see every message that arrives, on any channel. |
| My gear plays nothing | It must listen on the output channels (`13-16` by default), and the cable must come from the shield's **OUT** (not THRU). |
| Only 4 notes sound, the 5th is silent | By design (see `OVERFLOW_MODE`). |
| Notes hang on my synth | Check that the destination's own sustain/`local` settings aren't holding them; send CC123 from the keyboard to panic. |

---

## Limitations

1. **MIDI and USB share D0/D1**, which is why there is a switch, a `DEBUG 0` default and the `0xFF` filter.
   If you want clean debugging over USB while MIDI runs, move the MIDI I/O to `Serial1` (pins 18/19) —
   `MIDI_IO_MODE MIDI_IO_SERIAL1`.
2. **Polyphony = number of output channels.** 13-16 gives 4 voices; use `1`/`8` for 8, etc.
3. **The 5th simultaneous note is dropped**, not stolen.
4. **All four output channels receive the same controllers** (pitch bend, CC…). That's intentional: it's
   the only way an effect is guaranteed to reach whichever channel a note is playing on.
5. **Dupont wiring is functional but not mechanically rugged.** For a permanent build, use male
   breakaway pin strips as an adapter, or replace the Arduino's female header rows with male pins.

---

## License

Code: **MIT** (see `LICENSE`). Documentation: **CC BY-SA 4.0**.

If you build one, a photo or a note about what gear you're driving with it is always welcome.
