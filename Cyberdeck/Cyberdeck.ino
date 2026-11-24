// ===========================================================================
//  CYBERDECK firmware for ESP32-S3 handhelds
//    - LILYGO T-Deck / T-Deck Plus (keyboard, trackball, LoRa)
//    - LCDWiki 2.8" ES3C28P touch display (touchscreen, BOOT key, no radio)
//  Pick the board in config.h or with `make build BOARD=...`; pins are in boards.h.
//
//  Board:  ESP32S3 Dev Module, USB CDC On Boot: Enabled,
//          Flash Size: 16MB, PSRAM: OPI PSRAM, Partition: 16M Flash (3MB APP/9.9MB FATFS)
//  Library: RadioLib (Library Manager, T-Deck only)
//
//  Controls (T-Deck)
//    Trackball up/down ... move          Trackball click / Enter ... select
//    Trackball left ...... back          Backspace ................ back
//    COMMS: type on the keyboard, Enter sends over LoRa
//  Controls (touch boards)
//    Tap an item to open it, drag lists to scroll, tap the "<" title or swipe right to go back.
//    BOOT key also goes back.
// ===========================================================================
#include <Arduino.h>
#include <algorithm>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <Preferences.h>
#include "config.h"
#include "boards.h"
#if SD_OVER_SPI
#include <SD.h>
#define SDCARD SD
#else
#include <SD_MMC.h>
#define SDCARD SD_MMC
#endif
#if HAS_LORA
#include <RadioLib.h>
#endif
#include "gfx.h"
#include "ui.h"

// ---- types used in function signatures (must precede the first function) ---
enum Ev { EV_UP, EV_DOWN, EV_LEFT, EV_RIGHT, EV_CLICK, EV_KEY };
static void handle(Ev e, char key = 0);

static void openScreen(Screen next);

#if HAS_TOUCH
enum TouchKind : uint8_t { TE_TAP, TE_STEP, TE_BACK, TE_BUTTON, TE_CAL };
struct TouchEv { TouchKind kind; int16_t x, y, rawX, rawY; };   // TE_STEP: x = +1 (down) / -1 (up)
#endif

// Serial log for bring-up (115200, `make monitor`). Skipped when no USB host is listening.
#define LOG(...) do { if (Serial) Serial.printf(__VA_ARGS__); } while (0)

// ---- globals ---------------------------------------------------------------
static UiState S;
static uint16_t* fb = nullptr;
static uint8_t lineBuf[SCREEN_W * 2];
static bool dirty = true;
static uint32_t lastFrame = 0;
static uint32_t drawUs = 0, pushUs = 0;   // last frame timings, for the [stat] log
static uint32_t battRawMv = 0;            // ADC pin mV before the x2 divider correction
static int radioErr = 0;                  // RadioLib begin() result (0 on boards without a radio)
static Preferences prefs;

#if HAS_LORA
SX1262 radio = new Module(PIN_RADIO_CS, PIN_RADIO_DIO1, PIN_RADIO_RST, PIN_RADIO_BUSY, SPI);
static volatile bool radioFlag = false;
static void IRAM_ATTR onRadio() { radioFlag = true; }
#endif

// ===========================================================================
//  Display: 320x240 landscape over SPI (ST7789 on the T-Deck, ILI9341V on the ES3C28P)
// ===========================================================================
static SPISettings tftSPI(40000000, MSBFIRST, SPI_MODE0);

static void tftCmd(uint8_t cmd, const uint8_t* data = nullptr, size_t n = 0) {
  digitalWrite(PIN_TFT_DC, LOW);
  SPI.write(cmd);
  digitalWrite(PIN_TFT_DC, HIGH);
  if (n) SPI.writeBytes(data, n);
}

struct TftStep { uint8_t cmd, len; uint8_t data[15]; uint16_t waitMs; };

#if TFT_IS_ILI9341
// LCDWiki's ILI9341V_Init.txt for this panel, with MADCTL written once at the right place.
// The panel's RST is tied to the ESP32's EN, so a software reset covers soft reboots.
static const TftStep TFT_INIT[] = {
  {0x01, 0, {0}, 120},                                    // software reset
  {0xCF, 3, {0x00, 0xC1, 0x30}, 0},
  {0xED, 4, {0x64, 0x03, 0x12, 0x81}, 0},
  {0xE8, 3, {0x85, 0x00, 0x78}, 0},
  {0xCB, 5, {0x39, 0x2C, 0x00, 0x34, 0x02}, 0},
  {0xF7, 1, {0x20}, 0},
  {0xEA, 2, {0x00, 0x00}, 0},
  {0xC0, 1, {0x13}, 0},                                   // power control
  {0xC1, 1, {0x13}, 0},
  {0xC5, 2, {0x22, 0x35}, 0},                             // VCOM
  {0xC7, 1, {0xBD}, 0},
  {0x21, 0, {0}, 0},                                      // inversion on (IPS)
  {0x36, 1, {TFT_MADCTL}, 0},                             // orientation + BGR
  {0xB6, 2, {0x0A, 0xA2}, 0},
  {0x3A, 1, {0x55}, 0},                                   // RGB565
  {0xF6, 2, {0x01, 0x30}, 0},
  {0xB1, 2, {0x00, 0x1B}, 0},                             // frame rate
  {0xF2, 1, {0x00}, 0},
  {0x26, 1, {0x01}, 0},                                   // gamma curve
  {0xE0, 15, {0x0F, 0x35, 0x31, 0x0B, 0x0E, 0x06, 0x49, 0xA7, 0x33, 0x07, 0x0F, 0x03, 0x0C, 0x0A, 0x00}, 0},
  {0xE1, 15, {0x00, 0x0A, 0x0F, 0x04, 0x11, 0x08, 0x36, 0x58, 0x4D, 0x07, 0x10, 0x0C, 0x32, 0x34, 0x0F}, 0},
  {0x11, 0, {0}, 120},                                    // sleep out
  {0x29, 0, {0}, 20},                                     // display on
};
#else
// Init sequence from LilyGO's T-Deck examples (ST7789), landscape.
static const TftStep TFT_INIT[] = {
  {0x01, 0, {0}, 120},
  {0x11, 0, {0}, 120},
  {0x3A, 1, {0x55}, 0},
  {0x36, 1, {TFT_MADCTL}, 0},
  {0xB2, 5, {0x0C, 0x0C, 0x00, 0x33, 0x33}, 0},
  {0xB7, 1, {0x75}, 0},
  {0xBB, 1, {0x1A}, 0},
  {0xC0, 1, {0x2C}, 0},
  {0xC2, 1, {0x01}, 0},
  {0xC3, 1, {0x13}, 0},
  {0xC4, 1, {0x20}, 0},
  {0xC6, 1, {0x0F}, 0},
  {0xD0, 2, {0xA4, 0xA1}, 0},
  {0xD6, 1, {0xA1}, 0},
  {0xE0, 14, {0xD0, 0x0D, 0x14, 0x0D, 0x0D, 0x09, 0x38, 0x44, 0x4E, 0x3A, 0x17, 0x18, 0x2F, 0x30}, 0},
  {0xE1, 14, {0xD0, 0x09, 0x0F, 0x08, 0x07, 0x14, 0x37, 0x44, 0x4D, 0x38, 0x15, 0x16, 0x2C, 0x3E}, 0},
  {0x21, 0, {0}, 0},    // inversion on (this panel needs it)
  {0x29, 0, {0}, 20},   // display on
};
#endif

