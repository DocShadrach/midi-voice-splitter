/* ============================================================================
 *  MIDI VOICE SPLITTER / ROUTER  --  ARDUINO MEGA 2560
 * ============================================================================
 *
 *  WHAT IT DOES
 *  ------------
 *  It comes in through MIDI IN on CHANNEL 11 and goes out through MIDI OUT,
 *  splitting the notes among channels 13, 14, 15 and 16 (4 voices maximum).
 *  It keeps the FIXED allocation of each note until that note really stops
 *  sounding (real Note Off + sustain resolved).  Voices that are already
 *  sounding are never rearranged: there is NO "voice rebalancing".
 *
 *  MODES (constant SPLITTER_MODE, section 1.1)
 *  -------------------------------------------
 *    MODE_VOICE_SPLIT : voice splitter (main function)
 *    MODE_THRU        : pure MIDI THRU (IN -> OUT byte by byte)
 *    MODE_MONITOR     : MIDI monitor/analyzer on the serial port  <-- extra mode
 *    MODE_SPLIT_ZONES : 2-zone keyboard split (extra, 2 voices per hand)
 *
 *  PRODUCTION SETTINGS (already set as default)
 *  --------------------------------------------
 *    DEBUG 0                -> no text at all travels over the MIDI cable
 *    FORWARD_SYSTEM_RESET 0 -> the spurious 0xFF (System Reset) is not resent
 *    Input buffer flush at startup
 *
 *  WIRING
 *  ------
 *    MIDI keyboard (channel 11) --> MIDI IN of the shield --> Arduino Mega 2560
 *    Arduino Mega 2560 --> MIDI OUT of the shield --> destination device
 *
 *    All the notes coming in on CHANNEL 11 go out split among the
 *    channels 13, 14, 15 and 16.  At most 4 simultaneous notes (4 voices),
 *    one per channel, and the 5th note that arrives does not sound.  The
 *    destination has to listen on those 4 channels.
 *
 *    >>> Connect ONLY the shield's OUT connector to the destination; the
 *        THRU connector carries the note on channel 11 and would duplicate.
 *
 *  HARDWARE OF THIS BUILD (identified in the photos of the shield)
 *  ---------------------------------------------------------------
 *    MIDI IN  -> Arduino D0  (RX0)   (goes through the ON/OFF switch "RX(S2)")
 *    MIDI OUT -> Arduino D1  (TX0)
 *    MIDI THRU-> hardware copy of IN (this sketch does not generate it)
 *    Optocoupler 6N138 + 220R resistors, RESET button (S1)
 *
 *    >>> IMPORTANT CONSEQUENCE (read section 1.2 and the README):
 *        as port D0/D1 is shared with the USB, any DEBUG text
 *        would also come out of the MIDI OUT connector.  That is why, when
 *        the MIDI is on D0/D1, the debug is sent WRAPPED IN SysEx (ID 0x7D),
 *        which the synthesizers must ignore.  In the Serial Monitor it reads
 *        the same (at 31250 baud); you only see 3 odd characters per line.
 *
 *  HOW TO UPLOAD IT
 *  ----------------
 *   1) Set the shield's switch to OFF (disconnects RX from the MIDI circuit).
 *   2) Arduino IDE: board "Arduino Mega or Mega 2560", processor "ATmega2560".
 *   3) Upload the sketch, then set the switch back to ON to play.
 *   4) Serial Monitor: 31250 baud if the MIDI is on D0/D1 (or 115200 if
 *      the MIDI were on Serial1/2/3).
 *
 *  Author: Doc Shadrach  (https://github.com/DocShadrach)
 *  License: MIT.  Original comments were in Spanish.
 * ==========================================================================*/


/* ===========================================================================
 * 1. CONFIGURATION
 * ======================================================================== */

/* ---- 1.1 Working mode -----------------------------------------------------
 * Pick ONE of these four:
 *   MODE_VOICE_SPLIT  -> splits the notes among the output channels
 *   MODE_THRU         -> pure MIDI THRU
 *   MODE_MONITOR      -> MIDI monitor over USB (it does not send notes)
 *   MODE_SPLIT_ZONES  -> note < SPLIT_POINT_NOTE  -> low channels
 *                        note >= SPLIT_POINT_NOTE -> high channels
 * ------------------------------------------------------------------------ */
#define MODE_VOICE_SPLIT  1
#define MODE_THRU         2
#define MODE_MONITOR      3
#define MODE_SPLIT_ZONES  4

#define SPLITTER_MODE   MODE_VOICE_SPLIT

/* ---- 1.2 Debug -----------------------------------------------------------
 * DEBUG 0 = NO logs.  This is the production setting: no text travels over the
 *           MIDI cable.  Set it to 1 only to diagnose.
 * DEBUG 1 = logs on the serial port.
 *
 * DEBUG_AUTO_SAFE 1 = if the debug shares the cable with the MIDI (the D0/D1
 *   case, which is this shield), the text is sent inside SysEx messages with
 *   the manufacturer ID 0x7D ("non-commercial use"), so that the synthesizer
 *   ignores them and the MIDI stream is still legal.  The text reads the same
 *   in the Serial Monitor (3 non-printable characters appear per line).
 *   If you set it to 0, the text comes out RAW through the MIDI OUT connector:
 *   that can dirty the synthesizer stream.  Only for one-off diagnostics.
 * ------------------------------------------------------------------------ */
#define DEBUG 0
#define DEBUG_AUTO_SAFE 1
#define DEBUG_BAUD 115200          /* baud rate when the USB is free */

