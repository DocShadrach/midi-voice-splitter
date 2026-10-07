/* ============================================================================
 *  MIDI VOICE SPLITTER / ROUTER  --  ARDUINO MEGA 2560
 * ============================================================================
 *
 *  QUE HACE
 *  --------
 *  Entra por MIDI IN en el CANAL 11 y sale por MIDI OUT repartiendo las notas
 *  entre los canales 13, 14, 15 y 16 (4 voces como maximo).
 *  Mantiene la asignacion FIJA de cada nota hasta que esa nota deja de sonar
 *  de verdad (Note Off real + sustain resuelto).  Nunca se reorganizan las
 *  voces que ya estan sonando: NO hay "voice rebalancing".
 *
 *  MODOS (constante SPLITTER_MODE, seccion 1.1)
 *  --------------------------------------------
 *    MODE_VOICE_SPLIT : voice splitter (funcion principal)
 *    MODE_THRU        : MIDI THRU puro (IN -> OUT byte a byte)
 *    MODE_MONITOR     : monitor/analizador MIDI por el puerto serie  <-- modo extra
 *    MODE_SPLIT_ZONES : split de teclado en 2 zonas (extra, 2 voces por mano)
 *
 *  AJUSTES DE PRODUCCION (ya puestos por defecto)
 *  ----------------------------------------------
 *    DEBUG 0                -> no viaja ningun texto por el cable MIDI
 *    FORWARD_SYSTEM_RESET 0 -> no se reenvia el 0xFF espurio (System Reset)
 *    Limpieza del buffer de entrada al arrancar
 *
 *  MONTAJE
 *  -------
 *    Teclado MIDI (canal 11) --> MIDI IN del shield --> Arduino Mega 2560
 *    Arduino Mega 2560 --> MIDI OUT del shield --> aparato de destino
 *
 *    Todas las notas que entran por el CANAL 11 salen repartidas entre los
 *    canales 13, 14, 15 y 16.  Como maximo 4 notas simultaneas (4 voces),
 *    una por canal, y la 5ª nota que llegue no suena.  El destino tiene que
 *    escuchar en esos 4 canales.
 *
 *    >>> Conecta SOLO el conector OUT del shield al destino.  El conector
 *        THRU lleva la nota original en canal 11 y duplicaria las voces.
 *
 *  HARDWARE DE ESTE MONTAJE (identificado en las fotos del shield)
 *  --------------------------------------------------------------
 *    MIDI IN  -> Arduino D0  (RX0)   (pasa por el interruptor ON/OFF "RX(S2)")
 *    MIDI OUT -> Arduino D1  (TX0)
 *    MIDI THRU-> copia por hardware de IN (no la genera este sketch)
 *    Optoacoplador 6N138 + resistencias 220R, boton RESET(S1)
 *
 *    >>> CONSECUENCIA IMPORTANTE (leer la seccion 1.2 y el README):
 *        al compartir puerto D0/D1 con el USB, cualquier texto de DEBUG
 *        saldria tambien por el conector MIDI OUT.  Por eso, cuando el MIDI
 *        esta en D0/D1, el debug se envia ENVUELTO EN SysEx (ID 0x7D), que
 *        los sintetizadores deben ignorar.  En el Monitor Serie se lee igual
 *        (a 31250 baudios), solo se ven 3 caracteres raros por linea.
 *
 *  COMO CARGARLO
 *  -------------
 *   1) Pon el interruptor del shield en OFF (desconecta RX del circuito MIDI).
 *   2) Arduino IDE: placa "Arduino Mega or Mega 2560", procesador "ATmega2560".
 *   3) Sube el sketch, y vuelve a poner el interruptor en ON para tocar.
 *   4) Monitor Serie: 31250 baudios si el MIDI esta en D0/D1 (o 115200 si el
 *      MIDI estuviera en Serial1/2/3).
 *
 *  Autor: Doc Shadrach  (https://github.com/DocShadrach)
 *  Licencia: MIT.  Comentarios en espanol.
 * ==========================================================================*/


/* ===========================================================================
 * 1. CONFIGURACION
 * ======================================================================== */

