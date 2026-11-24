#include "ui.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

const char* const MENU_LABELS[MENU_COUNT] = {"SYSTEM", "SCAN", "COMMS", "TOOLS", "FILES"};

static const int GROUND = 198;   // skyline baseline on the home screen

// ---------------------------------------------------------------- shared bits

static void frame(Canvas& g) { g.roundRect(4, 4, 312, 232, 9, C_MID); }

static void battery(Canvas& g, int xRight, int y, int pct, int scale) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  char buf[8];
  snprintf(buf, sizeof buf, "%d%%", pct);
  bool low = pct <= 15;
  uint16_t line = low ? C_WARN : C_MID, fill = low ? C_WARN : C_BRIGHT;
  int tx = xRight - Canvas::textW(buf, scale);
  int bw = scale == 2 ? 30 : 17, bh = scale == 2 ? 14 : 8;
  int bx = tx - (scale == 2 ? 10 : 6) - bw;
  int by = y + (7 * scale - bh) / 2;
  g.rect(bx, by, bw, bh, line);
  g.fillRect(bx + bw, by + bh / 4, 2, bh - 2 * (bh / 4), line);
  g.fillRect(bx + 2, by + 2, (bw - 4) * pct / 100, bh - 4, fill);
  g.text(tx, y, buf, fill, scale);
}

static void header(Canvas& g, const UiState& s, const char* name, const char* info) {
  g.background();
  frame(g);
  char crumb[40];
  snprintf(crumb, sizeof crumb, "%s /", s.title);
  g.text(18, 13, crumb, C_SOFT, 1);
  g.text(18, 24, name, C_BRIGHT, 2);
  g.fillRect(16, 43, 288, 2, C_MID);
  battery(g, 304, 13, s.battPct, 1);
  if (info) g.textRight(304, 30, info, C_SOFT, 1);
}

static void footer(Canvas& g, const char* hint) { g.text(18, 220, hint, C_SOFT, 1); }

static void scrollbar(Canvas& g, int x, int y, int h, int first, int visible, int total) {
  if (total <= visible) return;
  g.fillRect(x, y, 2, h, C_FAINT);
  int th = h * visible / total;
  if (th < 6) th = 6;
  int ty = y + (h - th) * first / (total - visible);
  g.fillRect(x, ty, 2, th, C_MID);
}

// ---------------------------------------------------------------- home screen

static void windows(Canvas& g, int x, int top, int w, int bottom, int seed, uint32_t t) {
  for (int wy = top + 4; wy + 2 <= bottom - 3; wy += 5)
    for (int wx = x + 3; wx + 2 <= x + w - 3; wx += 4) {
      uint32_t id = hash32(seed * 7919u + wx * 31u + wy * 131u);
      uint32_t period = 2500 + id % 6000;
      uint32_t slot = (t + id % period) / period;
      bool on = hash32(id ^ (slot * 2654435761u)) % 100 < 42;
      g.fillRect(wx, wy, 2, 2, on ? C_WIN_ON : C_WIN_OFF);
    }
}

static void box(Canvas& g, int x, int top, int w, int bottom, int seed, uint32_t t) {
  g.fillRect(x, top, w, bottom - top, C_BUILDING);
  g.rect(x, top, w, bottom - top + 1, C_MID);
  windows(g, x, top, w, bottom, seed, t);
}