static void tftInit() {
  pinMode(PIN_TFT_DC, OUTPUT);
  SPI.beginTransaction(tftSPI);
  digitalWrite(PIN_TFT_CS, LOW);
  for (const TftStep& s : TFT_INIT) {
    tftCmd(s.cmd, s.data, s.len);
    if (s.waitMs) delay(s.waitMs);
  }
  digitalWrite(PIN_TFT_CS, HIGH);
  SPI.endTransaction();
}

#if TFT_IS_ILI9341
// Read one parameter byte of a read command (ILI9341 SPI uses the 0xD9 index trick, as Adafruit's driver does).
static uint8_t tftRead8(uint8_t cmd, uint8_t index = 0) {
  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_TFT_CS, LOW);
  uint8_t idx = 0x10 + index;
  tftCmd(0xD9, &idx, 1);
  tftCmd(cmd);
  uint8_t v = SPI.transfer(0);
  digitalWrite(PIN_TFT_CS, HIGH);
  SPI.endTransaction();
  return v;
}

// Proves the SPI wiring both ways: these come back from the panel itself.
static void logPanel() {
  LOG("[tft] ID4=%02X %02X %02X (want 00 93 41)  power=0x%02X (want 0x9C)  MADCTL=0x%02X (want 0x%02X)  pixfmt=0x%02X\n",
      tftRead8(0xD3, 1), tftRead8(0xD3, 2), tftRead8(0xD3, 3), tftRead8(0x0A), tftRead8(0x0B), TFT_MADCTL,
      tftRead8(0x0C));
}
#endif

static void pushFrame() {
  static const uint8_t cols[4] = {0, 0, (SCREEN_W - 1) >> 8, (SCREEN_W - 1) & 0xFF};
  static const uint8_t rows[4] = {0, 0, (SCREEN_H - 1) >> 8, (SCREEN_H - 1) & 0xFF};
  SPI.beginTransaction(tftSPI);
  digitalWrite(PIN_TFT_CS, LOW);
  tftCmd(0x2A, cols, 4);
  tftCmd(0x2B, rows, 4);
  tftCmd(0x2C);
  for (int y = 0; y < SCREEN_H; y++) {
    composeRow(fb, y, lineBuf, S.glow, S.scanlines);
    SPI.writeBytes(lineBuf, sizeof lineBuf);
  }
  digitalWrite(PIN_TFT_CS, HIGH);
  SPI.endTransaction();
}

// Backlight level 1..16 (16 = brightest).
static void setBacklight(uint8_t value) {
  const uint8_t steps = 16;
  if (value > steps) value = steps;
#if BACKLIGHT_PWM
  static bool attached = false;
  if (!attached) attached = ledcAttach(PIN_TFT_BL, 20000, 8);
  ledcWrite(PIN_TFT_BL, (value * value * 255 + 255) / 256);   // squared for an even-looking ramp
#else
  // T-Deck: the driver takes pulses, 16 levels (from LilyGO's example).
  static uint8_t level = 0;
  if (value == 0) { digitalWrite(PIN_TFT_BL, 0); delay(3); level = 0; return; }
  if (level == 0) { digitalWrite(PIN_TFT_BL, 1); level = steps; delayMicroseconds(30); }
  int from = steps - level, to = steps - value;
  int num = (steps + to - from) % steps;
  for (int i = 0; i < num; i++) { digitalWrite(PIN_TFT_BL, 0); digitalWrite(PIN_TFT_BL, 1); }
  level = value;
#endif
}

static void render() {
  S.ms = millis();
  Canvas g;
  g.fb = fb;
  uint32_t t0 = micros();
  drawScreen(g, S);
  uint32_t t1 = micros();
  pushFrame();
  drawUs = t1 - t0;
  pushUs = micros() - t1;
  lastFrame = millis();
  dirty = false;
}