/* ---- 1.3 Output channels / polyphony -------------------------------------
 * MIDI channels 13..16  ->  4 voices, just as you asked for.
 * The polyphony is worked out by itself: VOICE_COUNT = LAST - FIRST + 1.
 * ------------------------------------------------------------------------ */
#define OUTPUT_CH_FIRST  13
#define OUTPUT_CH_LAST   16

/* ---- 1.4 Chord window ----------------------------------------------------
 * 0  = IMMEDIATE allocation in arrival order (0 ms of latency).
 *      The rule "lowest note -> lowest free channel" holds for the
 *      order in which the notes arrive (keyboards usually send chords
 *      from low to high, so the result is the one you are after).
 * 15 = groups the notes arriving within 15 ms and assigns them sorted by
 *      pitch, with 15 ms of latency.  Useful if your keyboard sends chords
 *      from high to low and you want the order by pitch guaranteed.
 * ------------------------------------------------------------------------ */
#define CHORD_WINDOW_MS 0

/* ---- 1.5 Duplicate notes -------------------------------------------------
 * MIDI allows receiving the same note twice before the Note Off.
 *   DUP_NOTE_IGNORE    : the second Note On is ignored (a single instance).
 *   DUP_NOTE_RETRIGGER : the Note On is forwarded on the SAME channel (same
 *                        voice, a single instance, a single Note Off).
 * The voice table stores at most ONE instance per (input channel,
 * note), so it is IMPOSSIBLE to leave a stuck note because of duplicates.
 * ------------------------------------------------------------------------ */
#define DUP_NOTE_IGNORE    0
#define DUP_NOTE_RETRIGGER 1
#define DUP_NOTE_MODE      DUP_NOTE_IGNORE

/* ---- 1.6 Polyphony overflow ----------------------------------------------
 *   OVERFLOW_DROP_NEWEST  : the note that does not fit does NOT sound (its
 *                           Note Off is ignored). Safe, no robbing.  DEFAULT.
 *   OVERFLOW_STEAL_OLDEST : steals the oldest voice (sends it its Note Off and
 *                           reuses its channel).  Not aggressive, but it cuts.
 * ------------------------------------------------------------------------ */
#define OVERFLOW_DROP_NEWEST  0
#define OVERFLOW_STEAL_OLDEST 1
#define OVERFLOW_MODE         OVERFLOW_DROP_NEWEST

/* ---- 1.7 Sustain and misc ------------------------------------------------ */
#define SUSTAIN_CC            64   /* CC64 = sustain pedal */
#define FORWARD_CC64          0    /* 1 = besides handling it, forwards CC64 */
#define FORWARD_SYSTEM_RESET  0    /* 0 = does NOT forward 0xFF (System Reset).
                                    * In this build, opening the PC serial port
                                    * lets a spurious 0xFF slip in through the
                                    * MIDI IN; forwarding it would reset the
                                    * destination device.  Set it to 1 if you
                                    * really need the System Reset. */
#define NOTE_OFF_STYLE        0    /* 0 = 0x80 with release velocity
                                    * 1 = 0x90 with velocity 0 */
#define MONITOR_THRU          1    /* in MODE_MONITOR, 1 = also does THRU */
#define MONITOR_SHOW_REALTIME 0    /* 1 = prints clock/active sensing (lots of
                                    * output; by default they are silenced) */
#define SPLIT_POINT_NOTE      60   /* only for MODE_SPLIT_ZONES (60 = C4) */

/* ---- 1.7b Keyboard input channel -----------------------------------------
 *   Your keyboard sends the NOTES on channel 11, so only those notes are
 *   accepted: a stray note from another channel cannot take up a voice.
 *   The rest of the messages (CC, pedal, pitch bend, aftertouch, program
 *   change) ARE accepted from any channel, because keyboards sometimes send
 *   the wheels on a channel other than the note one; so they are not lost.
 *   0 = also accept the notes from ALL the channels.
 * ------------------------------------------------------------------------ */
#define INPUT_CHANNEL         11

/* ---- 1.8 MIDI port: PICK YOURS -------------------------------------------
 *   MIDI_IO_SERIAL0  -> D0 (RX) / D1 (TX)   <-- THIS SHIELD (SparkFun clone)
 *   MIDI_IO_SERIAL1  -> D19 (RX1) / D18 (TX1)   (ideal on the Mega: USB free)
 *   MIDI_IO_SERIAL2  -> D17 (RX2) / D16 (TX2)
 *   MIDI_IO_SERIAL3  -> D15 (RX3) / D14 (TX3)
 *   MIDI_IO_SOFTWARE -> SoftwareSerial on MIDI_SOFT_RX_PIN / MIDI_SOFT_TX_PIN
 *
 * CAREFUL with SoftwareSerial on the MEGA: the RX pin HAS to support
 * pin-change interrupts (PCINT).  On the Mega only these are valid:
 * 10, 11, 12, 13, 50, 51, 52, 53 and A8..A15.  Pins 2 and 3 do NOT work
 * (that is an Uno thing).  The TX pin can be any one you like.
 * ------------------------------------------------------------------------ */
#define MIDI_IO_SERIAL0   0
#define MIDI_IO_SERIAL1   1
#define MIDI_IO_SERIAL2   2
#define MIDI_IO_SERIAL3   3
#define MIDI_IO_SOFTWARE  4

#define MIDI_IO_MODE      MIDI_IO_SERIAL0
#define MIDI_BAUD         31250
#define MIDI_SOFT_RX_PIN  10
#define MIDI_SOFT_TX_PIN  11


/* ===========================================================================
 * 2. PORTS AND INCLUDES
 * ======================================================================== */

#include <stdio.h>          /* snprintf for the logs */
#include <SoftwareSerial.h>

