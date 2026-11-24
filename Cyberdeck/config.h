// ---------------------------------------------------------------------------
//  Cyberdeck firmware settings. Change these, then re-flash.
// ---------------------------------------------------------------------------
#pragma once

// Hardware target: 1 = LILYGO T-Deck, 2 = LCDWiki 2.8" ES3C28P (see boards.h).
// `make build BOARD=tdeck|es3c28p` overrides this; the Arduino IDE uses the value here.
#ifndef DECK_BOARD
#define DECK_BOARD        2
#endif

// Title shown on the home screen (keep it to ~16 characters).
#define DECK_TITLE        "NEOTROPOLIS"

// Name sent with your LoRa messages. Leave empty ("") to use DECK-xxxx from the chip ID.
#define DECK_CALLSIGN     ""

// LoRa radio. Use the band your T-Deck was sold for and that is legal where you are:
//   915.0 = North America / Australia, 868.0 = Europe, 433.0 = Asia (check local rules).
// Every deck that should talk to each other needs the same five values.
#define LORA_FREQ_MHZ     915.0
#define LORA_BW_KHZ       125.0
#define LORA_SF           9
#define LORA_CR           7
#define LORA_SYNC_WORD    0x2B      // private network word, keeps you off Meshtastic/LoRaWAN traffic
#define LORA_TX_DBM       14        // 2..22. 14 dBm is plenty for a festival grounds

// Display
#define DEFAULT_BRIGHTNESS 12       // 1..16
#define DEFAULT_SCANLINES  true     // CRT-style dark lines on every other row
#define DEFAULT_GLOW       true     // soft halo around text and lines

// Trackball: ticks needed per menu step. Raise if it feels jumpy.
#define TRACKBALL_STEP     3