/* ---- 1.1 Modo de trabajo --------------------------------------------------
 * Elige UNA de estas cuatro:
 *   MODE_VOICE_SPLIT  -> reparte notas entre los canales de salida
 *   MODE_THRU         -> MIDI THRU puro
 *   MODE_MONITOR      -> monitor MIDI por USB (no envia notas)
 *   MODE_SPLIT_ZONES  -> nota < SPLIT_POINT_NOTE  -> canales bajos
 *                        nota >= SPLIT_POINT_NOTE -> canales altos
 * ------------------------------------------------------------------------ */
#define MODE_VOICE_SPLIT  1
#define MODE_THRU         2
#define MODE_MONITOR      3
#define MODE_SPLIT_ZONES  4

#define SPLITTER_MODE   MODE_VOICE_SPLIT

/* ---- 1.2 Debug -----------------------------------------------------------
 * DEBUG 0 = SIN logs.  Es el ajuste de produccion: nada de texto viaja por el
 *           cable MIDI.  Ponlo a 1 solo para diagnosticar.
 * DEBUG 1 = logs por el puerto serie.
 *
 * DEBUG_AUTO_SAFE 1 = si el debug comparte cable con el MIDI (caso de D0/D1,
 *   que es este shield), el texto se envia dentro de mensajes SysEx con el
 *   ID de fabricante 0x7D ("uso no comercial"), de modo que el sintetizador
 *   los ignora y el flujo MIDI sigue siendo legal.  El texto se lee igual en
 *   el Monitor Serie (aparecen 3 caracteres no imprimibles por linea).
 *   Si lo pones a 0, el texto sale CRUDO por el conector MIDI OUT: eso puede
 *   ensuciar el stream del sintetizador.  Solo para diagnosticos puntuales.
 * ------------------------------------------------------------------------ */
#define DEBUG 0
#define DEBUG_AUTO_SAFE 1
#define DEBUG_BAUD 115200          /* baudios cuando el USB esta libre */

/* ---- 1.3 Canales de salida / polifonia -----------------------------------
 * Canales MIDI 13..16  ->  4 voces, tal como pediste.
 * La polifonia se deduce sola: VOICE_COUNT = LAST - FIRST + 1.
 * ------------------------------------------------------------------------ */
#define OUTPUT_CH_FIRST  13
#define OUTPUT_CH_LAST   16

/* ---- 1.4 Ventana de acorde -----------------------------------------------
 * 0  = asignacion INMEDIATA en orden de llegada (0 ms de latencia).
 *      La regla "nota mas grave -> canal libre mas bajo" se cumple para el
 *      orden en que llegan las notas (los teclados suelen mandar los acordes
 *      de grave a agudo, con lo que el resultado es el que buscas).
 * 15 = agrupa las notas que llegan en 15 ms y las asigna ordenadas por
 *      altura, con 15 ms de latencia.  Util si tu teclado manda los acordes
 *      de agudo a grave y quieres el orden por pitch garantizado.
 * ------------------------------------------------------------------------ */
#define CHORD_WINDOW_MS 0

/* ---- 1.5 Notas duplicadas ------------------------------------------------
 * MIDI permite recibir la misma nota dos veces antes del Note Off.
 *   DUP_NOTE_IGNORE    : la segunda Note On se ignora (una sola instancia).
 *   DUP_NOTE_RETRIGGER : se reenvia la Note On por el MISMO canal (misma voz,
 *                        una sola instancia, un solo Note Off).
 * La tabla de voces guarda como maximo UNA instancia por (canal de entrada,
 * nota), asi que es IMPOSIBLE dejar una nota colgada por duplicados.
 * ------------------------------------------------------------------------ */
#define DUP_NOTE_IGNORE    0
#define DUP_NOTE_RETRIGGER 1
#define DUP_NOTE_MODE      DUP_NOTE_IGNORE

/* ---- 1.6 Desbordamiento de polifonia -------------------------------------
 *   OVERFLOW_DROP_NEWEST  : la nota que no cabe NO suena (y su Note Off se
 *                           ignora). Seguro, sin robar voces.  POR DEFECTO.
 *   OVERFLOW_STEAL_OLDEST : roba la voz mas antigua (le manda su Note Off y
 *                           reutiliza su canal).  No es agresivo, pero corta.
 * ------------------------------------------------------------------------ */
#define OVERFLOW_DROP_NEWEST  0
#define OVERFLOW_STEAL_OLDEST 1
#define OVERFLOW_MODE         OVERFLOW_DROP_NEWEST

