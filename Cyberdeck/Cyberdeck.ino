// ===========================================================================
//  CYBERDECK firmware for the LILYGO T-Deck / T-Deck Plus (ESP32-S3)
//
//  Board:  ESP32S3 Dev Module, USB CDC On Boot: Enabled,
//          Flash Size: 16MB, PSRAM: OPI PSRAM, Partition: 16M Flash (3MB APP/9.9MB FATFS)
//  Library: RadioLib (Library Manager)
//
//  Controls
//    Trackball up/down ... move          Trackball click / Enter ... select
//    Trackball left ...... back          Backspace ................ back
//    COMMS: type on the keyboard, Enter sends over LoRa
// ===========================================================================
#include <Arduino.h>
#include <algorithm>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <SD.h>
#include <Preferences.h>
#include <RadioLib.h>
#include "config.h"
#include "gfx.h"
#include "ui.h"

// ---- T-Deck pin map (from LilyGO's utilities.h) ----------------------------
#define PIN_POWER_ON    10   // must be HIGH to power keyboard, radio, display
#define PIN_I2C_SDA     18
#define PIN_I2C_SCL      8
#define PIN_BAT_ADC      4
#define PIN_SPI_SCK     40
#define PIN_SPI_MOSI    41
#define PIN_SPI_MISO    38
#define PIN_TFT_CS      12
#define PIN_TFT_DC      11
#define PIN_TFT_BL      42
#define PIN_SD_CS       39
#define PIN_RADIO_CS     9
#define PIN_RADIO_BUSY  13
#define PIN_RADIO_RST   17
#define PIN_RADIO_DIO1  45
#define PIN_BALL_RIGHT   2
#define PIN_BALL_UP      3
#define PIN_BALL_LEFT    1
#define PIN_BALL_DOWN   15
#define PIN_BALL_CLICK   0
#define KEYBOARD_ADDR 0x55

// ---- globals ---------------------------------------------------------------
enum Ev { EV_UP, EV_DOWN, EV_LEFT, EV_RIGHT, EV_CLICK, EV_KEY };
static void handle(Ev e, char key = 0);

static UiState S;
static uint16_t* fb = nullptr;
static uint8_t lineBuf[SCREEN_W * 2];
static bool dirty = true;
static uint32_t lastFrame = 0;
static Preferences prefs;

SX1262 radio = new Module(PIN_RADIO_CS, PIN_RADIO_DIO1, PIN_RADIO_RST, PIN_RADIO_BUSY, SPI);
static volatile bool radioFlag = false;
static void IRAM_ATTR onRadio() { radioFlag = true; }

// ===========================================================================
//  Display: ST7789 320x240 on the shared SPI bus
// ===========================================================================
static SPISettings tftSPI(40000000, MSBFIRST, SPI_MODE0);

static void tftCmd(uint8_t cmd, const uint8_t* data = nullptr, size_t n = 0) {
  digitalWrite(PIN_TFT_DC, LOW);
  SPI.write(cmd);
  digitalWrite(PIN_TFT_DC, HIGH);
  if (n) SPI.writeBytes(data, n);
}

