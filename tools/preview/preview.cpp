// Desktop preview for the cyberdeck UI.
//
// Renders every screen through the SAME drawing code the firmware uses (Cyberdeck/gfx.cpp + ui.cpp)
// and writes PNGs, so UI changes can be checked without flashing.
//
//   make preview                       -> build/preview/<scene>.png (2x) + all.png (1x sheet) for the T-Deck,
//                                         es3c28p_<scene>.png + all_es3c28p.png for the 2.8" touch board
//   build/preview/render OUTDIR [--scale N] [--only NAME]
//
// To preview a new screen or state: add a scene to SCENES below.
// No dependencies beyond a C++17 compiler (PNG writer is built in, uncompressed).

#include "gfx.h"
#include "ui.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

// ------------------------------------------------------------------ minimal PNG writer
static uint32_t crcTable[256];
static void crcInit() {
  for (uint32_t n = 0; n < 256; n++) {
    uint32_t c = n;
    for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    crcTable[n] = c;
  }
}
static uint32_t crc(const uint8_t* d, size_t n, uint32_t c = 0xFFFFFFFFu) {
  for (size_t i = 0; i < n; i++) c = crcTable[(c ^ d[i]) & 0xFF] ^ (c >> 8);
  return c;
}
static void be32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x);
}
static void chunk(FILE* f, const char* type, const std::vector<uint8_t>& data) {
  std::vector<uint8_t> buf;
  be32(buf, (uint32_t)data.size());
  buf.insert(buf.end(), type, type + 4);
  buf.insert(buf.end(), data.begin(), data.end());
  uint32_t c = crc(buf.data() + 4, buf.size() - 4) ^ 0xFFFFFFFFu;
  be32(buf, c);
  fwrite(buf.data(), 1, buf.size(), f);
}
static bool writePng(const char* path, const std::vector<uint8_t>& rgb, int w, int h) {
  FILE* f = fopen(path, "wb");
  if (!f) { fprintf(stderr, "cannot write %s\n", path); return false; }
  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  fwrite(sig, 1, 8, f);
  std::vector<uint8_t> ihdr;
  be32(ihdr, w); be32(ihdr, h);
  ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});           // 8-bit RGB
  chunk(f, "IHDR", ihdr);

  std::vector<uint8_t> raw;                           // filter byte 0 + row
  raw.reserve((size_t)(w * 3 + 1) * h);
  for (int y = 0; y < h; y++) {
    raw.push_back(0);
    raw.insert(raw.end(), rgb.begin() + (size_t)y * w * 3, rgb.begin() + (size_t)(y + 1) * w * 3);
  }
  std::vector<uint8_t> z = {0x78, 0x01};              // zlib header, stored blocks
  uint32_t a = 1, b = 0;
  for (uint8_t v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
  for (size_t off = 0; off < raw.size() || off == 0;) {
    size_t n = raw.size() - off > 65535 ? 65535 : raw.size() - off;
    z.push_back(off + n == raw.size() ? 1 : 0);
    z.push_back(n & 0xFF); z.push_back(n >> 8);
    z.push_back(~n & 0xFF); z.push_back((~n >> 8) & 0xFF);
    z.insert(z.end(), raw.begin() + off, raw.begin() + off + n);
    off += n;
    if (n == 0) break;
  }
  be32(z, (b << 16) | a);
  chunk(f, "IDAT", z);
  chunk(f, "IEND", {});
  fclose(f);
  return true;
}

// ------------------------------------------------------------------ rendering
static uint16_t fb[SCREEN_W * SCREEN_H];

// Draw the state and post-process exactly like the device does; returns 320x240 RGB888.
static std::vector<uint8_t> render(const UiState& s) {
  Canvas g;
  g.fb = fb;
  drawScreen(g, s);
  std::vector<uint8_t> rgb(SCREEN_W * SCREEN_H * 3);
  uint8_t row[SCREEN_W * 2];
  for (int y = 0; y < SCREEN_H; y++) {
    composeRow(fb, y, row, s.glow, s.scanlines);
    for (int x = 0; x < SCREEN_W; x++) {
      uint16_t c = (row[2 * x] << 8) | row[2 * x + 1];
      uint8_t r = (c >> 11) << 3, gg = ((c >> 5) & 63) << 2, bb = (c & 31) << 3;
      uint8_t* p = &rgb[(y * SCREEN_W + x) * 3];
      p[0] = r | r >> 5; p[1] = gg | gg >> 6; p[2] = bb | bb >> 5;
    }
  }
  return rgb;
}