// ===========================================================================
//  Battery
// ===========================================================================
static void readBattery() {
  uint32_t mv = 0;
  for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(PIN_BAT_ADC);
  battRawMv = mv / 8;
  float v = battRawMv * 2 / 1000.0f;   // 1:2 divider on both boards
  S.battV = S.battV == 0 ? v : S.battV * 0.8f + v * 0.2f;
  static const float curve[][2] = {{4.20f, 100}, {4.10f, 90}, {4.00f, 80}, {3.92f, 70}, {3.85f, 60}, {3.80f, 50},
                                   {3.75f, 40}, {3.70f, 30}, {3.65f, 20}, {3.55f, 10}, {3.40f, 5}, {3.30f, 0}};
  float b = S.battV, pct = 0;
  if (b >= curve[0][0]) pct = 100;
  else
    for (int i = 1; i < 12; i++)
      if (b >= curve[i][0]) {
        float t = (b - curve[i][0]) / (curve[i - 1][0] - curve[i][0]);
        pct = curve[i][1] + t * (curve[i - 1][1] - curve[i][1]);
        break;
      }
  S.battPct = (int)(pct + 0.5f);
}

// ===========================================================================
//  System info
// ===========================================================================
static void readSystem() {
  S.heapFree = ESP.getFreeHeap();
  S.heapTotal = ESP.getHeapSize();
  S.psramFree = ESP.getFreePsram();
  S.psramTotal = ESP.getPsramSize();
  S.uptimeS = millis() / 1000;
}

static void initSystemInfo() {
  uint16_t rev = ESP.getChipRevision();
  snprintf(S.chip, sizeof S.chip, "%s REV %u.%u", ESP.getChipModel(), rev / 100, rev % 100);
  S.cores = ESP.getChipCores();
  S.cpuMhz = ESP.getCpuFreqMHz();
  S.flashMB = ESP.getFlashChipSize() / (1024 * 1024);
  uint64_t mac = ESP.getEfuseMac();
  uint8_t m[6];
  for (int i = 0; i < 6; i++) m[i] = (mac >> (8 * i)) & 0xFF;
  snprintf(S.mac, sizeof S.mac, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  if (strlen(DECK_CALLSIGN)) snprintf(S.callsign, sizeof S.callsign, "%s", DECK_CALLSIGN);
  else snprintf(S.callsign, sizeof S.callsign, "DECK-%02X%02X", m[4], m[5]);
  readSystem();
}

// ===========================================================================
//  Wi-Fi scan
// ===========================================================================
static void startScan() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.scanNetworks(true, true);
  S.scanning = true;
}

static void stopWifi() {
  WiFi.scanDelete();
  WiFi.mode(WIFI_OFF);
  S.scanning = false;
}

static void pollScan() {
  if (!S.scanning) return;
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;
  S.scanning = false;
  dirty = true;
  if (n < 0) { S.netCount = 0; return; }
  static NetInfo tmp[64];
  int count = n < 64 ? n : 64;
  for (int i = 0; i < count; i++) {
    snprintf(tmp[i].ssid, sizeof tmp[i].ssid, "%s", WiFi.SSID(i).c_str());
    tmp[i].rssi = WiFi.RSSI(i);
    tmp[i].channel = WiFi.channel(i);
    tmp[i].secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  std::sort(tmp, tmp + count, [](const NetInfo& a, const NetInfo& b) { return a.rssi > b.rssi; });
  S.netCount = count < MAX_NETS ? count : MAX_NETS;
  memcpy(S.nets, tmp, sizeof(NetInfo) * S.netCount);
  if (S.netSel >= S.netCount) S.netSel = S.netCount ? S.netCount - 1 : 0;
  if (S.netScroll > S.netSel) S.netScroll = S.netSel;
  LOG("[scan] %d networks\n", n);
}

// ===========================================================================
//  LoRa chat (boards with an SX1262)
// ===========================================================================
static void pushLine(uint8_t kind, const char* text, int16_t rssi) {
  if (S.chatCount == MAX_CHAT) {
    memmove(S.chat, S.chat + 1, sizeof(ChatLine) * (MAX_CHAT - 1));
    S.chatCount--;
  }
  ChatLine& l = S.chat[S.chatCount++];
  snprintf(l.text, sizeof l.text, "%s", text);
  l.kind = kind;
  l.rssi = rssi;
}

// Word-wraps "FROM: message" into CHAT_COLS-wide lines.
static void addMessage(uint8_t kind, const char* from, const char* msg, int16_t rssi) {
  char full[200];
  if (from) snprintf(full, sizeof full, "%s: %s", from, msg);
  else snprintf(full, sizeof full, "%s", msg);
  for (char* c = full; *c; c++)
    if (*c < 32 || *c > 126) *c = '?';
  const char* p = full;
  bool first = true;
  while (*p) {
    int room = first ? CHAT_COLS : CHAT_COLS - 2;
    int len = (int)strlen(p), take = len;
    if (len > room) {
      take = room;
      int sp = room;
      while (sp > 0 && p[sp] != ' ') sp--;
      if (sp > room / 2) take = sp;
    }
    char line[CHAT_COLS + 1];
    snprintf(line, sizeof line, "%s%.*s", first ? "" : "  ", take, p);
    pushLine(kind, line, first ? rssi : 0);
    p += take;
    while (*p == ' ') p++;
    first = false;
  }
  dirty = true;
}

#if HAS_LORA
static void initRadio() {
  int st = radio.begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR, LORA_SYNC_WORD, LORA_TX_DBM, 10);
  radioErr = st;
  LOG("[radio] begin() = %d\n", st);
  if (st != RADIOLIB_ERR_NONE) {
    char e[40];
    snprintf(e, sizeof e, "-- RADIO INIT FAILED (%d) --", st);
    addMessage(0, nullptr, e, 0);
    return;
  }
  radio.setCurrentLimit(140);
  radio.setPacketReceivedAction(onRadio);
  radio.startReceive();
  S.radioOk = true;
  S.freqMhz = LORA_FREQ_MHZ;
  char m[40];
  snprintf(m, sizeof m, "-- RADIO ONLINE %.1f MHZ --", LORA_FREQ_MHZ);
  addMessage(0, nullptr, m, 0);
}