#if MIDI_IO_MODE == MIDI_IO_SERIAL0
  #define MIDI_PORT Serial
#elif MIDI_IO_MODE == MIDI_IO_SERIAL1
  #define MIDI_PORT Serial1
#elif MIDI_IO_MODE == MIDI_IO_SERIAL2
  #define MIDI_PORT Serial2
#elif MIDI_IO_MODE == MIDI_IO_SERIAL3
  #define MIDI_PORT Serial3
#elif MIDI_IO_MODE == MIDI_IO_SOFTWARE
  SoftwareSerial midiSoftSerial(MIDI_SOFT_RX_PIN, MIDI_SOFT_TX_PIN);
  #define MIDI_PORT midiSoftSerial
#else
  #error "MIDI_IO_MODE is not valid: check section 1.8"
#endif

/* The MIDI shares the UART with the USB: the debug text would also go to the
 * MIDI OUT connector.  In that case it is sent wrapped in SysEx. */
#if DEBUG && DEBUG_AUTO_SAFE && (MIDI_IO_MODE == MIDI_IO_SERIAL0)
  #define DEBUG_WRAPPED 1
#else
  #define DEBUG_WRAPPED 0
#endif

/* If the MIDI does NOT use D0/D1, the USB is free and DEBUG_BAUD is used. */
#if (DEBUG || (SPLITTER_MODE == MODE_MONITOR)) && (MIDI_IO_MODE != MIDI_IO_SERIAL0)
  #define USB_SERIAL_NEEDED 1
#else
  #define USB_SERIAL_NEEDED 0
#endif


/* ===========================================================================
 * 3. CONSTANTS, TYPES AND VOICE TABLE
 * ======================================================================== */

#define VOICE_COUNT     (OUTPUT_CH_LAST - OUTPUT_CH_FIRST + 1)
#define PENDING_MAX     8
#define CH_MAX          16

/* MIDI channel number "as seen by the user" (1..16) -> nibble (0..15) */
#define CH_NIBBLE(ch)   ((uint8_t)((ch) - 1))

/* One voice = one output channel taken up by one note.
 * It is the structure you asked for, with a couple of extra fields:
 *   held      -> the key is physically pressed
 *   sustained -> the key has been released, but the pedal keeps it sounding
 * A voice is "sounding" while active == true.  The channel is only released
 * when active goes to false. */
struct Voice {
  bool     active;      /* a Note On has been sent and its Note Off is pending */
  bool     held;        /* key pressed right now */
  bool     sustained;   /* released with the pedal down (still sounding) */
  uint8_t  note;        /* MIDI note 0..127 */
  uint8_t  vel;         /* velocity of the Note On */
  uint8_t  inCh;        /* input MIDI channel (nibble 0..15) */
  uint8_t  outCh;       /* output MIDI channel  (nibble 0..15) */
  uint32_t order;       /* allocation order (for voice stealing and debug) */
};

Voice    voices[VOICE_COUNT];
uint32_t voiceOrderCounter = 0;
bool     sustainDown = false;

/* Queue for the chord window (only used if CHORD_WINDOW_MS > 0) */
struct PendingNote { uint8_t inCh; uint8_t note; uint8_t vel; };
PendingNote pending[PENDING_MAX];
uint8_t     pendingCount  = 0;
uint32_t    windowStartMs = 0;


/* ===========================================================================
 * 4. PROTOTYPES
 * ======================================================================== */

char *noteName(uint8_t n);
void  parseByte(uint8_t b);
void  handleMessage(uint8_t status, const uint8_t *d, uint8_t len);
void  writeMessage(uint8_t status, const uint8_t *d, uint8_t len);
void  routeRawByte(uint8_t b);
void  routeRealtime(uint8_t b);
void  broadcastMessage(uint8_t typeHi, uint8_t d1, uint8_t d2, uint8_t len);
void  allocateAndStart(uint8_t inCh, uint8_t note, uint8_t vel);
void  voiceNoteOff(uint8_t inCh, uint8_t note, uint8_t relVel);
void  queueNoteOn(uint8_t inCh, uint8_t note, uint8_t vel);
void  flushPending();
void  setSustain(uint8_t value);
void  releaseSustainedVoices();
void  panicAllVoices();
void  polyAftertouch(uint8_t inCh, uint8_t note, uint8_t value);
void  controlChange(uint8_t inCh, uint8_t cc, uint8_t value);
void  serialInitAndBanner();
void  zoneRange(uint8_t note, uint8_t &first, uint8_t &last);
int8_t findVoiceIndex(uint8_t inCh, uint8_t note);
int8_t allocSlot(uint8_t note);
int8_t stealOldestSlot(uint8_t note);
void  freeVoice(uint8_t idx);
void  sendNoteOn(uint8_t chNibble, uint8_t note, uint8_t vel);
void  sendNoteOff(uint8_t chNibble, uint8_t note, uint8_t vel);


/* ===========================================================================
 * 5. UTILITIES
 * ======================================================================== */

/* "C4", "F#3"...  (note 60 = C4, scientific convention)
 * It uses TWO alternating buffers so that two note names can be printed on the
 * same log line (with a single buffer, the second would hide the first). */
char *noteName(uint8_t n) {
  static const char base[12]  = {'C','C','D','D','E','F','F','G','G','A','A','B'};
  static const bool sharp[12] = {false,true,false,true,false,false,true,
                                 false,true,false,true,false};
  static char    buf[2][8];
  static uint8_t slot = 0;
  char   *b;
  uint8_t i = 0;
  int8_t  oct;

  slot ^= 1;
  b = buf[slot];

  b[i++] = base[n % 12];
  if (sharp[n % 12]) b[i++] = '#';

  oct = (int8_t)(n / 12) - 1;
  if (oct < 0) { b[i++] = '-'; oct = (int8_t)(-oct); }
  if (oct >= 10) b[i++] = (char)('0' + (oct / 10));
  b[i++] = (char)('0' + (oct % 10));
  b[i]   = '\0';
  return b;
}

