# Cyberdeck firmware: ESP32-S3 handhelds (LCDWiki 2.8" ES3C28P, LILYGO T-Deck)

A retro terminal "cyberdeck" UI for two ESP32-S3 boards, picked at build time:

- **ES3C28P** (default): LCDWiki "2.8inch ESP32-S3 Display", ILI9341V 320×240 IPS, FT6336G capacitive touch,
  microSD over 4-bit SDIO, BOOT key, RGB LED, ES8311 audio codec. **No keyboard, no trackball, no LoRa.**
- **T-Deck**: LILYGO T-Deck / T-Deck Plus, ST7789 320×240, BlackBerry-style I2C keyboard, trackball, SX1262 LoRa, microSD on SPI.

The UI is cyan-on-navy, styled after `docs/reference-ui.jpg`. It has a glowing phosphor look, CRT scanlines, an animated
pixel skyline on the home screen, and five apps: SYSTEM, SCAN, COMMS, TOOLS, FILES.

## Status (read this first)

- **ES3C28P runs on real hardware** (first bring-up 2026-11-23). Verified over serial on the device:
  - The panel reads back ID `00 93 41`, power mode `0x9C` and MADCTL `0x68`.
  - The I2C bus shows 0x18 (ES8311) and 0x38 (touch). The FT6336G chip ID is `0x64`.
  - Touch calibration fitted with a 6.2 px worst error, and menu, back-title and list taps then hit first time.
  - Wi-Fi scan found 9 networks. The battery ADC reads 4.10 V.
- **Not yet verified on the ES3C28P:**
  - SD card: no card was inserted at bring-up, and `0x107` is a timeout.
  - BOOT key, both short press and the 2 s hold.
  - The user hasn't explicitly confirmed screen orientation or colours, though they navigated the UI by touch without problems.
  - Whether a battery was attached when it read 4.10 V. On USB power the TP4054 output can read like that.
- **T-Deck: compiles but has never run on a T-Deck.** Its pin map, display init, backlight driver, trackball and keyboard code
  come from LilyGO's examples (github.com/Xinyuan-LilyGO/T-Deck). Use the T-Deck checklist below for the first flash.
- Both builds compile with arduino-cli, esp32 core **3.3.12** and RadioLib **7.8.1**. Each is ~32% of the 3 MB app partition and 17% of RAM,
  with zero warnings from project files.
- **Measured frame time on the ES3C28P:** draw ~7 ms, push ~92 ms (home screen with glow). `composeRow` + 40 MHz SPI dominate,
  so the home animation runs at ~10 fps.

## Commands

```
make preview                          # render every screen for both boards to build/preview/*.png (2x) + contact sheets. No hardware needed.
make setup                            # one-time: install esp32 core + RadioLib into arduino-cli
make build [BOARD=es3c28p|tdeck]      # compile into build/fw-<board> (default es3c28p)
make build-all                        # compile both
make flash PORT=<port> [BOARD=...]    # upload (arduino-cli board list shows the port; macOS: /dev/cu.usbmodem1101)
make monitor PORT=<port>              # serial at 115200 (bring-up log: [boot] [tft] [i2c] [touch] [cal] [sd] [in] [stat])
make dist [BOARD=...]                 # build + write dist/cyberdeck-<board>-0x0.bin (single image for the esptool-js web flasher)
```

`BOARD` sets `-DDECK_BOARD` via `compiler.cpp.extra_flags`. The Arduino IDE uses the default in `config.h` instead.
FQBN (both boards): `esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB`.
The Arduino IDE equivalent is ESP32S3 Dev Module, USB CDC On Boot Enabled, 16MB flash, OPI PSRAM, 16M Flash 3MB APP/9.9MB FATFS.

**After any UI change, run `make preview` and look at the affected PNGs before calling it done. After any change, run
`make build-all`, so the board you aren't using doesn't break.**

Preview outputs are `<scene>.png` plus `all.png` for the T-Deck look, and `es3c28p_<scene>.png` plus `all_es3c28p.png` for the
touch board (touch hints, "<" back, footer buttons, COMMS greyed out).

## Layout