static void pollRadio() {
  if (!S.radioOk || !radioFlag) return;
  radioFlag = false;
  uint8_t buf[256];
  size_t len = radio.getPacketLength();
  if (len > sizeof buf - 1) len = sizeof buf - 1;
  int st = radio.readData(buf, len);
  if (st == RADIOLIB_ERR_NONE && len > 0) {
    buf[len] = 0;
    char* text = (char*)buf;
    char* bar = strchr(text, '|');
    const char* from = "???";
    if (bar && bar - text < 16) { *bar = 0; from = text; text = bar + 1; }
    addMessage(2, from, text, (int16_t)radio.getRSSI());
    S.lastRssi = (int)radio.getRSSI();
    if (S.screen != SCR_COMMS) S.unread++;
  }
  radio.startReceive();
}

static void sendMessage() {
  if (!S.input[0]) return;
  if (!S.radioOk) { addMessage(0, nullptr, "-- RADIO OFFLINE, NOT SENT --", 0); return; }
  char pkt[128];
  int n = snprintf(pkt, sizeof pkt, "%s|%s", S.callsign, S.input);
  if (n > (int)sizeof pkt - 1) n = sizeof pkt - 1;
  int st = radio.transmit((uint8_t*)pkt, n);
  radioFlag = false;          // transmit-done also fires DIO1
  radio.startReceive();
  if (st == RADIOLIB_ERR_NONE) addMessage(1, "YOU", S.input, 0);
  else {
    char e[40];
    snprintf(e, sizeof e, "-- SEND FAILED (%d) --", st);
    addMessage(0, nullptr, e, 0);
  }
  S.input[0] = 0;
}
#else
static void sendMessage() { S.input[0] = 0; }
#endif

// ===========================================================================
//  SD card (SPI on the T-Deck, 4-bit SDIO on the ES3C28P)
// ===========================================================================
static bool mountSD() {
  if (S.sdOk) return true;
#if SD_OVER_SPI
  SD.end();
  S.sdOk = SD.begin(PIN_SD_CS, SPI, 4000000) && SD.cardType() != CARD_NONE;
#else
  SD_MMC.end();
  SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3);
  S.sdOk = SD_MMC.begin("/sdcard", false) && SD_MMC.cardType() != CARD_NONE;
#endif
  if (S.sdOk) {
    S.sdTotalGB = SDCARD.totalBytes() / 1e9f;
    S.sdUsedGB = SDCARD.usedBytes() / 1e9f;
  }
  LOG("[sd] %s\n", S.sdOk ? "mounted" : "no card / mount failed");
  return S.sdOk;
}

static void loadDir() {
  S.fileCount = 0;
  S.fileSel = 0;
  S.fileScroll = 0;
  File dir = SDCARD.open(S.path);
  if (!dir || !dir.isDirectory()) {     // card pulled, or path vanished
    if (dir) dir.close();
    if (strcmp(S.path, "/") != 0) { strcpy(S.path, "/"); loadDir(); return; }
    S.sdOk = false;
    return;
  }
  for (File f = dir.openNextFile(); f && S.fileCount < MAX_FILES; f = dir.openNextFile()) {
    const char* nm = f.name();
    if (nm[0] != '.' && strcmp(nm, "System Volume Information") != 0) {
      FileEntry& e = S.files[S.fileCount++];
      snprintf(e.name, sizeof e.name, "%s", nm);
      e.dir = f.isDirectory();
      e.size = f.size();
    }
    f.close();
  }
  dir.close();
  std::sort(S.files, S.files + S.fileCount, [](const FileEntry& a, const FileEntry& b) {
    if (a.dir != b.dir) return a.dir;
    return strcasecmp(a.name, b.name) < 0;
  });
}

// ===========================================================================
//  Input: T-Deck trackball + click + I2C keyboard
// ===========================================================================
#if HAS_TRACKBALL
static volatile int16_t ballUp, ballDown, ballLeft, ballRight;
static void IRAM_ATTR isrUp()    { ballUp = ballUp + 1; }
static void IRAM_ATTR isrDown()  { ballDown = ballDown + 1; }
static void IRAM_ATTR isrLeft()  { ballLeft = ballLeft + 1; }
static void IRAM_ATTR isrRight() { ballRight = ballRight + 1; }

static void initTrackball() {
  const uint8_t pins[4] = {PIN_BALL_UP, PIN_BALL_DOWN, PIN_BALL_LEFT, PIN_BALL_RIGHT};
  void (*isrs[4])() = {isrUp, isrDown, isrLeft, isrRight};
  for (int i = 0; i < 4; i++) {
    pinMode(pins[i], INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(pins[i]), isrs[i], CHANGE);
  }
  pinMode(PIN_BUTTON, INPUT_PULLUP);
}

static void pollTrackball() {
  static int accX = 0, accY = 0;
  static uint32_t lastMove = 0;
  int u = ballUp, d = ballDown, l = ballLeft, r = ballRight;
  ballUp -= u; ballDown -= d; ballLeft -= l; ballRight -= r;
  uint32_t now = millis();
  if (u | d | l | r) lastMove = now;
  else if (now - lastMove > 400) accX = accY = 0;   // forget stray ticks

  accY += d - u;
  accX += r - l;
  while (accY <= -TRACKBALL_STEP) { accY += TRACKBALL_STEP; accX = 0; handle(EV_UP); }
  while (accY >= TRACKBALL_STEP)  { accY -= TRACKBALL_STEP; accX = 0; handle(EV_DOWN); }
  // horizontal needs a firmer push, since left means "back"
  if (accX <= -2 * TRACKBALL_STEP) { accX = 0; accY = 0; handle(EV_LEFT); }
  if (accX >= 2 * TRACKBALL_STEP)  { accX = 0; accY = 0; handle(EV_RIGHT); }

  static bool lastDown = false;
  static uint32_t changed = 0;
  bool down = digitalRead(PIN_BUTTON) == LOW;
  if (down != lastDown && now - changed > 30) {
    changed = now;
    lastDown = down;
    if (down) handle(BUTTON_EVENT);
  }
}
#endif