/* MIDI channel number (1..16) of a voice */
#define VOICE_CH_NUM(i)  ((uint8_t)(OUTPUT_CH_FIRST + (i)))


/* ===========================================================================
 * 6. DEBUG
 * ======================================================================== */

#if DEBUG
  static char dbgBuf[112];

  static void dbgSend() {
#if DEBUG_WRAPPED
    /* SysEx with the manufacturer ID 0x7D (non-commercial use): legal over MIDI
     * and the synthesizers ignore it.  The text stays readable in the monitor. */
    Serial.write((uint8_t)0xF0);
    Serial.write((uint8_t)0x7D);
    Serial.print(dbgBuf);
    Serial.write((uint8_t)0xF7);
#else
    Serial.println(dbgBuf);
#endif
  }
  #define LOG(...) do { snprintf(dbgBuf, sizeof(dbgBuf), __VA_ARGS__); dbgSend(); } while (0)
#else
  #define LOG(...) do { } while (0)
#endif


/* ===========================================================================
 * 7. MIDI PARSER
 * ---------------------------------------------------------------------------
 *  - It supports running status.
 *  - Real-time bytes (0xF8..0xFF) are forwarded instantly and do NOT
 *    alter the parser state (important: they can slip in the middle of
 *    any message).
 *  - SysEx (0xF0..0xF7) and System Common (0xF1..0xF6) are forwarded as is.
 *  - An orphan data byte (no status) is discarded; a valid message is never
 *    lost because of the parser.
 * ======================================================================== */

uint8_t rxStatus    = 0;      /* active running status (0 = none) */
uint8_t rxData[2]   = {0,0};
uint8_t rxCount     = 0;
uint8_t rxNeed      = 0;      /* data bytes expected by the current status */
bool    rxSysEx     = false;
uint8_t rxSysCommon = 0;      /* System Common status in progress */
uint8_t rxSysLeft   = 0;      /* data bytes left for that System Common */

uint8_t expectedDataBytes(uint8_t status) {
  switch (status & 0xF0) {
    case 0x80: case 0x90: case 0xA0: case 0xB0: case 0xE0: return 2;
    case 0xC0: case 0xD0: return 1;
    default: return 0;
  }
}

uint8_t sysCommonLen(uint8_t status) {
  switch (status) {
    case 0xF1: return 1;   /* MTC quarter frame  */
    case 0xF2: return 2;   /* Song position      */
    case 0xF3: return 1;   /* Song select        */
    default:   return 0;   /* 0xF4, 0xF5, 0xF6   */
  }
}

void parseByte(uint8_t b) {

  /* --- Real time: 0xF8..0xFF (clock, start, stop, active sensing) --------- */
  if (b >= 0xF8) { routeRealtime(b); return; }

  /* --- Status byte ------------------------------------------------------- */
  if (b & 0x80) {
    if (b == 0xF0) {                      /* start of SysEx */
      rxSysEx = true; rxStatus = 0; rxCount = 0; rxSysCommon = 0;
      routeRawByte(b);
      return;
    }
    if (b == 0xF7) {                      /* end of SysEx (EOX) */
      rxSysEx = false; rxStatus = 0; rxCount = 0; rxSysCommon = 0;
      routeRawByte(b);
      return;
    }
    rxSysEx = false;                      /* any other status aborts SysEx */

    if (b >= 0xF0) {                      /* System Common 0xF1..0xF6 */
      rxSysCommon = b;
      rxSysLeft   = sysCommonLen(b);
      rxStatus    = 0;
      rxCount     = 0;
      routeRawByte(b);
      if (rxSysLeft == 0) rxSysCommon = 0;
      return;
    }

    rxStatus = b;                         /* channel status (new running) */
    rxCount  = 0;
    rxNeed   = expectedDataBytes(b);
    if (rxNeed == 0) rxStatus = 0;
    return;
  }

  /* --- Data byte --------------------------------------------------------- */
  if (rxSysEx)     { routeRawByte(b); return; }
  if (rxSysCommon) { routeRawByte(b); if (--rxSysLeft == 0) rxSysCommon = 0; return; }
  if (rxStatus == 0) return;              /* orphan data byte: discarded */

  rxData[rxCount++] = b;
  if (rxCount >= rxNeed) {
    handleMessage(rxStatus, rxData, rxNeed);
    rxCount = 0;                          /* running status: the status stays */
  }
}

/* Loose byte (SysEx / System Common / real time) */
void routeRawByte(uint8_t b) {
#if SPLITTER_MODE == MODE_MONITOR
  #if MONITOR_THRU
    MIDI_PORT.write(b);
  #endif
#else
  MIDI_PORT.write(b);
#endif
}

/* Output of a real-time byte (clock, start/stop/continue, active
 * sensing...).  0xFF (System Reset) is filtered out by default: see
 * FORWARD_SYSTEM_RESET in section 1.7. */
void writeRealtime(uint8_t b) {
#if !FORWARD_SYSTEM_RESET
  if (b == 0xFF) return;
#endif
  MIDI_PORT.write(b);
}

void routeRealtime(uint8_t b) {
#if SPLITTER_MODE == MODE_MONITOR
  #if MONITOR_SHOW_REALTIME
    monitorPrintRealtime(b);
  #endif
  #if MONITOR_THRU
    writeRealtime(b);
  #endif
#else
  writeRealtime(b);        /* clock / start / stop / continue / active sensing */
#endif
}