static std::vector<uint8_t> upscale(const std::vector<uint8_t>& src, int w, int h, int k) {
  std::vector<uint8_t> out((size_t)w * k * h * k * 3);
  for (int y = 0; y < h * k; y++)
    for (int x = 0; x < w * k; x++)
      memcpy(&out[((size_t)y * w * k + x) * 3], &src[((size_t)(y / k) * w + x / k) * 3], 3);
  return out;
}

// ------------------------------------------------------------------ sample data
static void chat(UiState& s, int kind, const char* t, int rssi = 0) {
  ChatLine& l = s.chat[s.chatCount++];
  snprintf(l.text, sizeof l.text, "%s", t);
  l.kind = kind;
  l.rssi = rssi;
}

static void baseline(UiState& s) {
  s = UiState();
  s.title = DECK_TITLE;
  s.battPct = 87; s.battV = 4.02f;
  s.radioOk = true; s.inputOk = true; s.sdOk = true;
  snprintf(s.callsign, sizeof s.callsign, "DECK-7F3A");
  s.freqMhz = LORA_FREQ_MHZ;
  s.ms = 5200;
  s.brightness = DEFAULT_BRIGHTNESS; s.scanlines = DEFAULT_SCANLINES; s.glow = DEFAULT_GLOW;
  snprintf(s.chip, sizeof s.chip, "ESP32-S3 REV 0.2");
  s.cores = 2; s.cpuMhz = 240; s.flashMB = 16;
  s.psramFree = 8000000; s.psramTotal = 8386295; s.heapFree = 251 * 1024; s.heapTotal = 372 * 1024;
  s.uptimeS = 4521;
  snprintf(s.mac, sizeof s.mac, "DC:54:75:C8:7F:3A");
}

static void nets(UiState& s) {
  const char* ss[] = {"NEON_GRID", "Mojave Base 5G", "", "corp-guest", "WASTELAND-NET", "DESERT_REPEATER",
                      "xfinitywifi", "Camp Spectre", "pixel_7a_hotspot", "HP-Print-3C-OfficeJet", "RV_Park_Free", "SCRAP-MESH"};
  int8_t rs[] = {-41, -52, -58, -63, -66, -70, -72, -77, -79, -84, -88, -91};
  uint8_t ch[] = {6, 11, 1, 6, 3, 11, 1, 9, 6, 11, 4, 1};
  bool sec[] = {1, 1, 1, 0, 1, 1, 0, 1, 1, 0, 0, 1};
  s.netCount = 12;
  for (int i = 0; i < 12; i++) {
    snprintf(s.nets[i].ssid, sizeof s.nets[i].ssid, "%s", ss[i]);
    s.nets[i].rssi = rs[i]; s.nets[i].channel = ch[i]; s.nets[i].secure = sec[i];
  }
}

static void files(UiState& s) {
  const char* fn[] = {"LOGS", "MAPS", "MUSIC", "notes.txt", "festival_map.png", "callsigns.csv", "readme.md"};
  bool dir[] = {1, 1, 1, 0, 0, 0, 0};
  uint32_t sz[] = {0, 0, 0, 2210, 1843200, 512, 9100};
  s.fileCount = 7;
  for (int i = 0; i < 7; i++) {
    snprintf(s.files[i].name, sizeof s.files[i].name, "%s", fn[i]);
    s.files[i].dir = dir[i]; s.files[i].size = sz[i];
  }
  snprintf(s.path, sizeof s.path, "/");
  s.sdUsedGB = 3.2f; s.sdTotalGB = 29.7f;
}

struct Scene { const char* name; void (*setup)(UiState&); };