#if HAS_KEYBOARD
static bool probeKeyboard() {
  Wire.beginTransmission(KEYBOARD_ADDR);
  return Wire.endTransmission() == 0;
}

static void pollKeyboard() {
  static uint32_t last = 0;
  if (!S.inputOk || millis() - last < 20) return;
  last = millis();
  if (Wire.requestFrom(KEYBOARD_ADDR, 1) == 1) {
    char c = Wire.read();
    if (c) handle(EV_KEY, c);
  }
}
#endif

// ===========================================================================
//  Input: touchscreen (FT6336) + BOOT key, read by a task on core 0.
//  A full frame takes ~100 ms on the loop core, so polling there would drop quick taps.
//  The task turns raw touches into gestures and queues them; loop() acts on them.
// ===========================================================================
#if HAS_TOUCH
static QueueHandle_t touchQ = nullptr;
static const int TAP_SLOP = 10;      // px a finger may wander and still count as a tap
static const int DRAG_STEP = 15;     // px of vertical drag per list step (about one row)
static const int SWIPE_BACK = 60;    // px of rightward swipe that means "back"

static bool i2cReadReg(uint8_t addr, uint8_t reg, uint8_t* out, uint8_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(addr, n) != n) return false;
  for (int i = 0; i < n; i++) out[i] = Wire.read();
  return true;
}

// Raw panel point -> screen pixel, as an affine map: sx = a*rx + b*ry + c, sy = d*rx + e*ry + f.
// Starts from the boards.h swap/flip guess; touch calibration replaces it and stores it in prefs ("tcal").
static float tcal[6];

static void touchDefaultCal() {
  float a = 0, b = 0, c = 0, d = 0, e = 0, f = 0;
  if (TOUCH_SWAP_XY) { b = 1; d = 1; } else { a = 1; e = 1; }
  if (TOUCH_FLIP_X) { a = -a; b = -b; c = SCREEN_W - 1; }
  if (TOUCH_FLIP_Y) { d = -d; e = -e; f = SCREEN_H - 1; }
  const float t[6] = {a, b, c, d, e, f};
  memcpy(tcal, t, sizeof tcal);
}

// One touch point in screen coordinates. false = no finger (or the chip didn't answer).
static bool touchRead(int& x, int& y, int& rx, int& ry) {
  uint8_t d[5];
  if (!i2cReadReg(TOUCH_ADDR, 0x02, d, 5)) return false;   // TD_STATUS, P1_XH, P1_XL, P1_YH, P1_YL
  uint8_t n = d[0] & 0x0F;
  if (n == 0 || n > 2) return false;
  rx = ((d[1] & 0x0F) << 8) | d[2];
  ry = ((d[3] & 0x0F) << 8) | d[4];
  float sx = tcal[0] * rx + tcal[1] * ry + tcal[2];
  float sy = tcal[3] * rx + tcal[4] * ry + tcal[5];
  x = constrain((int)lroundf(sx), 0, SCREEN_W - 1);
  y = constrain((int)lroundf(sy), 0, SCREEN_H - 1);
  return true;
}

static void sendTouch(TouchKind k, int x = 0, int y = 0, int rx = 0, int ry = 0) {
  TouchEv ev = {k, (int16_t)x, (int16_t)y, (int16_t)rx, (int16_t)ry};
  xQueueSend(touchQ, &ev, 0);
}

static void inputTask(void*) {
  bool was = false, dragged = false;
  int x0 = 0, y0 = 0, rx0 = 0, ry0 = 0, lx = 0, ly = 0, acc = 0;
  bool btnDown = false, btnLong = false;
  uint8_t btnStable = 0;
  uint32_t btnAt = 0;
  for (;;) {
    int x, y, rx, ry;
    bool down = touchRead(x, y, rx, ry);
    if (down && !was) { x0 = lx = x; y0 = ly = y; rx0 = rx; ry0 = ry; dragged = false; acc = 0; }
    else if (down) {
      if (!dragged && (abs(x - x0) > TAP_SLOP || abs(y - y0) > TAP_SLOP)) dragged = true;
      if (dragged && abs(y - y0) > abs(x - x0)) {                // vertical drag scrolls lists
        acc += y - ly;
        while (acc <= -DRAG_STEP) { acc += DRAG_STEP; sendTouch(TE_STEP, +1); }   // finger up -> next item
        while (acc >= DRAG_STEP)  { acc -= DRAG_STEP; sendTouch(TE_STEP, -1); }
      }
      lx = x; ly = y;
    } else if (was) {                                            // finger lifted
      if (!dragged) sendTouch(TE_TAP, x0, y0, rx0, ry0);
      else if (lx - x0 > SWIPE_BACK && abs(ly - y0) < 50) sendTouch(TE_BACK);
    }
    was = down;

    // BOOT key, debounced (30 ms stable): short press = BUTTON_EVENT, hold 2 s = touch calibration
    bool b = digitalRead(PIN_BUTTON) == LOW;
    if (b != btnDown) {
      if (++btnStable >= 3) {
        btnDown = b;
        btnStable = 0;
        if (b) { btnAt = millis(); btnLong = false; }
        else if (!btnLong) sendTouch(TE_BUTTON);
      }
    } else btnStable = 0;
    if (btnDown && !btnLong && millis() - btnAt >= 2000) { btnLong = true; sendTouch(TE_CAL); }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

static bool initTouch() {
  pinMode(PIN_TOUCH_RST, OUTPUT);
  digitalWrite(PIN_TOUCH_RST, LOW);
  delay(10);
  digitalWrite(PIN_TOUCH_RST, HIGH);
  delay(300);                                    // FT6336 needs ~300 ms after reset
  pinMode(PIN_TOUCH_INT, INPUT_PULLUP);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);

  char found[64] = "";
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) snprintf(found + strlen(found), sizeof found - strlen(found), " 0x%02X", a);
  }
  uint8_t id = 0, vendor = 0;
  bool ok = i2cReadReg(TOUCH_ADDR, 0xA3, &id, 1);
  i2cReadReg(TOUCH_ADDR, 0xA8, &vendor, 1);
  LOG("[i2c] devices:%s\n", found[0] ? found : " none");
  LOG("[touch] %s  chip id 0x%02X (FT6336G = 0x64)  vendor 0x%02X\n", ok ? "found" : "NOT FOUND", id, vendor);
  return ok;
}