```
Cyberdeck/            Arduino sketch (folder name must match Cyberdeck.ino)
  Cyberdeck.ino       ALL hardware: display drivers, backlight, battery, Wi-Fi scan, LoRa, SD, trackball, keyboard,
                      touch task + calibration, navigation/input (handle()), boot sequence, setup()/loop()
  boards.h            per-board pins and HAS_* feature flags (the only place board differences are spelled out)
  ui.h / ui.cpp       UiState (everything a screen needs) + one draw function per screen + skyline art + uiHitTest()
  gfx.h / gfx.cpp     Canvas (software renderer into a 320x240 RGB565 framebuffer), palette, 5x7 text,
                      composeRow() post-process (glow + scanlines + byte swap for the panel)
  font5x7.h           Adafruit GFX glcdfont, ASCII 32-126 (BSD)
  config.h            user settings: board, title, callsign, LoRa params, display defaults, trackball step
tools/preview/        desktop renderer: same ui.cpp/gfx.cpp + sample data in SCENES[], rendered once per board PROFILES[]
tools/trim_bin.py     trims the 16 MB merged image for web flashing
dist/                 prebuilt cyberdeck-<board>-0x0.bin images
docs/reference-ui.jpg the look we're matching
backup/               (git-ignored) original firmware dumps; see backup/RESTORE.txt
```

## Architecture rules

1. **`ui.*` and `gfx.*` must stay hardware-free.** Use only the standard C/C++ library, with no `Arduino.h`, `String`, `millis()`,
   `Serial`, or `DECK_BOARD`/`HAS_*` macros. That is what lets `tools/preview` compile on a desktop. Board differences reach the UI
   only through `UiState` fields (`touchUi`, `hasRadio`, `boardName`, `bootTotal`), and the .ino sets them from `HAS_*` in `setup()`.
2. **Board code switches on feature flags, not board names.** Use `HAS_TOUCH`, `HAS_LORA`, `HAS_KEYBOARD`, `HAS_TRACKBALL`,
   `SD_OVER_SPI`, `TFT_IS_ILI9341` and `BACKLIGHT_PWM`. To add a board, add a block to `boards.h`, a `BOARD_ID_*` line in the
   Makefile, and a profile in `tools/preview`.
   Don't name a macro after something in the core's `variants/esp32s3/pins_arduino.h`. `PIN_RGB_LED` was one, and it silently
   enabled LED code on the T-Deck.
3. **Rendering pipeline:** `drawScreen(canvas, S)` draws the whole frame into `fb` (in PSRAM), and then `pushFrame()` runs
   `composeRow()` per row and streams it over SPI. Every frame is a full redraw. There are no partial updates and no TFT_eSPI or LVGL.
4. **Frame pacing** happens in `loop()`: it redraws when `dirty` is set or when the per-screen interval passes (home 90 ms, scan radar 50 ms,
   comms 250 ms, touch calibration 150 ms, everything else 1 s). Set `dirty = true` after state changes.
5. **Animation** reads `S.ms` (millis at draw time), and randomness comes from `hash32()` seeded by position/index, so frames are deterministic
   and the preview can pin a moment with `s.ms`.
6. **Input** goes through one function: `handle(Ev, key)` with EV_UP/DOWN/LEFT/RIGHT/CLICK/KEY. Enter maps to CLICK, and Backspace or
   trackball-left maps to back. On COMMS, printable keys go to the text box.
   - **Touch** is read by `inputTask` pinned to **core 0**. A frame takes ~100 ms on the loop core, so polling there drops quick taps.
     The task turns raw points into gestures (TE_TAP, TE_STEP for vertical drags, TE_BACK for a right swipe, TE_BUTTON/TE_CAL for the
     BOOT key) and queues them. `pollTouch()` in `loop()` maps taps through `uiHitTest()` and then calls `handle()`.
   - **`uiHitTest()` lives in ui.cpp** next to the layout it mirrors. If you move a row or button in a draw function, update its
     hit band there too. `footerButton()` is shared by drawing and hit-testing.
   - **Touch mapping** is an affine fit (`tcal[6]`) from the 4-point calibration screen, stored in Preferences key `tcal`.
     It runs on first boot when none is stored, or whenever BOOT is held for 2 s. The `TOUCH_SWAP_XY/FLIP_*` values in `boards.h` are
     only the fallback before calibration.