static void drawSkyline(Canvas& g, const UiState& s) {
  uint32_t t = s.ms;

  // stars
  for (int i = 0; i < 22; i++) {
    uint32_t h = hash32(i * 977u + 13);
    int x = 164 + h % 142, y = 14 + (h >> 8) % 104;
    if ((x - 288) * (x - 288) + (y - 32) * (y - 32) < 22 * 22) continue;   // keep clear of the moon
    uint32_t tw = hash32(h ^ ((t + (h >> 4) % 900) / 900));
    if (tw % 7 == 0) {
      g.px(x, y, C_BRIGHT);
      g.px(x - 1, y, C_SOFT); g.px(x + 1, y, C_SOFT); g.px(x, y - 1, C_SOFT); g.px(x, y + 1, C_SOFT);
    } else {
      g.px(x, y, (h >> 20) & 1 ? C_SOFT : C_DIM);
    }
  }

  // crescent moon
  const int mx = 288, my = 32, r = 13;
  for (int dy = -r; dy <= r; dy++)
    for (int dx = -r; dx <= r; dx++) {
      if (dx * dx + dy * dy > r * r + r) continue;
      int ex = dx - 7, ey = dy + 4;
      if (ex * ex + ey * ey <= (r - 1) * (r - 1)) continue;
      g.px(mx + dx, my + dy, C_BRIGHT);
    }
  g.fillCircle(mx - 8, my + 3, 1, C_MID);
  g.px(mx - 5, my + 8, C_MID);

  // distant layer
  for (int x = 158, i = 0; x < 308; i++) {
    uint32_t h = hash32(i * 4561u + 7);
    int w = 7 + h % 7, hgt = 18 + (h >> 8) % 46;
    g.dither(x, GROUND - hgt, w, hgt, C_FAINT);
    x += w + 1;
  }

  // foreground buildings, left to right
  box(g, 164, GROUND - 44, 16, GROUND, 1, t);
  g.fillRect(172, GROUND - 48, 5, 4, C_BUILDING); g.rect(172, GROUND - 48, 5, 4, C_MID);   // roof vent

  // stepped tower
  box(g, 182, GROUND - 52, 22, GROUND, 2, t);
  box(g, 185, GROUND - 78, 16, GROUND - 52, 3, t);
  box(g, 188, GROUND - 94, 10, GROUND - 78, 4, t);
  g.vline(193, GROUND - 102, 8, C_MID);

  // block with water tank
  box(g, 206, GROUND - 56, 14, GROUND, 5, t);
  g.vline(209, GROUND - 60, 4, C_MID); g.vline(216, GROUND - 60, 4, C_MID);
  g.fillRect(208, GROUND - 67, 10, 7, C_BUILDING); g.rect(208, GROUND - 67, 10, 7, C_MID);

  // radio mast tower with blinking beacon
  box(g, 222, GROUND - 72, 26, GROUND, 6, t);
  box(g, 228, GROUND - 92, 14, GROUND - 72, 7, t);
  g.fillRect(234, GROUND - 136, 2, 44, C_MID);
  for (int y = GROUND - 128; y < GROUND - 94; y += 8) g.hline(231, y, 8, C_MID);
  bool beacon = (t % 1400) < 650;
  g.fillRect(234, GROUND - 139, 2, 2, beacon ? C_ALERT : C_DIM);

  // domed hall
  box(g, 250, GROUND - 60, 20, GROUND, 8, t);
  {
    int cx = 259, cy = GROUND - 60, rr = 9;
    for (int dy = 0; dy <= rr; dy++) {
      int dx = (int)sqrtf((float)(rr * rr - dy * dy));
      g.hline(cx - dx, cy - dy, 2 * dx + 2, C_BUILDING);
      g.px(cx - dx, cy - dy, C_MID); g.px(cx + dx + 1, cy - dy, C_MID);
      if (dy == rr) g.hline(cx - dx, cy - dy, 2 * dx + 2, C_MID);
    }
    g.vline(259, cy - rr - 5, 5, C_MID); g.vline(260, cy - rr - 5, 5, C_MID);
  }

  // slim tower with antenna
  box(g, 272, GROUND - 100, 16, GROUND, 9, t);
  g.vline(276, GROUND - 116, 16, C_MID);
  g.hline(274, GROUND - 110, 5, C_MID);
  g.fillRect(275, GROUND - 119, 3, 3, C_BRIGHT);

  box(g, 290, GROUND - 46, 16, GROUND, 10, t);

  g.hline(156, GROUND, 152, C_MID);
}

