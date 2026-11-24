# Cyberdeck firmware: LILYGO T-Deck (ESP32-S3)

Handheld "cyberdeck" firmware for the LILYGO T-Deck / T-Deck Plus: a 320×240 ST7789 screen, a BlackBerry-style
keyboard (separate ESP32-C3 on I2C), a trackball, an SX1262 LoRa radio, microSD, and a battery.
The UI is a cyan-on-navy retro terminal styled after `docs/reference-ui.jpg`. It has a glowing phosphor look,
CRT scanlines, an animated pixel skyline on the home screen, and five apps: SYSTEM, SCAN, COMMS, TOOLS, FILES.

## Status (read this first)

- **Compiles cleanly** with arduino-cli, esp32 core **3.3.12**, and RadioLib **7.8.1**: 32% of the 3 MB app partition and 17% of RAM, with zero warnings from project files.
- **The UI renders correctly** in the desktop preview (`make preview`).
- **It has never run on real hardware.** The pin map, display init, backlight driver, trackball mapping, and keyboard
  protocol were copied from LilyGO's official T-Deck examples (github.com/Xinyuan-LilyGO/T-Deck, `examples/UnitTest`,
  `examples/HelloWorld`, `utilities.h`), but none of it has been confirmed on a device. Treat the first flash as
  bring-up and use the checklist below.

## Commands

```
make preview              # render every screen to build/preview/*.png (2x) + all.png contact sheet. No hardware needed.
make setup                # one-time: install esp32 core + RadioLib into arduino-cli
make build                # compile into build/fw
make flash PORT=<port>    # upload (arduino-cli board list shows the port)
make monitor PORT=<port>  # serial at 115200
make dist                 # build + write dist/cyberdeck-tdeck-0x0.bin (single image for the esptool-js web flasher)
```

FQBN: `esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB`
(Arduino IDE equivalent: ESP32S3 Dev Module, USB CDC On Boot Enabled, 16MB flash, OPI PSRAM, 16M Flash 3MB APP/9.9MB FATFS.)

If `make` isn't available (Windows without make), run the commands from the Makefile directly.

**After any UI change, run `make preview` and look at the affected PNGs before calling it done. After any change, run `make build`.**

## Layout

```
Cyberdeck/            Arduino sketch (folder name must match Cyberdeck.ino)
  Cyberdeck.ino       ALL hardware: display driver, backlight, battery, Wi-Fi scan, LoRa, SD, trackball,
                      keyboard, navigation/input (handle()), boot sequence, setup()/loop()
  ui.h / ui.cpp       UiState (everything a screen needs) + one draw function per screen + skyline art
  gfx.h / gfx.cpp     Canvas (software renderer into a 320x240 RGB565 framebuffer), palette, 5x7 text,
                      composeRow() post-process (glow + scanlines + byte swap for the panel)
  font5x7.h           Adafruit GFX glcdfont, ASCII 32-126 (BSD)
  config.h            user settings: title, callsign, LoRa params, display defaults, trackball step
tools/preview/        desktop renderer: same ui.cpp/gfx.cpp + sample data in SCENES[] -> PNGs
tools/trim_bin.py     trims the 16 MB merged image for web flashing
dist/                 prebuilt cyberdeck-tdeck-0x0.bin (from the last verified build)
docs/reference-ui.jpg the look we're matching
```

## Architecture rules

1. **`ui.*` and `gfx.*` must stay hardware-free.** Use only the standard C/C++ library, with no `Arduino.h`, `String`, `millis()`,
   or `Serial`. That is what lets `tools/preview` compile on a desktop. Hardware reads go in `Cyberdeck.ino` and are copied
   into `UiState S`, and the UI only reads `UiState`.
2. **Rendering pipeline:** `drawScreen(canvas, S)` draws the whole frame into `fb` (in PSRAM), and then `pushFrame()` runs
   `composeRow()` per row and streams it over SPI. Every frame is a full redraw. There are no partial updates and no TFT_eSPI or LVGL.
3. **Frame pacing** happens in `loop()`: it redraws when `dirty` is set or when the per-screen interval passes (home 90 ms for animation,
   scan radar 50 ms, comms 250 ms for cursor blink, everything else 1 s). Set `dirty = true` after state changes.
4. **Animation** reads `S.ms` (millis at draw time), and randomness comes from `hash32()` seeded by position/index, so frames are deterministic
   and the preview can pin a moment with `s.ms`.
5. **Input** goes through one function: `handle(Ev, key)` with EV_UP/DOWN/LEFT/RIGHT/CLICK/KEY. Enter maps to CLICK, and Backspace or
   trackball-left maps to back. On COMMS, printable keys go to the text box.
6. **Arduino auto-prototypes:** types used in function signatures in the .ino (like `Ev`) must be declared near the top
   of the file, before the first function. Otherwise compilation fails with "'Ev' was not declared".
7. Keep `ui.h` list-size constants (`SCAN_ROWS`, `FILE_ROWS`, `CHAT_COLS`, `MAX_*`) as the single source of truth shared by
   the UI and the .ino.

### Adding a screen

1. Add it to `enum Screen` in `ui.h`, plus any state fields in `UiState`.
2. Write `drawX()` in `ui.cpp` using `header()`, content from y=54, and `footer()`, then add it to `drawScreen()`.
3. Wire it up in `Cyberdeck.ino`: `openScreen()` (enter/exit side effects), `handle()` (input), and `loop()` (frame interval, if animated).
   To put it on the home menu, extend `MENU_LABELS`/`MENU_COUNT`. Home uses `SCR_SYSTEM + menuSel`, so menu order must match
   the enum order. With 6+ items the menu needs re-spacing, because each item is 30 px tall starting at y=54.
