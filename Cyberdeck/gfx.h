// Tiny software renderer: everything is drawn into a 320x240 RGB565 framebuffer,
// then composeRow() adds the glow + scanline look and converts each row for the panel.
// No Arduino dependencies, so the same code renders the desktop preview.
#pragma once
#include <stdint.h>

#define SCREEN_W 320
#define SCREEN_H 240

constexpr uint16_t RGB(uint32_t hex) {
  return (uint16_t)((((hex >> 16) & 0xF8) << 8) | (((hex >> 8) & 0xFC) << 3) | ((hex & 0xFF) >> 3));
}

// Palette
constexpr uint16_t C_BG_TOP   = RGB(0x0B1C3C);
constexpr uint16_t C_BG_BOT   = RGB(0x050E22);
constexpr uint16_t C_BRIGHT   = RGB(0xB4ECFF);  // main text
constexpr uint16_t C_MID      = RGB(0x5FB0E6);  // lines, frame
constexpr uint16_t C_SOFT     = RGB(0x3D76A8);  // secondary text (does not glow)
constexpr uint16_t C_DIM      = RGB(0x24507E);
constexpr uint16_t C_FAINT    = RGB(0x15345C);
constexpr uint16_t C_BUILDING = RGB(0x10305A);
constexpr uint16_t C_WIN_OFF  = RGB(0x1B4472);
constexpr uint16_t C_WIN_ON   = RGB(0xD8F4FF);
constexpr uint16_t C_HILITE   = RGB(0x9EE0FF);  // selection bar
constexpr uint16_t C_INK      = RGB(0x020A1C);  // text on selection bar (never glows)
constexpr uint16_t C_WARN     = RGB(0xFFB347);
constexpr uint16_t C_ALERT    = RGB(0xFF5A5A);

struct Canvas {
  uint16_t* fb = nullptr;

  inline void px(int x, int y, uint16_t c) {
    if ((unsigned)x < SCREEN_W && (unsigned)y < SCREEN_H) fb[y * SCREEN_W + x] = c;
  }
  uint16_t bgAt(int y) const;                 // background gradient colour for row y
  void background();                          // fill with gradient
  void fillRect(int x, int y, int w, int h, uint16_t c);
  void hline(int x, int y, int w, uint16_t c) { fillRect(x, y, w, 1, c); }
  void vline(int x, int y, int h, uint16_t c) { fillRect(x, y, 1, h, c); }
  void rect(int x, int y, int w, int h, uint16_t c);
  void roundRect(int x, int y, int w, int h, int r, uint16_t c);
  void fillCircle(int cx, int cy, int r, uint16_t c);
  void circle(int cx, int cy, int r, uint16_t c);
  void line(int x0, int y0, int x1, int y1, uint16_t c);
  void dither(int x, int y, int w, int h, uint16_t c);

  // 5x7 bitmap font. Advance is 6*scale. Returns x after the last glyph.
  int text(int x, int y, const char* s, uint16_t c, int scale = 1, int maxChars = -1);
  int textRight(int xRight, int y, const char* s, uint16_t c, int scale = 1);
  int textCenter(int xCenter, int y, const char* s, uint16_t c, int scale = 1);
  static int textW(const char* s, int scale = 1, int maxChars = -1);
};

// Post-process one row: phosphor glow around bright pixels, optional scanlines,
// then write big-endian RGB565 bytes (what the ST7789 expects) to out[SCREEN_W*2].
void composeRow(const uint16_t* fb, int y, uint8_t* out, bool glow, bool scanlines);

uint32_t hash32(uint32_t x);