static void drawHome(Canvas& g, const UiState& s) {
  g.background();
  frame(g);
  drawSkyline(g, s);

  g.text(18, 18, s.title, C_BRIGHT, 2);
  int tw = Canvas::textW(s.title, 2);
  g.fillRect(16, 38, tw + 14 > 144 ? tw + 14 : 144, 2, C_MID);

  for (int i = 0; i < MENU_COUNT; i++) {
    int y = 54 + i * 30;
    bool sel = i == s.menuSel;
    if (sel) g.fillRect(14, y - 5, 136, 24, C_HILITE);
    uint16_t c = sel ? C_INK : C_BRIGHT;
    g.text(20, y, ">", c, 2);
    int end = g.text(40, y, MENU_LABELS[i], c, 2);
    if (i == 2 && s.unread > 0 && ((s.ms / 400) & 1))
      g.fillRect(end + 4, y + 4, 6, 6, sel ? C_INK : C_ALERT);
  }

  char st[48];
  snprintf(st, sizeof st, "RF %s  SD %s  KB %s", s.radioOk ? "OK" : "--", s.sdOk ? "OK" : "--",
           s.kbOk ? "OK" : "--");
  g.text(18, 211, st, C_SOFT, 1);
  battery(g, 306, 207, s.battPct, 2);
}

// ---------------------------------------------------------------- boot

static void drawBoot(Canvas& g, const UiState& s) {
  g.background();
  frame(g);
  g.textCenter(160, 40, s.title, C_BRIGHT, 3);
  g.fillRect(60, 68, 200, 2, C_MID);
  int done = 0;
  for (int i = 0; i < s.bootCount; i++) {
    const BootLine& b = s.boot[i];
    int y = 86 + i * 15;
    char row[48];
    snprintf(row, sizeof row, "> %s ", b.label);
    int n = (int)strlen(row);
    while (n < 30) row[n++] = '.';
    row[n] = 0;
    g.text(44, y, row, C_SOFT, 1);
    const char* st = b.status == 1 ? "OK" : b.status == 2 ? "FAIL" : b.status == 3 ? "--" : nullptr;
    if (st) {
      g.textRight(276, y, st, b.status == 2 ? C_WARN : b.status == 1 ? C_BRIGHT : C_SOFT, 1);
      done++;
    } else if ((s.ms / 250) & 1) {
      g.fillRect(272, y, 5, 7, C_BRIGHT);
    }
  }
  g.rect(44, 202, 232, 8, C_DIM);
  g.fillRect(46, 204, 228 * done / BOOT_LINES, 4, C_MID);
}

// ---------------------------------------------------------------- system

static void bar(Canvas& g, int x, int y, int w, float frac) {
  if (frac < 0) frac = 0;
  if (frac > 1) frac = 1;
  g.rect(x, y, w, 6, C_DIM);
  g.fillRect(x + 1, y + 1, (int)((w - 2) * frac), 4, C_MID);
}