void writeMessage(uint8_t status, const uint8_t *d, uint8_t len) {
  MIDI_PORT.write(status);
  for (uint8_t i = 0; i < len; i++) MIDI_PORT.write(d[i]);
}


/* ===========================================================================
 * 8. MIDI OUTPUT
 * ======================================================================== */

void sendNoteOn(uint8_t chNibble, uint8_t note, uint8_t vel) {
  MIDI_PORT.write((uint8_t)(0x90 | (chNibble & 0x0F)));
  MIDI_PORT.write(note);
  MIDI_PORT.write(vel);
}

void sendNoteOff(uint8_t chNibble, uint8_t note, uint8_t vel) {
#if NOTE_OFF_STYLE == 1
  MIDI_PORT.write((uint8_t)(0x90 | (chNibble & 0x0F)));
  MIDI_PORT.write(note);
  MIDI_PORT.write((uint8_t)0);
#else
  MIDI_PORT.write((uint8_t)(0x80 | (chNibble & 0x0F)));
  MIDI_PORT.write(note);
  MIDI_PORT.write(vel);
#endif
}

/* Forwards a channel message to ALL the output channels.
 * It is used for Pitch Bend, channel Aftertouch, CC (except 64), Program
 * Change...  So, whatever channel a note is sounding on, the effect
 * reaches it too.  It is deterministic and leaves no half-done states. */
void broadcastMessage(uint8_t typeHi, uint8_t d1, uint8_t d2, uint8_t len) {
  for (uint8_t i = 0; i < VOICE_COUNT; i++) {
    MIDI_PORT.write((uint8_t)(typeHi | CH_NIBBLE(VOICE_CH_NUM(i))));
    MIDI_PORT.write(d1);
    if (len > 1) MIDI_PORT.write(d2);
  }
}


/* ===========================================================================
 * 9. VOICE TABLE AND CHANNEL ALLOCATION
 * ---------------------------------------------------------------------------
 *  Rule: when CREATING a voice the LOWEST FREE channel of the range is chosen.
 *  Once created, the allocation is never touched again until its Note Off.
 * ======================================================================== */

int8_t findVoiceIndex(uint8_t inCh, uint8_t note) {
  for (uint8_t i = 0; i < VOICE_COUNT; i++) {
    if (voices[i].active && voices[i].inCh == inCh && voices[i].note == note)
      return (int8_t)i;
  }
  return -1;
}

void freeVoice(uint8_t idx) {
  voices[idx].active    = false;
  voices[idx].held      = false;
  voices[idx].sustained = false;
}

/* Usable slot range according to the zone (only changes in MODE_SPLIT_ZONES) */
void zoneRange(uint8_t note, uint8_t &first, uint8_t &last) {
  first = 0;
  last  = (uint8_t)(VOICE_COUNT - 1);
#if SPLITTER_MODE == MODE_SPLIT_ZONES
  if (note < SPLIT_POINT_NOTE) {
    first = 0;
    last  = (uint8_t)(VOICE_COUNT / 2 - 1);
  } else {
    first = (uint8_t)(VOICE_COUNT / 2);
    last  = (uint8_t)(VOICE_COUNT - 1);
  }
#else
  (void)note;
#endif
}

int8_t allocSlot(uint8_t note) {
  uint8_t first, last;
  zoneRange(note, first, last);
  for (uint8_t i = first; i <= last; i++) {
    if (!voices[i].active) return (int8_t)i;   /* lowest free channel */
  }
  return -1;
}

int8_t stealOldestSlot(uint8_t note) {
  uint8_t  first, last;
  int8_t   best = -1;
  uint32_t bestOrder = 0xFFFFFFFFUL;

  zoneRange(note, first, last);
  for (uint8_t i = first; i <= last; i++) {
    if (voices[i].active && voices[i].order < bestOrder) {
      bestOrder = voices[i].order;
      best      = (int8_t)i;
    }
  }
  if (best < 0) return -1;

  LOG("Overflow: I steal the voice CH%d (note %s) for %s",
      VOICE_CH_NUM(best), noteName(voices[best].note), noteName(note));
  sendNoteOff(voices[best].outCh, voices[best].note, 0);
  freeVoice((uint8_t)best);
  return best;
}


/* ===========================================================================
 * 10. NOTE ON  (voice creation)
 * ---------------------------------------------------------------------------
 *  Priorities: do not hang notes, do not reallocate busy voices, no retrigger
 *  of notes that are already sounding, do not rearrange the chord.
 * ======================================================================== */