7. **Arduino auto-prototypes:** types used in function signatures in the .ino (like `Ev`, `TouchEv`) must be declared near the top
   of the file, before the first function. Calls to functions defined further down need a forward declaration
   (see `handle`, `openScreen`).
8. Keep `ui.h` list-size constants (`SCAN_ROWS`, `FILE_ROWS`, `CHAT_COLS`, `MAX_*`, `CAL_POINTS`) as the single source of truth shared by
   the UI and the .ino.

### Adding a screen

1. Add it to `enum Screen` in `ui.h`, plus any state fields in `UiState`. Menu screens go between `SCR_SYSTEM` and `SCR_FILES` in menu
   order. Off-menu screens (like `SCR_TOUCHCAL`) go after `SCR_FILES`.
2. Write `drawX()` in `ui.cpp` using `header()`, content from y=54, and `footer(g, s, hint, touchHint)`, then add it to `drawScreen()`.
   Add its touch targets to `uiHitTest()`.
3. Wire it up in `Cyberdeck.ino`: `openScreen()` (enter/exit side effects), `handle()` (input), `onTap()` (what HIT_ITEM does),
   and `loop()` (frame interval, if animated). To put it on the home menu, extend `MENU_LABELS`/`MENU_COUNT`. Home uses
   `SCR_SYSTEM + menuSel`, so menu order must match the enum order. With 6+ items the menu needs re-spacing, because each item is
   30 px tall starting at y=54.
4. Add one or more scenes to `SCENES[]` in `tools/preview/preview.cpp`, then run `make preview` and look at both boards' versions.

## UI style guide

- Screen is 320×240 landscape. The outer frame is `roundRect(4,4,312,232,r=9)`.
- Sub-screens use `header()`, which draws a breadcrumb `TITLE /` at y=13 (scale 1), the name at y=24 (scale 2), a 2-px rule at y=43,
  the battery at top right, and an info string at right y=30. Content starts at y=54, and the footer hint is at y=220.
- **Touch mode** (`s.touchUi`) changes three things:
  - The header gets a `<` before the name, and the whole band above y=46 is the back button.
  - Footers show the `touchHint` text.
  - `footerButton()` can put one boxed button at the right end of the footer (RESCAN on SCAN, RETRY on FILES with no card).
  Keep touch hints short so they don't run into the button.
- Text uses a 5×7 bitmap font scaled 1/2/3, with an advance of 6 px × scale. **`Canvas::text()` uppercases everything** on purpose:
  lowercase in the 5×7 font has no room for descenders and looked broken.
- Selection is a `C_HILITE` filled bar with `C_INK` text. Unavailable items (COMMS without a radio) use `C_DIM`.
- **Glow rule:** in `composeRow()`, any pixel whose 6-bit green channel is ≥32 glows (except `C_INK`). Of the palette, these glow:
  BRIGHT, MID, WIN_ON, HILITE, WARN. These don't: SOFT, DIM, FAINT, BUILDING, WIN_OFF, ALERT, INK.
  Use `C_SOFT` for secondary text so it stays crisp and doesn't bloom. A new colour's glow follows from its green value.
- **Scanlines:** odd rows are darkened ~20%, but only where the source pixel's 6-bit green is <20 (backgrounds), so text stays sharp.
- Use `C_WARN` (amber) for warnings and low battery (≤15%). `C_ALERT` (red) is used sparingly (beacon, unread dot).

## Hardware facts

### ES3C28P (from the LCDWiki ES3C28P/ES3N28P user manual schematic; verified items marked ✓)