static void drawSystem(Canvas& g, const UiState& s) {
  header(g, s, "SYSTEM", "STATUS");
  char v[48];
  int y = 54;
  auto row = [&](const char* label, const char* value, uint16_t c) {
    g.text(20, y, label, C_SOFT, 1);
    g.text(88, y, value, c, 1);
    y += 14;
  };
  row("CHIP", s.chip, C_BRIGHT);
  snprintf(v, sizeof v, "%d CORES @ %d MHZ", s.cores, s.cpuMhz);           row("CPU", v, C_BRIGHT);
  snprintf(v, sizeof v, "%u MB", (unsigned)s.flashMB);                     row("FLASH", v, C_BRIGHT);
  snprintf(v, sizeof v, "%.1f / %.1f MB FREE", s.psramFree / 1048576.0f, s.psramTotal / 1048576.0f);
  bar(g, 236, y, 66, s.psramTotal ? 1.0f - (float)s.psramFree / s.psramTotal : 0);
  row("PSRAM", v, C_BRIGHT);
  snprintf(v, sizeof v, "%u / %u KB FREE", (unsigned)(s.heapFree / 1024), (unsigned)(s.heapTotal / 1024));
  bar(g, 236, y, 66, s.heapTotal ? 1.0f - (float)s.heapFree / s.heapTotal : 0);
  row("HEAP", v, C_BRIGHT);
  unsigned u = s.uptimeS;
  if (u >= 86400) snprintf(v, sizeof v, "%uD %02u:%02u:%02u", u / 86400, u / 3600 % 24, u / 60 % 60, u % 60);
  else snprintf(v, sizeof v, "%02u:%02u:%02u", u / 3600, u / 60 % 60, u % 60);
  row("UPTIME", v, C_BRIGHT);
  snprintf(v, sizeof v, "%.2f V  %d%%", s.battV, s.battPct);               row("BATTERY", v, s.battPct <= 15 ? C_WARN : C_BRIGHT);
  row("MAC", s.mac, C_BRIGHT);
  if (s.radioOk) snprintf(v, sizeof v, "SX1262 ONLINE  %.1f MHZ", s.freqMhz);
  row("RADIO", s.radioOk ? v : "OFFLINE", s.radioOk ? C_BRIGHT : C_WARN);
  row("SD CARD", s.sdOk ? "MOUNTED" : "NOT FOUND", s.sdOk ? C_BRIGHT : C_WARN);
  row("KEYBOARD", s.kbOk ? "ONLINE" : "NOT FOUND", s.kbOk ? C_BRIGHT : C_WARN);
  footer(g, "< BACK");
}

// ---------------------------------------------------------------- scan

static void radar(Canvas& g, const UiState& s) {
  const int cx = 160, cy = 122, R = 54;
  g.circle(cx, cy, R, C_DIM);
  g.circle(cx, cy, R * 2 / 3, C_FAINT);
  g.circle(cx, cy, R / 3, C_FAINT);
  g.hline(cx - R, cy, 2 * R + 1, C_FAINT);
  g.vline(cx, cy - R, 2 * R + 1, C_FAINT);
  float a = s.ms * 0.0035f;
  for (int k = 12; k >= 0; k--) {
    float aa = a - k * 0.055f;
    uint16_t c = k == 0 ? C_BRIGHT : k < 4 ? C_MID : k < 8 ? C_SOFT : C_DIM;
    g.line(cx, cy, cx + (int)(cosf(aa) * R), cy + (int)(sinf(aa) * R), c);
  }
  for (int i = 0; i < 5; i++) {   // fading contacts behind the sweep
    uint32_t h = hash32(i * 31337u + 5);
    float ba = (h % 628) / 100.0f, br = 12 + (h >> 10) % (R - 16);
    float d = fmodf(a - ba + 62.8f, 6.2832f);
    if (d < 2.2f) {
      int bx = cx + (int)(cosf(ba) * br), by = cy + (int)(sinf(ba) * br);
      g.fillRect(bx - 1, by - 1, 3, 3, d < 0.8f ? C_BRIGHT : C_SOFT);
    }
  }
  char t[24];
  snprintf(t, sizeof t, "SCANNING%s", &"..."[2 - (s.ms / 350) % 3]);
  g.text(160 - Canvas::textW("SCANNING...", 1) / 2, 186, t, C_SOFT, 1);
}