void allocateAndStart(uint8_t inCh, uint8_t note, uint8_t vel) {

  /* --- 10.1 Is there a voice for this note on this input channel? --------- */
  int8_t v = findVoiceIndex(inCh, note);

  if (v >= 0) {
    if (voices[v].held) {
      /* Duplicate note: the key was already pressed. */
#if DUP_NOTE_MODE == DUP_NOTE_RETRIGGER
      voices[v].vel = vel;
      sendNoteOn(voices[v].outCh, note, vel);
      LOG("Dup    in=%d %s(%d) v=%d -> retrigger CH%d (same voice)",
          inCh + 1, noteName(note), note, vel, voices[v].outCh + 1);
#else
      LOG("Dup    in=%d %s(%d) v=%d -> ignored (already sounding on CH%d)",
          inCh + 1, noteName(note), note, vel, voices[v].outCh + 1);
#endif
      return;
    }

    /* It was sounding because of the pedal: the same key is pressed again.
     * The SAME voice and the SAME channel are reused (nothing is reassigned). */
    voices[v].held      = true;
    voices[v].sustained = false;
    voices[v].vel       = vel;
    sendNoteOn(voices[v].outCh, note, vel);
    LOG("On(rep) in=%d %s(%d) v=%d -> CH%d (sustained voice recovered)",
        inCh + 1, noteName(note), note, vel, voices[v].outCh + 1);
    return;
  }

  /* --- 10.2 New voice: we look for the lowest free channel ---------------- */
  int8_t slot = allocSlot(note);

  if (slot < 0) {
#if OVERFLOW_MODE == OVERFLOW_STEAL_OLDEST
    slot = stealOldestSlot(note);
    if (slot < 0) {
      LOG("Overflow: %s(%d) v=%d dropped (no free channel)",
          noteName(note), note, vel);
      return;
    }
#else
    LOG("Overflow: %s(%d) v=%d dropped (the %d busy voices)",
        noteName(note), note, vel, VOICE_COUNT);
    return;
#endif
  }

  voices[slot].active    = true;
  voices[slot].held      = true;
  voices[slot].sustained = false;
  voices[slot].note      = note;
  voices[slot].vel       = vel;
  voices[slot].inCh      = inCh;
  voices[slot].outCh     = CH_NIBBLE(VOICE_CH_NUM(slot));
  voices[slot].order     = ++voiceOrderCounter;

  sendNoteOn(voices[slot].outCh, note, vel);
  LOG("NoteOn in=%d %s(%d) v=%d -> CH%d",
      inCh + 1, noteName(note), note, vel, VOICE_CH_NUM(slot));
}


/* ===========================================================================
 * 11. NOTE OFF  (voice release)
 * ---------------------------------------------------------------------------
 *  The Note Off ALWAYS goes out on the SAME channel the Note On went out on,
 *  because the channel is stored in the voice itself.
 * ======================================================================== */

void voiceNoteOff(uint8_t inCh, uint8_t note, uint8_t relVel) {

  /* If the note is still in the chord window, it is resolved first */
  if (pendingCount > 0) flushPending();

  int8_t v = findVoiceIndex(inCh, note);
  if (v < 0) {
    LOG("NoteOff in=%d %s(%d) -> no active voice (ignored)",
        inCh + 1, noteName(note), note);
    return;
  }

  if (sustainDown) {
    /* The pedal is down: the note keeps sounding and KEEPS its channel. */
    voices[v].held      = false;
    voices[v].sustained = true;
    LOG("NoteOff in=%d %s(%d) -> CH%d keeps sounding (pedal)",
        inCh + 1, noteName(note), note, voices[v].outCh + 1);
    return;
  }

  uint8_t ch = voices[v].outCh;
  sendNoteOff(ch, note, relVel);
  LOG("NoteOff in=%d %s(%d) v=%d -> CH%d (voice released)",
      inCh + 1, noteName(note), note, relVel, ch + 1);
  freeVoice((uint8_t)v);
}


/* ===========================================================================
 * 12. SUSTAIN (CC64)
 * ---------------------------------------------------------------------------
 *  Pedal down : the physical Note Off does not cut anything; the voice becomes
 *               "sustained" and keeps its channel.  Pressing the key again
 *               reuses the same voice/channel.
 *  Pedal up   : the pending Note Offs are sent (each one on ITS channel) and
 *               the channels are released.
 * ======================================================================== */

void releaseSustainedVoices() {
  uint8_t liberadas = 0;
  for (uint8_t i = 0; i < VOICE_COUNT; i++) {
    if (voices[i].active && !voices[i].held) {
      sendNoteOff(voices[i].outCh, voices[i].note, 0);
      LOG("Sustain OFF: NoteOff %s(%d) -> CH%d (voice released)",
          noteName(voices[i].note), voices[i].note, VOICE_CH_NUM(i));
      freeVoice(i);
      liberadas++;
    }
  }
  if (liberadas == 0) LOG("Sustain OFF: there were no pending notes");
}

void setSustain(uint8_t value) {
  bool down = (value >= 64);

  if (down) {
    if (!sustainDown) {
      sustainDown = true;
      LOG("Sustain ON  (CC64=%d)", value);
    }
    return;
  }

  if (!sustainDown) return;
  sustainDown = false;
  LOG("Sustain OFF (CC64=%d)", value);
  releaseSustainedVoices();
}

/* CC120 (all sound off) / CC123 (all notes off) / CC121 (reset controllers) */
void panicAllVoices() {
  for (uint8_t i = 0; i < VOICE_COUNT; i++) {
    if (voices[i].active) {
      sendNoteOff(voices[i].outCh, voices[i].note, 0);
      LOG("Panic: NoteOff %s(%d) -> CH%d", noteName(voices[i].note),
          voices[i].note, VOICE_CH_NUM(i));
      freeVoice(i);
    }
  }
  pendingCount = 0;
  voiceOrderCounter = 0;
}

void resetControllers() {
  sustainDown = false;
  releaseSustainedVoices();
}


/* ===========================================================================
 * 13. OTHER MIDI MESSAGES
 * ---------------------------------------------------------------------------
 *  Channel policy (this is what you asked about):
 *   - Pitch Bend, channel Aftertouch, CC (except 64) and Program Change:
 *     they are sent to the FOUR output channels.  It is the only way an effect
 *     reliably reaches the note that needs it, regardless of which one is
 *     sounding at each moment.  It breaks no voice allocation.
 *   - POLYPHONIC Aftertouch: it carries a note number.  The voice of that
 *     note is looked up and it is sent ONLY on its channel.  If the note is
 *     not sounding, it is discarded (another channel would affect another note).
 *   - CC64 (sustain): it is handled here and NOT forwarded (otherwise the
 *     synthesizer would lengthen the notes on its own and it would mismatch).
 *   - System Common / SysEx / real time: they pass through untouched.
 * ======================================================================== */