| Function | GPIO | Notes |
|---|---|---|
| LCD SPI SCK / MOSI / MISO | 12 / 11 / 13 | ✓ Read-back works. These are the S3's IO_MUX FSPI pins. The LCD is the only SPI device |
| LCD CS / DC | 10 / 46 | ✓ |
| LCD reset | — | Tied to the ESP32's EN, so `tftInit()` sends a software reset (0x01) first |
| Backlight | 45 | Active high through a MOSFET, LEDC PWM 20 kHz. GPIO45 is a strapping pin, which is fine after boot |
| Touch I2C SDA / SCL | 16 / 15 | ✓ 400 kHz. Shared with the ES8311 codec (0x18) and the 4-pin I2C socket |
| Touch INT / RST | 17 / 18 | FT6336G at **0x38** ✓. Needs ~300 ms after reset. Polled, INT unused |
| SD (SDIO 4-bit) CLK / CMD / D0 / D1 / D2 / D3 | 38 / 40 / 39 / 41 / 48 / 47 | Untested (no card at bring-up) |
| Battery ADC | 9 | ✓ 200k/200k divider, ×2 |
| BOOT key | 0 | Pull-up, low when pressed. Short = back, hold 2 s = touch calibration (untested) |
| RGB LED | 42 | WS2812-style. Turned off at boot, because it keeps its colour across resets |
| Audio | MCLK 4, BCLK 5, WS 7, DOUT 8, DIN 6; amp enable 1 (low = on) | Unused. The amp is left off |
| UART0 RX / TX | 44 / 43 | 4-pin socket |
| Free GPIOs | 2, 3, 14, 21 | "Expand pin" socket |

The chip is an ESP32-S3R8 (8 MB octal PSRAM) with 16 MB quad flash. The ILI9341V init is LCDWiki's `ILI9341V_Init.txt`, with MADCTL `0x68`
(MX|MV|BGR) for landscape and inversion on.

### T-Deck (from LilyGO's utilities.h, unverified)

| Function | GPIO | Notes |
|---|---|---|
| Peripheral power | 10 | **Must be HIGH** before the keyboard, radio, and display work |
| I2C SDA / SCL | 18 / 8 | Keyboard ESP32-C3 at **0x55**. One read returns one char, and 0 means no key. Needs ~500 ms after power-on |
| SPI SCK / MOSI / MISO | 40 / 41 / 38 | **Shared** by TFT, SD, and LoRa. All CS lines are driven HIGH before `SPI.begin` |
| TFT CS / DC / backlight | 12 / 11 / 42 | ST7789, 40 MHz. Backlight is a 16-level pulse-count driver (`setBacklight`, 1..16) |
| SD CS | 39 | 4 MHz |
| LoRa CS / BUSY / RST / DIO1 | 9 / 13 / 17 / 45 | SX1262. RadioLib 7.8 handles the DIO2 RF switch itself |
| Trackball right / up / left / down | 2 / 3 / 1 / 15 | Each roll tick toggles the pin, and the ISR counts CHANGE edges |
| Trackball click | 0 | Same pin as BOOT. Hold it while pressing reset to enter download mode |
| Battery ADC | 4 | Read ×2 for the 1:2 divider |
| GPS RX / TX (Plus only) | 44 / 43 | Unused so far |
| I2S speaker | WS 5, BCK 7, DOUT 6 | Unused |
| ES7210 mic | MCLK 48, LRCK 21, SCK 47, DIN 14 | Unused |
| Touch interrupt | 16 | Touch panel unused (GT911 on I2C) |

The ESP32-S3 has BLE only, with no Bluetooth Classic.

## Flashing notes

- **Two different ESP32-S3 boards can show up as the same `/dev/cu.usbmodem1101`**, and `arduino-cli board list` can't tell them
  apart. Before flashing, run `esptool flash-id` and check the MAC against the board you mean to flash. Keep board MACs in
  local notes, not in this public repo.
- **Back up a device before its first flash:**
  ```
  ~/Library/Arduino15/packages/esp32/tools/esptool_py/5.3.1/esptool --port <port> --no-stub read-flash 0 0x400000 backup/<name>.bin
  ```
  On these boards the stub's `read-flash` dies on some sectors ("Serial data stream stopped"). `--no-stub` works at ~75 KB/s.
- esptool 5 skips regions whose contents already match and reports them as "verified". Fewer "Hash of data verified" lines than
  images is normal on a re-flash.
- The USB port is the S3's USB-Serial/JTAG, so esptool resets into download mode by itself. If it won't connect, hold BOOT, tap reset,
  then release BOOT.

## First-boot checklist

### ES3C28P