static void drawScan(Canvas& g, const UiState& s) {
  char info[24];
  if (s.scanning) snprintf(info, sizeof info, "SCANNING");
  else snprintf(info, sizeof info, "%d NETWORKS", s.netCount);
  header(g, s, "SCAN", info);

  if (s.netCount == 0) {
    if (s.scanning) radar(g, s);
    else g.textCenter(160, 120, "NO NETWORKS FOUND", C_SOFT, 1);
    footer(g, "CLICK RESCAN   < BACK");
    return;
  }

  const int y0 = 54, rowH = 15, rows = SCAN_ROWS;
  for (int r = 0; r < rows; r++) {
    int i = s.netScroll + r;
    if (i >= s.netCount) break;
    const NetInfo& n = s.nets[i];
    int y = y0 + r * rowH;
    bool sel = i == s.netSel;
    if (sel) g.fillRect(14, y - 4, 282, 15, C_HILITE);
    uint16_t c = sel ? C_INK : C_BRIGHT, soft = sel ? C_INK : C_SOFT;
    g.text(20, y, n.ssid[0] ? n.ssid : "<HIDDEN>", c, 1, 26);
    char b[12];
    snprintf(b, sizeof b, "CH%d", n.channel);
    g.text(194, y, b, soft, 1);
    snprintf(b, sizeof b, "%d", n.rssi);
    g.textRight(250, y, b, soft, 1);
    int lvl = n.rssi >= -55 ? 4 : n.rssi >= -67 ? 3 : n.rssi >= -75 ? 2 : n.rssi >= -85 ? 1 : 0;
    for (int k = 0; k < 4; k++) {
      int h = 2 + k * 2;
      g.fillRect(258 + k * 4, y + 7 - h, 3, h, k < lvl ? c : (sel ? RGB(0x5BA4CC) : C_DIM));
    }
    if (n.secure) {
      g.rect(281, y, 4, 4, c);
      g.fillRect(280, y + 3, 6, 4, c);
    }
  }
  scrollbar(g, 300, y0 - 4, rows * rowH, s.netScroll, rows, s.netCount);
  footer(g, "^v SELECT   CLICK RESCAN   < BACK");
}

// ---------------------------------------------------------------- comms

static void drawComms(Canvas& g, const UiState& s) {
  char info[24];
  if (s.radioOk) snprintf(info, sizeof info, "LORA %.1f MHZ", s.freqMhz);
  header(g, s, "COMMS", s.radioOk ? info : "RADIO OFFLINE");

  const int y0 = 54, rowH = 12, rows = 11;
  if (s.chatCount == 0) {
    g.textCenter(160, 100, "NO MESSAGES YET", C_SOFT, 1);
    g.textCenter(160, 114, "DECKS ON THE SAME FREQUENCY", C_DIM, 1);
    g.textCenter(160, 126, "SHOW UP HERE", C_DIM, 1);
  }
  int first = s.chatCount > rows ? s.chatCount - rows : 0;
  for (int i = first; i < s.chatCount; i++) {
    const ChatLine& l = s.chat[i];
    int y = y0 + (i - first) * rowH;
    uint16_t c = l.kind == 0 ? C_SOFT : l.kind == 1 ? C_MID : C_BRIGHT;
    g.text(20, y, l.text, c, 1);
    if (l.kind == 2 && l.rssi) {
      char r[8];
      snprintf(r, sizeof r, "%d", l.rssi);
      g.textRight(304, y, r, C_DIM, 1);
    }
  }

  g.rect(14, 190, 292, 19, C_MID);
  g.text(20, 196, ">", C_BRIGHT, 1);
  int len = (int)strlen(s.input);
  const char* shown = len > 43 ? s.input + (len - 43) : s.input;
  int x = 30;
  if (len == 0) g.text(30, 196, "TYPE A MESSAGE", C_DIM, 1);
  else x = g.text(30, 196, shown, C_BRIGHT, 1);
  if ((s.ms / 500) & 1) g.fillRect(len ? x : 30, 195, 5, 9, C_BRIGHT);

  char f[48];
  snprintf(f, sizeof f, "ENTER SEND   < BACK   AS %s", s.callsign);
  footer(g, f);
}

// ---------------------------------------------------------------- tools