static void startInputTask() {
  touchQ = xQueueCreate(16, sizeof(TouchEv));
  xTaskCreatePinnedToCore(inputTask, "input", 4096, nullptr, 2, nullptr, 0);
}

static void onTap(const TouchEv& ev) {
  UiHit h = uiHitTest(S, ev.x, ev.y);
  static const char* const HN[] = {"none", "back", "item", "action"};
  LOG("[touch] tap raw=(%d,%d) screen=(%d,%d) scr=%d hit=%s %d\n", ev.rawX, ev.rawY, ev.x, ev.y, S.screen,
      HN[h.kind], h.index);
  switch (h.kind) {
    case HIT_BACK:   handle(EV_LEFT); break;
    case HIT_ACTION: handle(EV_CLICK); break;
    case HIT_ITEM:
      if (S.screen == SCR_HOME)       { S.menuSel = h.index; handle(EV_CLICK); }
      else if (S.screen == SCR_TOOLS) { S.toolSel = h.index; handle(EV_CLICK); }
      else if (S.screen == SCR_FILES) { S.fileSel = h.index; handle(EV_CLICK); }
      else if (S.screen == SCR_SCAN)  { S.netSel = h.index; dirty = true; }
      break;
    default: break;
  }
}

// ---- touch calibration: tap CAL_POINTS crosshairs, least-squares fit an affine map, keep it if it agrees
static int16_t calRaw[CAL_POINTS][2];

static void startCalibration() {
  S.calStep = 0;
  S.calFailed = false;
  openScreen(SCR_TOUCHCAL);
  LOG("[cal] start\n");
}

// Fit out = p*rx + q*ry + r for one screen axis (0 = x, 1 = y) via the 3x3 normal equations.
static bool fitAxis(int axis, float out[3]) {
  double m[3][4] = {};
  for (int i = 0; i < CAL_POINTS; i++) {
    const double v[3] = {(double)calRaw[i][0], (double)calRaw[i][1], 1.0};
    const double t = CAL_TARGETS[i][axis];
    for (int r = 0; r < 3; r++) {
      for (int c = 0; c < 3; c++) m[r][c] += v[r] * v[c];
      m[r][3] += v[r] * t;
    }
  }
  for (int c = 0; c < 3; c++) {                       // Gauss-Jordan, partial pivoting
    int p = c;
    for (int r = c + 1; r < 3; r++) if (fabs(m[r][c]) > fabs(m[p][c])) p = r;
    if (fabs(m[p][c]) < 1e-9) return false;           // all taps in one spot / on one line
    for (int k = 0; k < 4; k++) std::swap(m[c][k], m[p][k]);
    for (int r = 0; r < 3; r++) {
      if (r == c) continue;
      double f = m[r][c] / m[c][c];
      for (int k = c; k < 4; k++) m[r][k] -= f * m[c][k];
    }
  }
  for (int r = 0; r < 3; r++) out[r] = (float)(m[r][3] / m[r][r]);
  return true;
}

static void calTap(int rx, int ry) {
  calRaw[S.calStep][0] = rx;
  calRaw[S.calStep][1] = ry;
  LOG("[cal] point %d target=(%d,%d) raw=(%d,%d)\n", S.calStep + 1, CAL_TARGETS[S.calStep][0],
      CAL_TARGETS[S.calStep][1], rx, ry);
  dirty = true;
  if (++S.calStep < CAL_POINTS) return;

  float t[6];
  bool ok = fitAxis(0, t) && fitAxis(1, t + 3);
  float worst = 0;
  for (int i = 0; ok && i < CAL_POINTS; i++) {
    float ex = t[0] * calRaw[i][0] + t[1] * calRaw[i][1] + t[2] - CAL_TARGETS[i][0];
    float ey = t[3] * calRaw[i][0] + t[4] * calRaw[i][1] + t[5] - CAL_TARGETS[i][1];
    worst = std::max(worst, sqrtf(ex * ex + ey * ey));
  }
  LOG("[cal] fit sx=%.4f*rx %+.4f*ry %+.1f   sy=%.4f*rx %+.4f*ry %+.1f   worst error %.1f px\n",
      t[0], t[1], t[2], t[3], t[4], t[5], worst);
  if (!ok || worst > 20) {                            // a mis-tap; the 4th point checks the other 3
    LOG("[cal] rejected, asking again\n");
    S.calStep = 0;
    S.calFailed = true;
    return;
  }
  memcpy(tcal, t, sizeof tcal);
  prefs.putBytes("tcal", tcal, sizeof tcal);
  LOG("[cal] saved\n");
  S.calFailed = false;
  openScreen(SCR_HOME);
}

static void pollTouch() {
  TouchEv ev;
  while (xQueueReceive(touchQ, &ev, 0) == pdTRUE) {
    if (ev.kind == TE_CAL) { startCalibration(); continue; }
    if (S.screen == SCR_TOUCHCAL) {
      if (ev.kind == TE_TAP) calTap(ev.rawX, ev.rawY);
      else if (ev.kind == TE_BUTTON) { LOG("[cal] cancelled\n"); openScreen(SCR_HOME); }
      continue;
    }
    switch (ev.kind) {
      case TE_TAP:    onTap(ev); break;
      case TE_STEP:   handle(ev.x > 0 ? EV_DOWN : EV_UP); break;
      case TE_BACK:   LOG("[touch] swipe back\n"); handle(EV_LEFT); break;
      case TE_BUTTON: LOG("[btn] BOOT\n"); handle(BUTTON_EVENT); break;
      default: break;
    }
  }
}
#endif