static const Scene SCENES[] = {
  {"home", [](UiState& s) { s.screen = SCR_HOME; }},
  {"home_unread", [](UiState& s) { s.screen = SCR_HOME; s.menuSel = 2; s.unread = 2; s.ms = 5600; }},
  {"home_lowbatt", [](UiState& s) { s.screen = SCR_HOME; s.battPct = 9; s.radioOk = false; s.sdOk = false; }},
  {"boot", [](UiState& s) {
     s.screen = SCR_BOOT; s.ms = 1250;
     const char* tdeck[] = {"DISPLAY", "KEYBOARD", "TRACKBALL", "LORA SX1262", "SD CARD", "BATTERY", "SYSTEM"};
     const char* touch[] = {"DISPLAY", "TOUCH", "SD CARD", "BATTERY", "SYSTEM"};
     uint8_t stT[] = {1, 1, 1, 1, 3, 0, 0}, stP[] = {1, 1, 3, 0, 0};
     const char** bl = s.touchUi ? touch : tdeck;
     uint8_t* st = s.touchUi ? stP : stT;
     s.bootCount = s.touchUi ? 4 : 6;
     for (int i = 0; i < s.bootTotal; i++) { snprintf(s.boot[i].label, sizeof s.boot[i].label, "%s", bl[i]); s.boot[i].status = st[i]; }
   }},
  {"system", [](UiState& s) { s.screen = SCR_SYSTEM; }},
  {"scan", [](UiState& s) { s.screen = SCR_SCAN; nets(s); s.netSel = 1; }},
  {"scan_radar", [](UiState& s) { s.screen = SCR_SCAN; s.scanning = true; s.ms = 7300; }},
  {"comms", [](UiState& s) {
     s.screen = SCR_COMMS; s.ms = 600;
     chat(s, 0, "-- RADIO ONLINE 915.0 MHZ --");
     chat(s, 2, "DECK-11C0: anyone near the main stage?", -78);
     chat(s, 1, "YOU: yeah by the scrap tower");
     chat(s, 2, "DECK-11C0: meet at the water tank in 10", -81);
     chat(s, 2, "DECK-9B22: bring the spare battery pls", -96);
     chat(s, 1, "YOU: copy. on my way");
     snprintf(s.input, sizeof s.input, "anyone got a usb-c cable");
   }},
  {"comms_empty", [](UiState& s) { s.screen = SCR_COMMS; s.ms = 0; }},
  {"tools", [](UiState& s) { s.screen = SCR_TOOLS; }},
  {"files", [](UiState& s) { s.screen = SCR_FILES; files(s); s.fileSel = 4; }},
  {"files_nocard", [](UiState& s) { s.screen = SCR_FILES; s.sdOk = false; }},
};

// Each scene is rendered once per board profile. The T-Deck keeps the original file names.
struct Profile { const char* prefix; const char* sheet; void (*apply)(UiState&); };
static const Profile PROFILES[] = {
  {"", "all.png", [](UiState&) {}},
  {"es3c28p_", "all_es3c28p.png", [](UiState& s) {
     s.touchUi = true; s.hasRadio = false; s.radioOk = false; s.boardName = "ES3C28P"; s.bootTotal = 5;
   }},
};

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s OUTDIR [--scale N] [--only NAME]\n", argv[0]);
    return 2;
  }
  const char* out = argv[1];
  int scale = 2;
  const char* only = nullptr;
  for (int i = 2; i < argc; i++) {
    if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--only") && i + 1 < argc) only = argv[++i];
  }
  if (scale < 1) scale = 1;
  crcInit();

  const int n = sizeof SCENES / sizeof SCENES[0];
  const int cols = 3, pad = 8;
  const int rows = (n + cols - 1) / cols;
  const int SW = cols * SCREEN_W + (cols + 1) * pad, SH = rows * SCREEN_H + (rows + 1) * pad;

  static UiState s;
  int written = 0;
  for (const Profile& prof : PROFILES) {
    std::vector<uint8_t> sheet((size_t)SW * SH * 3, 18);
    for (int i = 0; i < n; i++) {
      std::string name = std::string(prof.prefix) + SCENES[i].name;
      if (only && name != only) continue;
      baseline(s);
      prof.apply(s);
      SCENES[i].setup(s);
      std::vector<uint8_t> rgb = render(s);
      std::string path = std::string(out) + "/" + name + ".png";
      if (!writePng(path.c_str(), scale > 1 ? upscale(rgb, SCREEN_W, SCREEN_H, scale) : rgb,
                    SCREEN_W * scale, SCREEN_H * scale)) return 1;
      printf("%s\n", path.c_str());
      written++;
      int ox = pad + (i % cols) * (SCREEN_W + pad), oy = pad + (i / cols) * (SCREEN_H + pad);
      for (int y = 0; y < SCREEN_H; y++)
        memcpy(&sheet[((size_t)(oy + y) * SW + ox) * 3], &rgb[(size_t)y * SCREEN_W * 3], SCREEN_W * 3);
    }
    if (!only) {
      std::string path = std::string(out) + "/" + prof.sheet;
      if (!writePng(path.c_str(), sheet, SW, SH)) return 1;
      printf("%s\n", path.c_str());
    }
  }
  if (only && !written) { fprintf(stderr, "no scene named %s\n", only); return 1; }
  return 0;
}