static void drawTools(Canvas& g, const UiState& s) {
  header(g, s, "TOOLS", "DISPLAY");
  const char* names[3] = {"BRIGHTNESS", "SCANLINES", "GLOW"};
  for (int i = 0; i < 3; i++) {
    int y = 62 + i * 32;
    bool sel = i == s.toolSel;
    if (sel) g.fillRect(14, y - 5, 292, 24, C_HILITE);
    uint16_t c = sel ? C_INK : C_BRIGHT;
    g.text(20, y, names[i], c, 2);
    if (i == 0) {
      for (int k = 0; k < 16; k++)
        g.fillRect(190 + k * 7, y, 5, 14, k < s.brightness ? c : (sel ? RGB(0x5BA4CC) : C_DIM));
    } else {
      bool on = i == 1 ? s.scanlines : s.glow;
      g.textRight(302, y, on ? "ON" : "OFF", c, 2);
    }
  }
  char v[48];
  g.hline(16, 160, 288, C_DIM);
  snprintf(v, sizeof v, "CALLSIGN   %s", s.callsign);
  g.text(20, 170, v, C_SOFT, 1);
  snprintf(v, sizeof v, "RADIO      %.1f MHZ", s.freqMhz);
  g.text(20, 184, v, C_SOFT, 1);
  g.text(20, 198, "FIRMWARE   CYBERDECK 1.0 / T-DECK", C_SOFT, 1);
  footer(g, "^v SELECT   CLICK CHANGE   < BACK");
}

// ---------------------------------------------------------------- files

static void drawFiles(Canvas& g, const UiState& s) {
  char info[32];
  if (s.sdOk) snprintf(info, sizeof info, "%.1f / %.1f GB", s.sdUsedGB, s.sdTotalGB);
  header(g, s, "FILES", s.sdOk ? info : "SD CARD");

  if (!s.sdOk) {
    g.textCenter(160, 104, "NO SD CARD", C_WARN, 2);
    g.textCenter(160, 132, "INSERT A FAT32 MICROSD", C_SOFT, 1);
    g.textCenter(160, 146, "THEN CLICK TO RETRY", C_SOFT, 1);
    footer(g, "CLICK RETRY   < BACK");
    return;
  }

  int plen = (int)strlen(s.path);
  g.text(20, 52, plen > 46 ? s.path + plen - 46 : s.path, C_SOFT, 1);

  const int y0 = 68, rowH = 14, rows = FILE_ROWS;
  if (s.fileCount == 0) g.text(34, y0, "(EMPTY)", C_DIM, 1);
  for (int r = 0; r < rows; r++) {
    int i = s.fileScroll + r;
    if (i >= s.fileCount) break;
    const FileEntry& f = s.files[i];
    int y = y0 + r * rowH;
    bool sel = i == s.fileSel;
    if (sel) g.fillRect(14, y - 4, 282, 14, C_HILITE);
    uint16_t c = sel ? C_INK : C_BRIGHT, soft = sel ? C_INK : C_SOFT;
    if (f.dir) { g.fillRect(20, y + 1, 8, 6, c); g.fillRect(20, y, 3, 1, c); }
    else { g.rect(21, y, 6, 7, c); g.hline(23, y + 2, 2, c); g.hline(23, y + 4, 2, c); }
    g.text(34, y, f.name, c, 1, 34);
    char sz[16];
    if (f.dir) snprintf(sz, sizeof sz, "DIR");
    else if (f.size < 1024) snprintf(sz, sizeof sz, "%uB", (unsigned)f.size);
    else if (f.size < 1048576) snprintf(sz, sizeof sz, "%.1fK", f.size / 1024.0f);
    else snprintf(sz, sizeof sz, "%.1fM", f.size / 1048576.0f);
    g.textRight(292, y, sz, soft, 1);
  }
  scrollbar(g, 300, y0 - 4, rows * rowH, s.fileScroll, rows, s.fileCount);
  footer(g, "^v SELECT   CLICK OPEN   < UP");
}

// ----------------------------------------------------------------

void drawScreen(Canvas& g, const UiState& s) {
  switch (s.screen) {
    case SCR_BOOT:   drawBoot(g, s); break;
    case SCR_HOME:   drawHome(g, s); break;
    case SCR_SYSTEM: drawSystem(g, s); break;
    case SCR_SCAN:   drawScan(g, s); break;
    case SCR_COMMS:  drawComms(g, s); break;
    case SCR_TOOLS:  drawTools(g, s); break;
    case SCR_FILES:  drawFiles(g, s); break;
  }
}