// ===========================================================================
//  Screens and navigation
// ===========================================================================
static void openScreen(Screen next) {
  if (S.screen == SCR_SCAN && next != SCR_SCAN) stopWifi();
  S.screen = next;
  switch (next) {
    case SCR_SCAN:   startScan(); break;
    case SCR_COMMS:  S.unread = 0; break;
    case SCR_SYSTEM: readSystem(); break;
    case SCR_FILES:  if (mountSD()) loadDir(); break;
    default: break;
  }
  dirty = true;
}

static void saveSettings() {
  prefs.putUChar("bright", S.brightness);
  prefs.putBool("scan", S.scanlines);
  prefs.putBool("glow", S.glow);
}

static void moveList(int& sel, int& scroll, int count, int rows, int delta) {
  if (!count) return;
  sel += delta;
  if (sel < 0) sel = 0;
  if (sel >= count) sel = count - 1;
  if (sel < scroll) scroll = sel;
  if (sel >= scroll + rows) scroll = sel - rows + 1;
}

static void filesBack() {
  if (strcmp(S.path, "/") == 0) { openScreen(SCR_HOME); return; }
  char* slash = strrchr(S.path, '/');
  if (slash == S.path) S.path[1] = 0;
  else *slash = 0;
  loadDir();
}

static void handle(Ev e, char key) {
  static const char* const EV_NAMES[] = {"UP", "DOWN", "LEFT", "RIGHT", "CLICK", "KEY"};
  if (e == EV_KEY) LOG("[in] KEY 0x%02X '%c' scr=%d\n", (uint8_t)key, key >= 32 && key <= 126 ? key : '.', S.screen);
  else LOG("[in] %s scr=%d\n", EV_NAMES[e], S.screen);
  dirty = true;
  bool enter = e == EV_KEY && (key == '\r' || key == '\n');
  bool bksp = e == EV_KEY && key == 0x08;

  if (S.screen == SCR_COMMS && e == EV_KEY) {
    size_t len = strlen(S.input);
    if (enter) sendMessage();
    else if (bksp) { if (len) S.input[len - 1] = 0; else openScreen(SCR_HOME); }
    else if (key >= 32 && key <= 126 && len < sizeof S.input - 1) { S.input[len] = key; S.input[len + 1] = 0; }
    return;
  }
  if (enter) e = EV_CLICK;
  bool back = e == EV_LEFT || bksp;

  switch (S.screen) {
    case SCR_HOME:
      if (e == EV_UP)   S.menuSel = (S.menuSel + MENU_COUNT - 1) % MENU_COUNT;
      if (e == EV_DOWN) S.menuSel = (S.menuSel + 1) % MENU_COUNT;
      if (e == EV_CLICK) openScreen((Screen)(SCR_SYSTEM + S.menuSel));
      break;

    case SCR_SCAN:
      if (back) openScreen(SCR_HOME);
      else if (e == EV_UP)    moveList(S.netSel, S.netScroll, S.netCount, SCAN_ROWS, -1);
      else if (e == EV_DOWN)  moveList(S.netSel, S.netScroll, S.netCount, SCAN_ROWS, 1);
      else if (e == EV_CLICK && !S.scanning) startScan();
      break;

    case SCR_TOOLS:
      if (back) openScreen(SCR_HOME);
      else if (e == EV_UP)   S.toolSel = (S.toolSel + 2) % 3;
      else if (e == EV_DOWN) S.toolSel = (S.toolSel + 1) % 3;
      else if (e == EV_CLICK || e == EV_RIGHT) {
        if (S.toolSel == 0) {
          if (e == EV_RIGHT) S.brightness = S.brightness < 16 ? S.brightness + 1 : 16;
          else S.brightness = S.brightness >= 16 ? 2 : (S.brightness + 2 > 16 ? 16 : S.brightness + 2);
          setBacklight(S.brightness);
        }
        if (S.toolSel == 1) S.scanlines = !S.scanlines;
        if (S.toolSel == 2) S.glow = !S.glow;
        saveSettings();
      }
      break;

    case SCR_FILES:
      if (!S.sdOk) {
        if (back) openScreen(SCR_HOME);
        else if (e == EV_CLICK && mountSD()) loadDir();
      } else if (back) filesBack();
      else if (e == EV_UP)   moveList(S.fileSel, S.fileScroll, S.fileCount, FILE_ROWS, -1);
      else if (e == EV_DOWN) moveList(S.fileSel, S.fileScroll, S.fileCount, FILE_ROWS, 1);
      else if (e == EV_CLICK && S.fileCount && S.files[S.fileSel].dir) {
        size_t pl = strlen(S.path);
        if (pl + strlen(S.files[S.fileSel].name) + 2 < sizeof S.path) {
          if (pl > 1) strcat(S.path, "/");
          strcat(S.path, S.files[S.fileSel].name);
          loadDir();
        }
      }
      break;

    case SCR_COMMS:   // non-keyboard input on COMMS
    case SCR_SYSTEM:
      if (back || e == EV_CLICK) openScreen(SCR_HOME);
      break;

    default: break;
  }
}

// ===========================================================================
//  Boot
// ===========================================================================
static int bootStep(const char* label) {
  int i = S.bootCount++;
  snprintf(S.boot[i].label, sizeof S.boot[i].label, "%s", label);
  S.boot[i].status = 0;
  render();
  return i;
}

static void bootDone(int i, uint8_t status) {
  static const char* const ST[] = {"...", "OK", "FAIL", "--"};
  LOG("[boot] %-12s %s\n", S.boot[i].label, ST[status & 3]);
  S.boot[i].status = status;
  render();
  delay(140);
}