/* ---- 1.7 Sustain y varios ------------------------------------------------ */
#define SUSTAIN_CC            64   /* CC64 = pedal de sustain */
#define FORWARD_CC64          0    /* 1 = ademas de gestionarlo, reenvia CC64 */
#define FORWARD_SYSTEM_RESET  0    /* 0 = NO reenvia 0xFF (System Reset).
                                    * En este montaje, al abrir el puerto serie
                                    * del PC se cuela un 0xFF espurio por el
                                    * MIDI IN; reenviarlo resetearia el aparato
                                    * de destino.  Ponlo a 1 si de verdad
                                    * necesitas el System Reset. */
#define NOTE_OFF_STYLE        0    /* 0 = 0x80 con velocity de release
                                    * 1 = 0x90 con velocity 0 */
#define MONITOR_THRU          1    /* en MODE_MONITOR, 1 = tambien hace THRU */
#define MONITOR_SHOW_REALTIME 0    /* 1 = imprime clock/active sensing (mucha
                                    * salida; por defecto se silencian) */
#define SPLIT_POINT_NOTE      60   /* solo para MODE_SPLIT_ZONES (60 = C4) */

/* ---- 1.7b Canal de entrada del teclado -----------------------------------
 *   Tu teclado manda las NOTAS por el canal 11, asi que solo esas notas se
 *   aceptan: una nota suelta de otro canal no puede ocupar una voz.
 *   El resto de mensajes (CC, pedal, pitch bend, aftertouch, program change)
 *   SI se aceptan desde cualquier canal, porque los teclados a veces mandan
 *   las ruedas por otro canal distinto al de las notas; asi no se pierden.
 *   0 = aceptar tambien las notas de TODOS los canales.
 * ------------------------------------------------------------------------ */
#define INPUT_CHANNEL         11

/* ---- 1.8 Puerto MIDI: ELIGE EL TUYO --------------------------------------
 *   MIDI_IO_SERIAL0  -> D0 (RX) / D1 (TX)   <-- ESTE SHIELD (clon SparkFun)
 *   MIDI_IO_SERIAL1  -> D19 (RX1) / D18 (TX1)   (ideal en Mega: USB libre)
 *   MIDI_IO_SERIAL2  -> D17 (RX2) / D16 (TX2)
 *   MIDI_IO_SERIAL3  -> D15 (RX3) / D14 (TX3)
 *   MIDI_IO_SOFTWARE -> SoftwareSerial en MIDI_SOFT_RX_PIN / MIDI_SOFT_TX_PIN
 *
 * OJO con SoftwareSerial en el MEGA: el pin de RX TIENE que soportar
 * interrupciones por cambio de pin (PCINT).  En el Mega solo valen:
 * 10, 11, 12, 13, 50, 51, 52, 53 y A8..A15.  Los pines 2 y 3 NO valen
 * (eso es cosa del Uno).  El pin de TX puede ser cualquiera.
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
 * 2. PUERTOS E INCLUDES
 * ======================================================================== */

#include <stdio.h>          /* snprintf para los logs */
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
  #error "MIDI_IO_MODE no es valido: revisa la seccion 1.8"
#endif

/* El MIDI comparte UART con el USB: el texto de debug iria tambien al
 * conector MIDI OUT.  En ese caso se envia envuelto en SysEx. */
#if DEBUG && DEBUG_AUTO_SAFE && (MIDI_IO_MODE == MIDI_IO_SERIAL0)
  #define DEBUG_WRAPPED 1
#else
  #define DEBUG_WRAPPED 0
#endif

/* Si el MIDI NO usa D0/D1, el USB queda libre y se usa a DEBUG_BAUD. */
#if (DEBUG || (SPLITTER_MODE == MODE_MONITOR)) && (MIDI_IO_MODE != MIDI_IO_SERIAL0)
  #define USB_SERIAL_NEEDED 1
#else
  #define USB_SERIAL_NEEDED 0
#endif


/* ===========================================================================
 * 3. CONSTANTES, TIPOS Y TABLA DE VOCES
 * ======================================================================== */

#define VOICE_COUNT     (OUTPUT_CH_LAST - OUTPUT_CH_FIRST + 1)
#define PENDING_MAX     8
#define CH_MAX          16

/* Numero de canal MIDI "de cara al usuario" (1..16) -> nibble (0..15) */
#define CH_NIBBLE(ch)   ((uint8_t)((ch) - 1))

