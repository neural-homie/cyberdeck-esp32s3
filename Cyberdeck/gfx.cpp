#include "gfx.h"
#include "font5x7.h"
#include <string.h>
#include <stdlib.h>

uint32_t hash32(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU;
  x ^= x >> 15; x *= 0x846ca68bU;
  x ^= x >> 16; return x;
}

static inline uint16_t mix565(uint16_t a, uint16_t b, int t /*0..256*/) {
  int ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31;
  int br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
  int r = ar + (((br - ar) * t) >> 8);
  int g = ag + (((bg - ag) * t) >> 8);
  int bl = ab + (((bb - ab) * t) >> 8);
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

uint16_t Canvas::bgAt(int y) const {
  if (y < 0) y = 0;
  if (y >= SCREEN_H) y = SCREEN_H - 1;
  return mix565(C_BG_TOP, C_BG_BOT, (y * 256) / SCREEN_H);
}

void Canvas::background() {
  for (int y = 0; y < SCREEN_H; y++) {
    uint16_t c = bgAt(y);
    uint16_t* row = fb + y * SCREEN_W;
    for (int x = 0; x < SCREEN_W; x++) row[x] = c;
  }
}

void Canvas::fillRect(int x, int y, int w, int h, uint16_t c) {
  if (w <= 0 || h <= 0) return;
  int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
  int x1 = x + w > SCREEN_W ? SCREEN_W : x + w;
  int y1 = y + h > SCREEN_H ? SCREEN_H : y + h;
  for (int yy = y0; yy < y1; yy++) {
    uint16_t* row = fb + yy * SCREEN_W;
    for (int xx = x0; xx < x1; xx++) row[xx] = c;
  }
}

void Canvas::rect(int x, int y, int w, int h, uint16_t c) {
  hline(x, y, w, c); hline(x, y + h - 1, w, c);
  vline(x, y, h, c); vline(x + w - 1, y, h, c);
}

void Canvas::roundRect(int x, int y, int w, int h, int r, uint16_t c) {
  hline(x + r, y, w - 2 * r, c); hline(x + r, y + h - 1, w - 2 * r, c);
  vline(x, y + r, h - 2 * r, c); vline(x + w - 1, y + r, h - 2 * r, c);
  // corner arcs (midpoint circle, one octant pair per corner)
  int f = 1 - r, dx = 1, dy = -2 * r, xx = 0, yy = r;
  int cx0 = x + r, cy0 = y + r, cx1 = x + w - 1 - r, cy1 = y + h - 1 - r;
  while (xx <= yy) {
    px(cx0 - xx, cy0 - yy, c); px(cx0 - yy, cy0 - xx, c);
    px(cx1 + xx, cy0 - yy, c); px(cx1 + yy, cy0 - xx, c);
    px(cx0 - xx, cy1 + yy, c); px(cx0 - yy, cy1 + xx, c);
    px(cx1 + xx, cy1 + yy, c); px(cx1 + yy, cy1 + xx, c);
    if (f >= 0) { yy--; dy += 2; f += dy; }
    xx++; dx += 2; f += dx;
  }
}

void Canvas::fillCircle(int cx, int cy, int r, uint16_t c) {
  for (int dy = -r; dy <= r; dy++) {
    int dx = 0;
    while ((dx + 1) * (dx + 1) + dy * dy <= r * r + r) dx++;
    hline(cx - dx, cy + dy, 2 * dx + 1, c);
  }
}

void Canvas::circle(int cx, int cy, int r, uint16_t c) {
  int f = 1 - r, dx = 1, dy = -2 * r, x = 0, y = r;
  while (x <= y) {
    px(cx + x, cy + y, c); px(cx - x, cy + y, c); px(cx + x, cy - y, c); px(cx - x, cy - y, c);
    px(cx + y, cy + x, c); px(cx - y, cy + x, c); px(cx + y, cy - x, c); px(cx - y, cy - x, c);
    if (f >= 0) { y--; dy += 2; f += dy; }
    x++; dx += 2; f += dx;
  }
}

void Canvas::line(int x0, int y0, int x1, int y1, uint16_t c) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    px(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void Canvas::dither(int x, int y, int w, int h, uint16_t c) {
  for (int yy = y; yy < y + h; yy++)
    for (int xx = x + ((yy + x) & 1); xx < x + w; xx += 2) px(xx, yy, c);
}

int Canvas::textW(const char* s, int scale, int maxChars) {
  int n = (int)strlen(s);
  if (maxChars >= 0 && n > maxChars) n = maxChars;
  return n ? n * 6 * scale - scale : 0;
}

int Canvas::text(int x, int y, const char* s, uint16_t c, int scale, int maxChars) {
  for (int i = 0; s[i] && (maxChars < 0 || i < maxChars); i++) {
    unsigned char ch = (unsigned char)s[i];
    if (ch < 32 || ch > 126) ch = '?';
    if (ch >= 'a' && ch <= 'z') ch -= 32;   // all caps: the 5x7 lowercase has no room for descenders
    const uint8_t* g = FONT5X7[ch - 32];
    for (int col = 0; col < 5; col++) {
      uint8_t bits = g[col];
      for (int row = 0; row < 7; row++)
        if (bits & (1 << row)) {
          if (scale == 1) px(x + col, y + row, c);
          else fillRect(x + col * scale, y + row * scale, scale, scale, c);
        }
    }
    x += 6 * scale;
  }
  return x;
}

int Canvas::textRight(int xRight, int y, const char* s, uint16_t c, int scale) {
  return text(xRight - textW(s, scale), y, s, c, scale);
}

int Canvas::textCenter(int xCenter, int y, const char* s, uint16_t c, int scale) {
  return text(xCenter - textW(s, scale) / 2, y, s, c, scale);
}

// --- post-processing -------------------------------------------------------

static inline bool lit(uint16_t c) { return ((c >> 5) & 63) >= 32 && c != C_INK; }
static inline int green(uint16_t c) { return (c >> 5) & 63; }

void composeRow(const uint16_t* fb, int y, uint8_t* out, bool glow, bool scanlines) {
  const uint16_t* row = fb + y * SCREEN_W;
  const uint16_t* up1 = y > 0 ? row - SCREEN_W : nullptr;
  const uint16_t* dn1 = y < SCREEN_H - 1 ? row + SCREEN_W : nullptr;
  const uint16_t* up2 = y > 1 ? row - 2 * SCREEN_W : nullptr;
  const uint16_t* dn2 = y < SCREEN_H - 2 ? row + 2 * SCREEN_W : nullptr;

  for (int x = 0; x < SCREEN_W; x++) {
    uint16_t c = row[x];
    bool isLit = lit(c);
    if (glow && !isLit && c != C_INK) {
      // Strongest lit neighbour wins: edge-adjacent, then diagonal, then two pixels away.
      uint16_t src = 0; int best = -1, weight = 0;
      #define TRY(p, w) { uint16_t n = (p); if (lit(n) && green(n) * (w) > best) { best = green(n) * (w); src = n; weight = (w); } }
      if (x > 0) TRY(row[x - 1], 100);
      if (x < SCREEN_W - 1) TRY(row[x + 1], 100);
      if (up1) TRY(up1[x], 100);
      if (dn1) TRY(dn1[x], 100);
      if (best < 0) {
        if (up1 && x > 0) TRY(up1[x - 1], 68);
        if (up1 && x < SCREEN_W - 1) TRY(up1[x + 1], 68);
        if (dn1 && x > 0) TRY(dn1[x - 1], 68);
        if (dn1 && x < SCREEN_W - 1) TRY(dn1[x + 1], 68);
      }
      if (best < 0) {
        if (x > 1) TRY(row[x - 2], 34);
        if (x < SCREEN_W - 2) TRY(row[x + 2], 34);
        if (up2) TRY(up2[x], 34);
        if (dn2) TRY(dn2[x], 34);
      }
      #undef TRY
      if (best >= 0) c = mix565(c, src, weight);  // 100 -> ~39%, 68 -> ~27%, 34 -> ~13%
    }
    // ~20% darker on odd rows. Only background is darkened, so even dim text stays sharp.
    if (scanlines && (y & 1) && green(row[x]) < 20) c = mix565(c, 0, 52);
    out[2 * x] = c >> 8;
    out[2 * x + 1] = c & 0xFF;
  }
}