static void tftInit() {
  // Init sequence from LilyGO's T-Deck examples, landscape (MADCTL 0x60).
  struct Step { uint8_t cmd, len; uint8_t data[14]; uint16_t waitMs; };
  static const Step seq[] = {
    {0x01, 0, {0}, 120},
    {0x11, 0, {0}, 120},
    {0x3A, 1, {0x55}, 0},
    {0x36, 1, {0x60}, 0},
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
  pinMode(PIN_TFT_DC, OUTPUT);
  SPI.beginTransaction(tftSPI);
  digitalWrite(PIN_TFT_CS, LOW);
  for (const Step& s : seq) {
    tftCmd(s.cmd, s.data, s.len);
    if (s.waitMs) delay(s.waitMs);
  }
  digitalWrite(PIN_TFT_CS, HIGH);
  SPI.endTransaction();
}

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

// The T-Deck backlight driver takes pulses: 16 levels, 16 = brightest (from LilyGO's example).
static void setBacklight(uint8_t value) {
  static uint8_t level = 0;
  const uint8_t steps = 16;
  if (value == 0) { digitalWrite(PIN_TFT_BL, 0); delay(3); level = 0; return; }
  if (value > steps) value = steps;
  if (level == 0) { digitalWrite(PIN_TFT_BL, 1); level = steps; delayMicroseconds(30); }
  int from = steps - level, to = steps - value;
  int num = (steps + to - from) % steps;
  for (int i = 0; i < num; i++) { digitalWrite(PIN_TFT_BL, 0); digitalWrite(PIN_TFT_BL, 1); }
  level = value;
}

static void render() {
  S.ms = millis();
  Canvas g;
  g.fb = fb;
  drawScreen(g, S);
  pushFrame();
  lastFrame = millis();
  dirty = false;
}

// ===========================================================================
//  Battery
// ===========================================================================
static void readBattery() {
  uint32_t mv = 0;
  for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(PIN_BAT_ADC);
  float v = (mv / 8) * 2 / 1000.0f;   // 1:2 divider on the board
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
}

// ===========================================================================
//  LoRa chat
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

static void initRadio() {
  int st = radio.begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR, LORA_SYNC_WORD, LORA_TX_DBM, 10);
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

// ===========================================================================
//  SD card
// ===========================================================================
static bool mountSD() {
  if (S.sdOk) return true;
  SD.end();
  S.sdOk = SD.begin(PIN_SD_CS, SPI, 4000000) && SD.cardType() != CARD_NONE;
  if (S.sdOk) {
    S.sdTotalGB = SD.totalBytes() / 1e9f;
    S.sdUsedGB = SD.usedBytes() / 1e9f;
  }
  return S.sdOk;
}

static void loadDir() {
  S.fileCount = 0;
  S.fileSel = 0;
  S.fileScroll = 0;
  File dir = SD.open(S.path);
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
//  Input: trackball (interrupt counted), click button, I2C keyboard
// ===========================================================================
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
  pinMode(PIN_BALL_CLICK, INPUT_PULLUP);
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
  bool down = digitalRead(PIN_BALL_CLICK) == LOW;
  if (down != lastDown && now - changed > 30) {
    changed = now;
    lastDown = down;
    if (down) handle(EV_CLICK);
  }
}

static bool probeKeyboard() {
  Wire.beginTransmission(KEYBOARD_ADDR);
  return Wire.endTransmission() == 0;
}

static void pollKeyboard() {
  static uint32_t last = 0;
  if (!S.kbOk || millis() - last < 20) return;
  last = millis();
  if (Wire.requestFrom(KEYBOARD_ADDR, 1) == 1) {
    char c = Wire.read();
    if (c) handle(EV_KEY, c);
  }
}

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
  S.boot[i].status = status;
  render();
  delay(140);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);
  uint32_t poweredAt = millis();

  // every SPI device deselected before the bus starts
  for (uint8_t cs : {PIN_SD_CS, PIN_RADIO_CS, PIN_TFT_CS}) { pinMode(cs, OUTPUT); digitalWrite(cs, HIGH); }
  pinMode(PIN_TFT_BL, OUTPUT);
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
  S.screen = SCR_BOOT;

  tftInit();
  int i = bootStep("DISPLAY");
  setBacklight(S.brightness);
  bootDone(i, 1);

  i = bootStep("KEYBOARD");
  while (millis() - poweredAt < 600) delay(10);   // keyboard MCU needs time after power-on
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  for (int tries = 0; tries < 5 && !S.kbOk; tries++) { S.kbOk = probeKeyboard(); if (!S.kbOk) delay(100); }
  bootDone(i, S.kbOk ? 1 : 2);

  i = bootStep("TRACKBALL");
  initTrackball();
  bootDone(i, 1);

  i = bootStep("LORA SX1262");
  initRadio();
  bootDone(i, S.radioOk ? 1 : 2);

  i = bootStep("SD CARD");
  bootDone(i, mountSD() ? 1 : 3);

  i = bootStep("BATTERY");
  for (int k = 0; k < 5; k++) readBattery();
  bootDone(i, 1);

  i = bootStep("SYSTEM");
  initSystemInfo();
  bootDone(i, 1);

  delay(350);
  S.screen = SCR_HOME;
  dirty = true;
}

void loop() {
  pollTrackball();
  pollKeyboard();
  pollRadio();
  pollScan();

  static uint32_t lastSlow = 0;
  if (millis() - lastSlow > 1000) {
    lastSlow = millis();
    readBattery();
    readSystem();
  }

  uint32_t interval = 1000;
  if (S.screen == SCR_HOME) interval = 90;                      // twinkling stars, beacon
  else if (S.screen == SCR_SCAN && S.scanning) interval = 50;   // radar sweep
  else if (S.screen == SCR_COMMS) interval = 250;               // cursor blink
  if (dirty || millis() - lastFrame >= interval) render();
  delay(2);
}