| Symptom | Likely fix |
|---|---|
| Upside down | `TFT_MADCTL` in `boards.h`: `0x68` → `0xA8`, then hold BOOT 2 s to recalibrate touch |
| Red/blue swapped | Clear the BGR bit in `TFT_MADCTL` (`0x68` → `0x60`) |
| Light background / inverted colours | Swap `0x21` for `0x20` in the ILI9341 `TFT_INIT` |
| Taps land in the wrong place | Hold BOOT 2 s and tap the four crosshairs. The `[cal]` log shows the fit and its worst error |
| `[touch] NOT FOUND` | ES3N28P (no-touch variant), or the touch FPC is loose. The BOOT key still works as back |
| `[sd] no card / mount failed` with `0x107` | No card or it isn't seated. Card must be FAT32. For a flaky card, try 1-bit mode (`SD_MMC.begin("/sdcard", true)`) |
| `[tft]` read-back values wrong | SPI wiring or pins in `boards.h` |

### T-Deck (hardware is unverified)

| Symptom | Likely fix |
|---|---|
| Black screen | Check that GPIO10 is HIGH early in `setup()`. Try `setBacklight(16)`. Confirm the init sequence in `TFT_INIT` |
| Colours inverted (light background) | In the ST7789 `TFT_INIT`, swap `0x21` (inversion on) for `0x20` |
| Red/blue swapped (orange where cyan should be) | `TFT_MADCTL` in `boards.h`: set the BGR bit, `0x60 → 0x68` |
| Upside-down or mirrored | `TFT_MADCTL` `0x60 → 0xA0` (180°). Other values: 0x00/0xC0 are portrait, so don't use them |
| Image shifted or garbage at the edges | The column/row window in `pushFrame()` |
| Trackball moves the wrong way | Swap pins in `boards.h`. Too twitchy or sluggish: change `TRACKBALL_STEP` in `config.h` |
| KB shows `--` | The keyboard C3 needs more time after power-on (raise the 600 ms wait in `setup()`), or its firmware isn't LilyGO's |
| Battery % is wrong | Divider factor in `readBattery()` or the voltage→% curve. The `[stat]` log prints the raw pin mV |
| RF shows `--` / "RADIO INIT FAILED (n)" | RadioLib error code n. -2 = chip not found (SPI/CS/power). Check the TCXO voltage (RadioLib default 1.6 V) |
| Two decks can't chat | Same `LORA_FREQ_MHZ`, BW, SF, CR, and `LORA_SYNC_WORD` on both. Radio hardware band must match (915 US / 868 EU) |

## LoRa chat protocol (T-Deck only)

A plaintext packet is `CALLSIGN|message`, up to 127 bytes, with no encryption, no ack, and no retries. Anyone on the same frequency/SF/BW/CR/sync word
can read it. The sync word 0x2B keeps it separate from Meshtastic/LoRaWAN, and it is **not** Meshtastic-compatible. Received lines
show sender RSSI. Chat history lives in RAM only (`MAX_CHAT` lines) and is lost on reboot. On boards without LoRa, COMMS is greyed out
and explains that there's no radio.

## Known gaps / backlog

- **Frame push is slow:** ~92 ms per frame, so ~10 fps on home. Options:
  - Overlap `composeRow` with SPI using DMA and two line buffers.
  - Keep the framebuffer or the 5-row glow window in internal RAM.
  - Try 80 MHz SPI on the ES3C28P's IO_MUX pins.
- ES3C28P: verify the SD card and the BOOT key (short press and 2 s hold). Confirm orientation and colours by eye.
- ES3C28P hardware not used yet: speaker (ES8311 + FM8002E amp), microphone, RGB LED (could signal events), the free GPIOs.
- T-Deck: never flashed. Do the bring-up first (checklist above).
- FILES lists folders only. Clicking a file does nothing (there's no text viewer yet).
- SCAN is passive and can't connect to Wi-Fi. There is no clock, so NTP after connecting would allow a header clock.
- COMMS: no persistence, encryption, delivery acks, or channel/callsign editing on device (callsign is set in `config.h`).
  There's no on-screen keyboard, so touch-only boards can't type.
- No sleep or screen timeout, so the battery drains with the screen on.
- T-Deck hardware not used yet: GPS (Plus), speaker, mic, touchscreen. Keyboard backlight control.

## Conventions

- Settings the user is likely to change go in `config.h`. Runtime-adjustable ones are persisted with `Preferences`
  (namespace `deck`: `bright`, `scan`, `glow`, and `tcal` for the touch calibration).
- Keep the all-caps terminal voice in UI strings.
- Before suggesting a new library, check it builds with esp32 core 3.x (many older libraries break on 3.x).