4. Add one or more scenes to `SCENES[]` in `tools/preview/preview.cpp`, then run `make preview` and look at them.

## UI style guide

- Screen is 320×240 landscape. The outer frame is `roundRect(4,4,312,232,r=9)`.
- Sub-screens use `header()`, which draws a breadcrumb `TITLE /` at y=13 (scale 1), the name at y=24 (scale 2), a 2-px rule at y=43,
  the battery at top right, and an info string at right y=30. Content starts at y=54, and the footer hint is at y=220.
- Text uses a 5×7 bitmap font scaled 1/2/3, with an advance of 6 px × scale. **`Canvas::text()` uppercases everything** on purpose:
  lowercase in the 5×7 font has no room for descenders and looked broken.
- Selection is a `C_HILITE` filled bar with `C_INK` text.
- **Glow rule:** in `composeRow()`, any pixel whose 6-bit green channel is ≥32 glows (except `C_INK`). Of the palette, these glow:
  BRIGHT, MID, WIN_ON, HILITE, WARN. These don't: SOFT, DIM, FAINT, BUILDING, WIN_OFF, ALERT, INK.
  Use `C_SOFT` for secondary text so it stays crisp and doesn't bloom. A new colour's glow follows from its green value.
- **Scanlines:** odd rows are darkened ~20%, but only where the source pixel's 6-bit green is <20 (backgrounds), so text stays sharp.
- Use `C_WARN` (amber) for warnings and low battery (≤15%). `C_ALERT` (red) is used sparingly (beacon, unread dot).

## Hardware facts (from LilyGO's utilities.h)

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
| GPS RX / TX (Plus only) | 44 / 43 | Unused so far. On the Plus, the Grove port is taken by the GPS |
| I2S speaker | WS 5, BCK 7, DOUT 6 | Unused |
| ES7210 mic | MCLK 48, LRCK 21, SCK 47, DIN 14 | Unused |
| Touch interrupt | 16 | Touch panel unused (GT911 on I2C) |

The ESP32-S3 has BLE only, with no Bluetooth Classic.

## First-boot checklist (hardware is unverified)

| Symptom | Likely fix |
|---|---|
| Black screen | Check that GPIO10 is HIGH early in `setup()`. Try `setBacklight(16)`. Confirm the init sequence in `tftInit()` |
| Colours inverted (light background) | In `tftInit()`, swap `0x21` (inversion on) for `0x20` |
| Red/blue swapped (orange where cyan should be) | MADCTL `0x36`: set the BGR bit, `0x60 → 0x68` |
| Upside-down or mirrored | MADCTL `0x60 → 0xA0` (180°). Other values: 0x00/0xC0 are portrait, so don't use them |
| Image shifted or garbage at the edges | The column/row window in `pushFrame()` |
| Trackball moves the wrong way | Swap pins in `initTrackball()`. Too twitchy or sluggish: change `TRACKBALL_STEP` in `config.h` |
| KB shows `--` | The keyboard C3 needs more time after power-on (raise the 600 ms wait in `setup()`), or its firmware isn't LilyGO's |
| Battery % is wrong | Divider factor in `readBattery()` or the voltage→% curve. Log `analogReadMilliVolts(4)` |
| RF shows `--` / "RADIO INIT FAILED (n)" | RadioLib error code n. -2 = chip not found (SPI/CS/power). Check the TCXO voltage (RadioLib default 1.6 V) |
| Two decks can't chat | Same `LORA_FREQ_MHZ`, BW, SF, CR, and `LORA_SYNC_WORD` on both. Radio hardware band must match (915 US / 868 EU) |

Expected full-frame push time is ~31 ms (153,600 bytes at 40 MHz SPI) plus `composeRow` CPU time. This hasn't been measured yet.

## LoRa chat protocol

A plaintext packet is `CALLSIGN|message`, up to 127 bytes, with no encryption, no ack, and no retries. Anyone on the same frequency/SF/BW/CR/sync word
can read it. The sync word 0x2B keeps it separate from Meshtastic/LoRaWAN, and it is **not** Meshtastic-compatible. Received lines
show sender RSSI. Chat history lives in RAM only (`MAX_CHAT` lines) and is lost on reboot.

## Known gaps / backlog

- Never flashed. Do the bring-up first (checklist above).
- FILES lists folders only. Clicking a file does nothing (there's no text viewer yet).
- SCAN is passive and can't connect to Wi-Fi. There is no clock, so NTP after connecting would allow a header clock.
- COMMS: no persistence, encryption, delivery acks, or channel/callsign editing on device (callsign is set in `config.h`).
- No sleep or screen timeout, so the battery drains with the screen on.
- Unused hardware: GPS (Plus), speaker, mic, touchscreen.
- Optional: DMA/double-buffered display push for smoother animation. Keyboard backlight control.

## Conventions

- Settings the user is likely to change go in `config.h`. Runtime-adjustable ones are persisted with `Preferences`
  (namespace `deck`: `bright`, `scan`, `glow`).
- Keep the all-caps terminal voice in UI strings.
- Before suggesting a new library, check it builds with esp32 core 3.x (many older libraries break on 3.x).