void setup() {
  Serial.begin(115200);
#ifdef PIN_POWER_ON
  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);
#endif
  uint32_t poweredAt = millis();
#ifdef PIN_LED_RGB
  rgbLedWrite(PIN_LED_RGB, 0, 0, 0);   // the LED keeps its last colour across resets
#endif

  // every SPI device deselected before the bus starts
  pinMode(PIN_TFT_CS, OUTPUT); digitalWrite(PIN_TFT_CS, HIGH);
#if SD_OVER_SPI
  pinMode(PIN_SD_CS, OUTPUT); digitalWrite(PIN_SD_CS, HIGH);
#endif
#if HAS_LORA
  pinMode(PIN_RADIO_CS, OUTPUT); digitalWrite(PIN_RADIO_CS, HIGH);
#endif
#if !BACKLIGHT_PWM
  pinMode(PIN_TFT_BL, OUTPUT);
#endif
  pinMode(PIN_SPI_MISO, INPUT_PULLUP);
  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);

  fb = (uint16_t*)ps_malloc(SCREEN_W * SCREEN_H * 2);
  if (!fb) fb = (uint16_t*)malloc(SCREEN_W * SCREEN_H * 2);

  prefs.begin("deck", false);
  S.brightness = prefs.getUChar("bright", DEFAULT_BRIGHTNESS);
  S.scanlines = prefs.getBool("scan", DEFAULT_SCANLINES);
  S.glow = prefs.getBool("glow", DEFAULT_GLOW);
  S.title = DECK_TITLE;
  S.freqMhz = LORA_FREQ_MHZ;
  S.touchUi = HAS_TOUCH;
  S.hasRadio = HAS_LORA;
  S.boardName = BOARD_NAME;
  S.bootTotal = 4 + HAS_KEYBOARD + HAS_TRACKBALL + HAS_LORA + HAS_TOUCH;   // + DISPLAY, SD, BATTERY, SYSTEM
  S.screen = SCR_BOOT;
  LOG("\n[cyberdeck] board %s\n", BOARD_NAME);

  tftInit();
  int i = bootStep("DISPLAY");
  setBacklight(S.brightness);
#if TFT_IS_ILI9341
  logPanel();
#endif
  bootDone(i, 1);

#if HAS_KEYBOARD
  i = bootStep("KEYBOARD");
  while (millis() - poweredAt < 600) delay(10);   // keyboard MCU needs time after power-on
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  for (int tries = 0; tries < 5 && !S.inputOk; tries++) { S.inputOk = probeKeyboard(); if (!S.inputOk) delay(100); }
  bootDone(i, S.inputOk ? 1 : 2);
#else
  (void)poweredAt;
#endif

#if HAS_TOUCH
  i = bootStep("TOUCH");
  S.inputOk = initTouch();
  touchDefaultCal();
  bool haveCal = prefs.isKey("tcal") && prefs.getBytes("tcal", tcal, sizeof tcal) == sizeof tcal;
  if (haveCal) LOG("[cal] loaded sx=%.4f*rx %+.4f*ry %+.1f   sy=%.4f*rx %+.4f*ry %+.1f\n",
                   tcal[0], tcal[1], tcal[2], tcal[3], tcal[4], tcal[5]);
  else { touchDefaultCal(); LOG("[cal] none stored, using the boards.h default\n"); }
  bootDone(i, S.inputOk ? 1 : 2);
#endif

#if HAS_TRACKBALL
  i = bootStep("TRACKBALL");
  initTrackball();
  bootDone(i, 1);
#endif

#if HAS_LORA
  i = bootStep("LORA SX1262");
  initRadio();
  bootDone(i, S.radioOk ? 1 : 2);
#endif

  i = bootStep("SD CARD");
  bootDone(i, mountSD() ? 1 : 3);

  i = bootStep("BATTERY");
  for (int k = 0; k < 5; k++) readBattery();
  bootDone(i, 1);

  i = bootStep("SYSTEM");
  initSystemInfo();
  bootDone(i, 1);

#if HAS_TOUCH
  startInputTask();
#endif
  delay(350);
  S.screen = SCR_HOME;
  dirty = true;
#if HAS_TOUCH
  if (S.inputOk && !haveCal) startCalibration();   // first boot on this board: line touch up with the screen
#endif
}

void loop() {
#if HAS_TRACKBALL
  pollTrackball();
#endif
#if HAS_KEYBOARD
  pollKeyboard();
#endif
#if HAS_TOUCH
  pollTouch();
#endif
#if HAS_LORA
  pollRadio();
#endif
  pollScan();

  static uint32_t lastSlow = 0;
  if (millis() - lastSlow > 1000) {
    lastSlow = millis();
    readBattery();
    readSystem();
    static uint8_t tick = 0;
    if (++tick % 5 == 0)
      LOG("[stat] up=%lus scr=%d batt=%lumV(pin) %.2fV %d%% heap=%lu psram=%lu draw=%luus push=%luus in=%d rf=%d(%d) sd=%d\n",
          (unsigned long)S.uptimeS, S.screen, (unsigned long)battRawMv, S.battV, S.battPct,
          (unsigned long)S.heapFree, (unsigned long)S.psramFree, (unsigned long)drawUs, (unsigned long)pushUs,
          S.inputOk, S.radioOk, radioErr, S.sdOk);
  }

  uint32_t interval = 1000;
  if (S.screen == SCR_HOME) interval = 90;                      // twinkling stars, beacon
  else if (S.screen == SCR_SCAN && S.scanning) interval = 50;   // radar sweep
  else if (S.screen == SCR_COMMS) interval = 250;               // cursor blink
  else if (S.screen == SCR_TOUCHCAL) interval = 150;            // blinking crosshair
  if (dirty || millis() - lastFrame >= interval) render();
  delay(2);
}