/* Una voz = un canal de salida ocupado por una nota.
 * Es la estructura que pediste, con un par de campos extra:
 *   held      -> la tecla esta fisicamente pulsada
 *   sustained -> la tecla ya se solto, pero el pedal la mantiene sonando
 * Una voz esta "sonando" mientras active == true.  El canal solo se libera
 * cuando active pasa a false. */
struct Voice {
  bool     active;      /* hay un Note On enviado y aun no su Note Off */
  bool     held;        /* tecla pulsada ahora mismo */
  bool     sustained;   /* soltada con pedal abajo (suena todavia) */
  uint8_t  note;        /* nota MIDI 0..127 */
  uint8_t  vel;         /* velocity de la Note On */
  uint8_t  inCh;        /* canal MIDI de entrada (nibble 0..15) */
  uint8_t  outCh;       /* canal MIDI de salida  (nibble 0..15) */
  uint32_t order;       /* orden de asignacion (para robo de voz y debug) */
};

Voice    voices[VOICE_COUNT];
uint32_t voiceOrderCounter = 0;
bool     sustainDown = false;

/* Cola para la ventana de acorde (solo se usa si CHORD_WINDOW_MS > 0) */
struct PendingNote { uint8_t inCh; uint8_t note; uint8_t vel; };
PendingNote pending[PENDING_MAX];
uint8_t     pendingCount  = 0;
uint32_t    windowStartMs = 0;


/* ===========================================================================
 * 4. PROTOTIPOS
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
 * 5. UTILIDADES
 * ======================================================================== */

/* "C4", "F#3"...  (nota 60 = C4, convencion cientifica)
 * Usa DOS buffers alternos para poder imprimir dos nombres de nota en la
 * misma linea de log (con un solo buffer, el segundo taparia al primero). */
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

/* Numero de canal MIDI (1..16) de una voz */
#define VOICE_CH_NUM(i)  ((uint8_t)(OUTPUT_CH_FIRST + (i)))


/* ===========================================================================
 * 6. DEBUG
 * ======================================================================== */

#if DEBUG
  static char dbgBuf[112];

  static void dbgSend() {
#if DEBUG_WRAPPED
    /* SysEx con ID de fabricante 0x7D (uso no comercial): legal por MIDI y
     * los sintetizadores lo ignoran.  El texto queda legible en el monitor. */
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
 * 7. PARSER MIDI
 * ---------------------------------------------------------------------------
 *  - Soporta running status.
 *  - Los bytes en tiempo real (0xF8..0xFF) se reenvian al instante y NO
 *    alteran el estado del parser (importante: pueden colarse en medio de
 *    cualquier mensaje).
 *  - SysEx (0xF0..0xF7) y System Common (0xF1..0xF6) se reenvian sin tocar.
 *  - Un dato huerfano (sin status) se descarta; nunca se pierde un mensaje
 *    valido por culpa del parser.
 * ======================================================================== */

uint8_t rxStatus    = 0;      /* running status activo (0 = ninguno) */
uint8_t rxData[2]   = {0,0};
uint8_t rxCount     = 0;
uint8_t rxNeed      = 0;      /* bytes de datos que espera el status actual */
bool    rxSysEx     = false;
uint8_t rxSysCommon = 0;      /* status de System Common en curso */
uint8_t rxSysLeft   = 0;      /* datos que le quedan a ese System Common */

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

  /* --- Tiempo real: 0xF8..0xFF (clock, start, stop, active sensing) ------- */
  if (b >= 0xF8) { routeRealtime(b); return; }

  /* --- Byte de estado ---------------------------------------------------- */
  if (b & 0x80) {
    if (b == 0xF0) {                      /* inicio de SysEx */
      rxSysEx = true; rxStatus = 0; rxCount = 0; rxSysCommon = 0;
      routeRawByte(b);
      return;
    }
    if (b == 0xF7) {                      /* fin de SysEx (EOX) */
      rxSysEx = false; rxStatus = 0; rxCount = 0; rxSysCommon = 0;
      routeRawByte(b);
      return;
    }
    rxSysEx = false;                      /* cualquier otro status aborta SysEx */

    if (b >= 0xF0) {                      /* System Common 0xF1..0xF6 */
      rxSysCommon = b;
      rxSysLeft   = sysCommonLen(b);
      rxStatus    = 0;
      rxCount     = 0;
      routeRawByte(b);
      if (rxSysLeft == 0) rxSysCommon = 0;
      return;
    }

    rxStatus = b;                         /* status de canal (nuevo running) */
    rxCount  = 0;
    rxNeed   = expectedDataBytes(b);
    if (rxNeed == 0) rxStatus = 0;
    return;
  }

  /* --- Byte de datos ----------------------------------------------------- */
  if (rxSysEx)     { routeRawByte(b); return; }
  if (rxSysCommon) { routeRawByte(b); if (--rxSysLeft == 0) rxSysCommon = 0; return; }
  if (rxStatus == 0) return;              /* dato huerfano: se descarta */

  rxData[rxCount++] = b;
  if (rxCount >= rxNeed) {
    handleMessage(rxStatus, rxData, rxNeed);
    rxCount = 0;                          /* running status: el status se queda */
  }
}

