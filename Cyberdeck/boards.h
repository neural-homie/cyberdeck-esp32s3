// ---------------------------------------------------------------------------
//  Hardware targets. Pick one with DECK_BOARD (config.h, or `make build BOARD=...`).
//  Everything board-specific that is a constant lives here; the code in
//  Cyberdeck.ino switches on the HAS_* feature flags, not on board names.
// ---------------------------------------------------------------------------
#pragma once

#define BOARD_TDECK    1   // LILYGO T-Deck / T-Deck Plus
#define BOARD_ES3C28P  2   // LCDWiki 2.8" IPS ESP32-S3 display (ES3C28P touch / ES3N28P)

#if DECK_BOARD == BOARD_TDECK
// ---- LILYGO T-Deck (from LilyGO's utilities.h) ------------------------------
#define BOARD_NAME        "T-DECK"
#define HAS_KEYBOARD      1
#define HAS_TRACKBALL     1
#define HAS_TOUCH         0
#define HAS_LORA          1
#define SD_OVER_SPI       1   // SD shares the SPI bus with the display and radio
#define TFT_IS_ILI9341    0   // ST7789
#define BACKLIGHT_PWM     0   // pulse-count driver

#define PIN_POWER_ON    10   // must be HIGH to power keyboard, radio, display
#define PIN_I2C_SDA     18
#define PIN_I2C_SCL      8
#define PIN_BAT_ADC      4
#define PIN_SPI_SCK     40
#define PIN_SPI_MOSI    41
#define PIN_SPI_MISO    38
#define PIN_TFT_CS      12
#define PIN_TFT_DC      11
#define PIN_TFT_BL      42   // pulse-count backlight driver, 16 levels
#define PIN_SD_CS       39
#define PIN_RADIO_CS     9
#define PIN_RADIO_BUSY  13
#define PIN_RADIO_RST   17
#define PIN_RADIO_DIO1  45
#define PIN_BALL_RIGHT   2
#define PIN_BALL_UP      3
#define PIN_BALL_LEFT    1
#define PIN_BALL_DOWN   15
#define PIN_BUTTON       0   // trackball click (same pin as BOOT)
#define BUTTON_EVENT     EV_CLICK
#define KEYBOARD_ADDR 0x55
#define TFT_MADCTL     0x60  // ST7789 landscape

#elif DECK_BOARD == BOARD_ES3C28P
// ---- LCDWiki ES3C28P (from the ES3C28P/ES3N28P user manual schematic) -------
// ESP32-S3R8 (8 MB octal PSRAM), 16 MB flash, ILI9341V 240x320 IPS on SPI,
// FT6336G capacitive touch (I2C 0x38), ES8311 codec (I2C 0x18), SDIO microSD,
// WS2812-style RGB LED, BOOT key. No keyboard, no trackball, no LoRa radio.
#define BOARD_NAME        "ES3C28P"
#define HAS_KEYBOARD      0
#define HAS_TRACKBALL     0
#define HAS_TOUCH         1
#define HAS_LORA          0
#define SD_OVER_SPI       0   // 4-bit SDIO on its own pins
#define TFT_IS_ILI9341    1
#define BACKLIGHT_PWM     1

#define PIN_I2C_SDA     16   // touch + audio codec + the 4-pin I2C socket
#define PIN_I2C_SCL     15
#define PIN_TOUCH_INT   17
#define PIN_TOUCH_RST   18
#define TOUCH_ADDR    0x38
#define PIN_BAT_ADC      9   // 200k/200k divider, so x2
#define PIN_SPI_SCK     12   // LCD only; these are the S3's IO_MUX FSPI pins
#define PIN_SPI_MOSI    11
#define PIN_SPI_MISO    13
#define PIN_TFT_CS      10
#define PIN_TFT_DC      46
#define PIN_TFT_BL      45   // active high through a MOSFET, PWM dimmable
#define PIN_SD_CLK      38
#define PIN_SD_CMD      40
#define PIN_SD_D0       39
#define PIN_SD_D1       41
#define PIN_SD_D2       48
#define PIN_SD_D3       47
#define PIN_LED_RGB     42
#define PIN_AUDIO_EN     1   // FM8002E amp shutdown, low = on. Left alone (pulled high = off)
#define PIN_BUTTON       0   // BOOT key, free after boot
#define BUTTON_EVENT     EV_LEFT
// ILI9341V landscape. 0x68 = MX|MV|BGR (LCDWiki direction 1); 0xA8 = the other way up.
#define TFT_MADCTL     0x68
// Touch panel reports portrait 240x320. Map to the 320x240 landscape screen.
#define TOUCH_SWAP_XY    1
#define TOUCH_FLIP_X     0
#define TOUCH_FLIP_Y     1

#else
#error "Unknown DECK_BOARD. Use BOARD_TDECK or BOARD_ES3C28P (see boards.h)."
#endif