void polyAftertouch(uint8_t inCh, uint8_t note, uint8_t value) {
  int8_t v = findVoiceIndex(inCh, note);
  if (v < 0) {
    LOG("PolyAT in=%d %s(%d) -> no voice (discarded)", inCh + 1,
        noteName(note), note);
    return;
  }
  MIDI_PORT.write((uint8_t)(0xA0 | voices[v].outCh));
  MIDI_PORT.write(note);
  MIDI_PORT.write(value);
}

void controlChange(uint8_t inCh, uint8_t cc, uint8_t value) {
  (void)inCh;   /* the input channel does not matter: the CCs apply to the 4 outputs */

  if (cc == SUSTAIN_CC) {
    setSustain(value);
#if FORWARD_CC64
    broadcastMessage(0xB0, cc, value, 2);
#endif
    return;
  }

  if (cc == 120 || cc == 123) {      /* all sound off / all notes off */
    panicAllVoices();
    broadcastMessage(0xB0, cc, value, 2);
    return;
  }

  if (cc == 121) {                   /* reset all controllers */
    resetControllers();
    broadcastMessage(0xB0, cc, value, 2);
    return;
  }

  broadcastMessage(0xB0, cc, value, 2);
}


/* ===========================================================================
 * 14. MONITOR (mode 3)
 * ======================================================================== */

#if SPLITTER_MODE == MODE_MONITOR

void monitorPrintMessage(uint8_t status, const uint8_t *d, uint8_t len) {
  uint8_t type = status & 0xF0;
  uint8_t ch   = (uint8_t)((status & 0x0F) + 1);

  switch (type) {
    case 0x80:
      Serial.print(F("NoteOff ch")); Serial.print(ch);
      Serial.print(F("  ")); Serial.print(noteName(d[0]));
      Serial.print(F("  (")); Serial.print(d[0]); Serial.print(F(")"));
      Serial.print(F("  velOff=")); Serial.println(d[1]);
      break;

    case 0x90:
      if (d[1] == 0) {
        Serial.print(F("NoteOff ch")); Serial.print(ch);
        Serial.print(F("  ")); Serial.print(noteName(d[0]));
        Serial.print(F("  (vel0)"));
        Serial.println();
      } else {
        Serial.print(F("NoteOn  ch")); Serial.print(ch);
        Serial.print(F("  ")); Serial.print(noteName(d[0]));
        Serial.print(F("  (")); Serial.print(d[0]); Serial.print(F(")"));
        Serial.print(F("  vel=")); Serial.println(d[1]);
      }
      break;

    case 0xA0:
      Serial.print(F("PolyAT  ch")); Serial.print(ch);
      Serial.print(F("  ")); Serial.print(noteName(d[0]));
      Serial.print(F("  val=")); Serial.println(d[1]);
      break;

    case 0xB0:
      Serial.print(F("CC      ch")); Serial.print(ch);
      Serial.print(F("  #")); Serial.print(d[0]);
      Serial.print(F("  val=")); Serial.println(d[1]);
      break;

    case 0xC0:
      Serial.print(F("ProgCh  ch")); Serial.print(ch);
      Serial.print(F("  prog=")); Serial.println(d[0]);
      break;

    case 0xD0:
      Serial.print(F("ChanAT  ch")); Serial.print(ch);
      Serial.print(F("  val=")); Serial.println(d[0]);
      break;

    case 0xE0:
      Serial.print(F("PitchB  ch")); Serial.print(ch);
      Serial.print(F("  val=")); Serial.println(((int)d[1] << 7) | d[0]);
      break;

    default:
      Serial.print(F("Msg     ")); Serial.println(status, HEX);
      break;
  }
  (void)len;
}

#if MONITOR_SHOW_REALTIME
void monitorPrintRealtime(uint8_t b) {
  Serial.print(F("RT      0x")); Serial.println(b, HEX);
}
#endif

#endif /* MODE_MONITOR */


/* ===========================================================================
 * 15. MESSAGE DISPATCH
 * ======================================================================== */

bool channelOk(uint8_t inCh) {
#if INPUT_CHANNEL > 0
  return (inCh == (uint8_t)(INPUT_CHANNEL - 1));
#else
  (void)inCh;
  return true;
#endif
}

void handleMessage(uint8_t status, const uint8_t *d, uint8_t len) {

#if SPLITTER_MODE == MODE_THRU

  writeMessage(status, d, len);            /* pure MIDI THRU */

#elif SPLITTER_MODE == MODE_MONITOR

  monitorPrintMessage(status, d, len);
  #if MONITOR_THRU
    writeMessage(status, d, len);
  #endif

#else  /* MODE_VOICE_SPLIT or MODE_SPLIT_ZONES */

  uint8_t type = status & 0xF0;
  uint8_t inCh = status & 0x0F;
  (void)len;    /* in the splitter the length is fixed by the status */

  switch (type) {
    case 0x80:                             /* Note Off */
      if (channelOk(inCh)) voiceNoteOff(inCh, d[0], d[1]);
      break;

    case 0x90:                             /* Note On (vel 0 = Note Off) */
      if (!channelOk(inCh)) break;
      if (d[1] == 0) voiceNoteOff(inCh, d[0], 0);
      else           queueNoteOn(inCh, d[0], d[1]);
      break;

    case 0xA0:                             /* Polyphonic aftertouch */
      polyAftertouch(inCh, d[0], d[1]);
      break;

    case 0xB0:                             /* Control Change */
      controlChange(inCh, d[0], d[1]);
      break;

    case 0xC0:                             /* Program Change */
      broadcastMessage(0xC0, d[0], 0, 1);
      break;

    case 0xD0:                             /* Channel Pressure */
      broadcastMessage(0xD0, d[0], 0, 1);
      break;

    case 0xE0:                             /* Pitch Bend */
      broadcastMessage(0xE0, d[0], d[1], 2);
      break;

    default:
      break;
  }
#endif
}