/* Byte suelto (SysEx / System Common / tiempo real) */
void routeRawByte(uint8_t b) {
#if SPLITTER_MODE == MODE_MONITOR
  #if MONITOR_THRU
    MIDI_PORT.write(b);
  #endif
#else
  MIDI_PORT.write(b);
#endif
}

/* Salida de un byte de tiempo real (clock, start/stop/continue, active
 * sensing...).  0xFF (System Reset) se filtra por defecto: ver
 * FORWARD_SYSTEM_RESET en la seccion 1.7. */
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
 * 8. SALIDA MIDI
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

/* Reenvia un mensaje de canal a TODOS los canales de salida.
 * Se usa para Pitch Bend, Aftertouch de canal, CC (excepto 64), Program
 * Change...  Asi, sea cual sea el canal en el que este sonando una nota,
 * el efecto tambien le llega.  Es deterministico y no deja estados a medias. */
void broadcastMessage(uint8_t typeHi, uint8_t d1, uint8_t d2, uint8_t len) {
  for (uint8_t i = 0; i < VOICE_COUNT; i++) {
    MIDI_PORT.write((uint8_t)(typeHi | CH_NIBBLE(VOICE_CH_NUM(i))));
    MIDI_PORT.write(d1);
    if (len > 1) MIDI_PORT.write(d2);
  }
}


/* ===========================================================================
 * 9. TABLA DE VOCES Y ASIGNACION DE CANALES
 * ---------------------------------------------------------------------------
 *  Regla: al CREAR una voz se elige el canal LIBRE mas bajo del rango.
 *  Una vez creada, la asignacion no se toca jamas hasta su Note Off.
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

/* Rango de slots utilizables segun la zona (solo cambia en MODE_SPLIT_ZONES) */
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
    if (!voices[i].active) return (int8_t)i;   /* canal libre mas bajo */
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

  LOG("Overflow: robo la voz CH%d (nota %s) para %s",
      VOICE_CH_NUM(best), noteName(voices[best].note), noteName(note));
  sendNoteOff(voices[best].outCh, voices[best].note, 0);
  freeVoice((uint8_t)best);
  return best;
}


/* ===========================================================================
 * 10. NOTE ON  (creacion de voz)
 * ---------------------------------------------------------------------------
 *  Prioridades: no colgar notas, no reasignar voces ocupadas, no retrigger
 *  de notas que ya suenan, no reorganizar el acorde.
 * ======================================================================== */

void allocateAndStart(uint8_t inCh, uint8_t note, uint8_t vel) {

  /* --- 10.1 Ya existe una voz para esta nota en este canal de entrada? ---- */
  int8_t v = findVoiceIndex(inCh, note);

  if (v >= 0) {
    if (voices[v].held) {
      /* Nota duplicada: la tecla ya estaba pulsada. */
#if DUP_NOTE_MODE == DUP_NOTE_RETRIGGER
      voices[v].vel = vel;
      sendNoteOn(voices[v].outCh, note, vel);
      LOG("Dup    in=%d %s(%d) v=%d -> retrigger CH%d (misma voz)",
          inCh + 1, noteName(note), note, vel, voices[v].outCh + 1);
#else
      LOG("Dup    in=%d %s(%d) v=%d -> ignorada (ya suena en CH%d)",
          inCh + 1, noteName(note), note, vel, voices[v].outCh + 1);
#endif
      return;
    }

    /* Estaba sonando por el pedal: se vuelve a pulsar la misma tecla.
     * Se reutiliza la MISMA voz y el MISMO canal (no se reasigna nada). */
    voices[v].held      = true;
    voices[v].sustained = false;
    voices[v].vel       = vel;
    sendNoteOn(voices[v].outCh, note, vel);
    LOG("On(rep) in=%d %s(%d) v=%d -> CH%d (voz sostenida recuperada)",
        inCh + 1, noteName(note), note, vel, voices[v].outCh + 1);
    return;
  }

  /* --- 10.2 Voz nueva: buscamos canal libre mas bajo ---------------------- */
  int8_t slot = allocSlot(note);

  if (slot < 0) {
#if OVERFLOW_MODE == OVERFLOW_STEAL_OLDEST
    slot = stealOldestSlot(note);
    if (slot < 0) {
      LOG("Overflow: %s(%d) v=%d descartada (sin canal libre)",
          noteName(note), note, vel);
      return;
    }
#else
    LOG("Overflow: %s(%d) v=%d descartada (las %d voces ocupadas)",
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
 * 11. NOTE OFF  (liberacion de voz)
 * ---------------------------------------------------------------------------
 *  El Note Off SIEMPRE sale por el MISMO canal por el que salio el Note On,
 *  porque el canal esta guardado en la propia voz.
 * ======================================================================== */

void voiceNoteOff(uint8_t inCh, uint8_t note, uint8_t relVel) {

  /* Si la nota aun esta en la ventana de acorde, se resuelve primero */
  if (pendingCount > 0) flushPending();

  int8_t v = findVoiceIndex(inCh, note);
  if (v < 0) {
    LOG("NoteOff in=%d %s(%d) -> sin voz activa (se ignora)",
        inCh + 1, noteName(note), note);
    return;
  }

  if (sustainDown) {
    /* El pedal esta abajo: la nota sigue sonando y CONSERVA su canal. */
    voices[v].held      = false;
    voices[v].sustained = true;
    LOG("NoteOff in=%d %s(%d) -> CH%d sigue sonando (pedal)",
        inCh + 1, noteName(note), note, voices[v].outCh + 1);
    return;
  }

  uint8_t ch = voices[v].outCh;
  sendNoteOff(ch, note, relVel);
  LOG("NoteOff in=%d %s(%d) v=%d -> CH%d (voz liberada)",
      inCh + 1, noteName(note), note, relVel, ch + 1);
  freeVoice((uint8_t)v);
}


/* ===========================================================================
 * 12. SUSTAIN (CC64)
 * ---------------------------------------------------------------------------
 *  Pedal abajo : el Note Off fisico no corta nada; la voz queda "sustained"
 *                y mantiene su canal.  Volver a pulsar la tecla reutiliza la
 *                misma voz/canal.
 *  Pedal arriba: se envian los Note Off pendientes (cada uno por SU canal) y
 *                se liberan los canales.
 * ======================================================================== */

void releaseSustainedVoices() {
  uint8_t liberadas = 0;
  for (uint8_t i = 0; i < VOICE_COUNT; i++) {
    if (voices[i].active && !voices[i].held) {
      sendNoteOff(voices[i].outCh, voices[i].note, 0);
      LOG("Sustain OFF: NoteOff %s(%d) -> CH%d (voz liberada)",
          noteName(voices[i].note), voices[i].note, VOICE_CH_NUM(i));
      freeVoice(i);
      liberadas++;
    }
  }
  if (liberadas == 0) LOG("Sustain OFF: no habia notas pendientes");
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
 * 13. OTROS MENSAJES MIDI
 * ---------------------------------------------------------------------------
 *  Politica de canal (esto es lo que preguntaste):
 *   - Pitch Bend, Aftertouch de canal, CC (menos 64) y Program Change:
 *     se envian a los CUATRO canales de salida.  Es la unica forma de que un
 *     efecto llegue seguro a la nota que lo necesita, sin depender de cual
 *     este sonando en cada momento.  No rompe ninguna asignacion de voz.
 *   - Aftertouch POLIFONICO: lleva numero de nota.  Se busca la voz de esa
 *     nota y se envia SOLO por su canal.  Si la nota no esta sonando, se
 *     descarta (mandarlo a otro canal afectaria a otra nota).
 *   - CC64 (sustain): se gestiona aqui y NO se reenvia (si no, el
 *     sintetizador alargaria las notas por su cuenta y se descuadraria).
 *   - System Common / SysEx / tiempo real: pasan tal cual, sin tocar.
 * ======================================================================== */

void polyAftertouch(uint8_t inCh, uint8_t note, uint8_t value) {
  int8_t v = findVoiceIndex(inCh, note);
  if (v < 0) {
    LOG("PolyAT in=%d %s(%d) -> sin voz (descartado)", inCh + 1,
        noteName(note), note);
    return;
  }
  MIDI_PORT.write((uint8_t)(0xA0 | voices[v].outCh));
  MIDI_PORT.write(note);
  MIDI_PORT.write(value);
}

void controlChange(uint8_t inCh, uint8_t cc, uint8_t value) {
  (void)inCh;   /* el canal de entrada no influye: los CC se aplican a las 4 salidas */

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
 * 14. MONITOR (modo 3)
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
 * 15. REPARTO DE MENSAJES
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

  writeMessage(status, d, len);            /* MIDI THRU puro */

#elif SPLITTER_MODE == MODE_MONITOR

  monitorPrintMessage(status, d, len);
  #if MONITOR_THRU
    writeMessage(status, d, len);
  #endif

#else  /* MODE_VOICE_SPLIT o MODE_SPLIT_ZONES */

  uint8_t type = status & 0xF0;
  uint8_t inCh = status & 0x0F;
  (void)len;    /* en el splitter la longitud es fija segun el status */

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
 * 16. VENTANA DE ACORDE (opcional)
 * ---------------------------------------------------------------------------
 *  Con CHORD_WINDOW_MS == 0 esto es un paso directo a allocateAndStart().
 * ======================================================================== */

void queueNoteOn(uint8_t inCh, uint8_t note, uint8_t vel) {
#if CHORD_WINDOW_MS > 0
  if (pendingCount == 0) windowStartMs = millis();

  if (pendingCount >= PENDING_MAX) flushPending();   /* el buffer se llena */

  if (pendingCount >= PENDING_MAX) {                 /* por si acaso */
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
  /* Orden estable por altura: la mas grave primero -> canal libre mas bajo */
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
  Serial.println(F("Modo      : VOICE SPLITTER"));
#elif SPLITTER_MODE == MODE_THRU
  Serial.println(F("Modo      : MIDI THRU"));
#elif SPLITTER_MODE == MODE_MONITOR
  Serial.println(F("Modo      : MONITOR MIDI"));
#elif SPLITTER_MODE == MODE_SPLIT_ZONES
  Serial.println(F("Modo      : SPLIT EN 2 ZONAS"));
#endif
  Serial.print(F("Canales   : CH"));
  Serial.print(OUTPUT_CH_FIRST);
  Serial.print(F("..CH"));
  Serial.print(OUTPUT_CH_LAST);
  Serial.print(F("   (polifonia "));
  Serial.print(VOICE_COUNT);
  Serial.println(F(")"));
  Serial.print(F("MIDI I/O  : "));
#if MIDI_IO_MODE == MIDI_IO_SERIAL0
  Serial.println(F("D0 (RX) / D1 (TX) a 31250"));
#elif MIDI_IO_MODE == MIDI_IO_SERIAL1
  Serial.println(F("D19 (RX1) / D18 (TX1) a 31250"));
#elif MIDI_IO_MODE == MIDI_IO_SERIAL2
  Serial.println(F("D17 (RX2) / D16 (TX2) a 31250"));
#elif MIDI_IO_MODE == MIDI_IO_SERIAL3
  Serial.println(F("D15 (RX3) / D14 (TX3) a 31250"));
#else
  Serial.println(F("SoftwareSerial a 31250"));
#endif
  Serial.print(F("DEBUG     : "));
  Serial.print(DEBUG);
#if DEBUG_WRAPPED
  Serial.println(F("  (envuelto en SysEx 0x7D: el sintetizador lo ignora)"));
#else
  Serial.println();
#endif
  Serial.println(F("Listo."));
  Serial.println();
#elif (DEBUG || (SPLITTER_MODE == MODE_MONITOR)) && !USB_SERIAL_NEEDED
  /* El MIDI ocupa D0/D1: el puerto serie ya va a 31250 y sirve para el log.
   * No se imprime cabecera aqui para no ensuciar el propio analisis MIDI. */
#endif
}

void setup() {
  MIDI_PORT.begin(MIDI_BAUD);

#if MIDI_IO_MODE == MIDI_IO_SOFTWARE
  midiSoftSerial.listen();
#endif

  /* Limpieza de arranque: se descarta lo que haya quedado en el buffer de
   * entrada al inicializar el puerto (basura de la linea). */
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