/* ===========================================================================
 * 16. CHORD WINDOW (optional)
 * ---------------------------------------------------------------------------
 *  With CHORD_WINDOW_MS == 0 this is a direct pass to allocateAndStart().
 * ======================================================================== */

void queueNoteOn(uint8_t inCh, uint8_t note, uint8_t vel) {
#if CHORD_WINDOW_MS > 0
  if (pendingCount == 0) windowStartMs = millis();

  if (pendingCount >= PENDING_MAX) flushPending();   /* the buffer fills up */

  if (pendingCount >= PENDING_MAX) {                 /* just in case */
    allocateAndStart(inCh, note, vel);
    return;
  }
  pending[pendingCount].inCh = inCh;
  pending[pendingCount].note = note;
  pending[pendingCount].vel  = vel;
  pendingCount++;
#else
  allocateAndStart(inCh, note, vel);
#endif
}

void flushPending() {
  if (pendingCount == 0) return;

#if CHORD_WINDOW_MS > 0
  /* Stable order by pitch: the lowest one first -> lowest free channel */
  for (uint8_t i = 1; i < pendingCount; i++) {
    PendingNote key = pending[i];
    int8_t j = (int8_t)i - 1;
    while (j >= 0 && pending[j].note > key.note) {
      pending[j + 1] = pending[j];
      j--;
    }
    pending[j + 1] = key;
  }
#endif

  for (uint8_t i = 0; i < pendingCount; i++) {
    allocateAndStart(pending[i].inCh, pending[i].note, pending[i].vel);
  }
  pendingCount = 0;
}


/* ===========================================================================
 * 17. SETUP / LOOP
 * ======================================================================== */

void serialInitAndBanner() {
#if USB_SERIAL_NEEDED
  Serial.begin(DEBUG_BAUD);
#endif

#if (DEBUG || (SPLITTER_MODE == MODE_MONITOR)) && USB_SERIAL_NEEDED
  Serial.println();
  Serial.println(F("=== MIDI Voice Splitter / Router -- Arduino Mega 2560 ==="));
#if SPLITTER_MODE == MODE_VOICE_SPLIT
  Serial.println(F("Mode      : VOICE SPLITTER"));
#elif SPLITTER_MODE == MODE_THRU
  Serial.println(F("Mode      : MIDI THRU"));
#elif SPLITTER_MODE == MODE_MONITOR
  Serial.println(F("Mode      : MONITOR MIDI"));
#elif SPLITTER_MODE == MODE_SPLIT_ZONES
  Serial.println(F("Mode      : SPLIT INTO 2 ZONES"));
#endif
  Serial.print(F("Channels  : CH"));
  Serial.print(OUTPUT_CH_FIRST);
  Serial.print(F("..CH"));
  Serial.print(OUTPUT_CH_LAST);
  Serial.print(F("   (polyphony "));
  Serial.print(VOICE_COUNT);
  Serial.println(F(")"));
  Serial.print(F("MIDI I/O  : "));
#if MIDI_IO_MODE == MIDI_IO_SERIAL0
  Serial.println(F("D0 (RX) / D1 (TX) at 31250"));
#elif MIDI_IO_MODE == MIDI_IO_SERIAL1
  Serial.println(F("D19 (RX1) / D18 (TX1) at 31250"));
#elif MIDI_IO_MODE == MIDI_IO_SERIAL2
  Serial.println(F("D17 (RX2) / D16 (TX2) at 31250"));
#elif MIDI_IO_MODE == MIDI_IO_SERIAL3
  Serial.println(F("D15 (RX3) / D14 (TX3) at 31250"));
#else
  Serial.println(F("SoftwareSerial at 31250"));
#endif
  Serial.print(F("DEBUG     : "));
  Serial.print(DEBUG);
#if DEBUG_WRAPPED
  Serial.println(F("  (wrapped in SysEx 0x7D: the synthesizer ignores it)"));
#else
  Serial.println();
#endif
  Serial.println(F("Ready."));
  Serial.println();
#elif (DEBUG || (SPLITTER_MODE == MODE_MONITOR)) && !USB_SERIAL_NEEDED
  /* The MIDI takes up D0/D1: the serial port already runs at 31250 and serves
   * the log.  No header is printed here to avoid dirtying the MIDI capture. */
#endif
}

void setup() {
  MIDI_PORT.begin(MIDI_BAUD);

#if MIDI_IO_MODE == MIDI_IO_SOFTWARE
  midiSoftSerial.listen();
#endif

  /* Startup cleanup: whatever was left in the input buffer when the port was
   * initialized is discarded (line garbage). */
  while (MIDI_PORT.available() > 0) MIDI_PORT.read();

  for (uint8_t i = 0; i < VOICE_COUNT; i++) freeVoice(i);
  sustainDown       = false;
  pendingCount      = 0;
  voiceOrderCounter = 0;

  serialInitAndBanner();
}

void loop() {
  while (MIDI_PORT.available() > 0) {
    parseByte((uint8_t)MIDI_PORT.read());
  }

#if CHORD_WINDOW_MS > 0
  if (pendingCount > 0 && (millis() - windowStartMs) >= CHORD_WINDOW_MS) {
    flushPending();
  }
#endif
}
