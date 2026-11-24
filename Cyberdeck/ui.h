// Screen layouts. Pure drawing from a UiState snapshot, no hardware access.
#pragma once
#include <stdint.h>
#include "gfx.h"

// Home menu order must match SCR_SYSTEM..SCR_FILES. Screens after SCR_FILES are not on the menu.
enum Screen : uint8_t { SCR_BOOT, SCR_HOME, SCR_SYSTEM, SCR_SCAN, SCR_COMMS, SCR_TOOLS, SCR_FILES, SCR_TOUCHCAL };

// Touch calibration targets (screen px), tapped in this order.
#define CAL_POINTS 4
extern const int16_t CAL_TARGETS[CAL_POINTS][2];

#define MENU_COUNT   5
#define MAX_NETS     32
#define MAX_CHAT     40
#define CHAT_COLS    40
#define MAX_FILES    48
#define BOOT_LINES   7
#define SCAN_ROWS    10   // list rows visible on the SCAN screen
#define FILE_ROWS    10   // list rows visible on the FILES screen

struct NetInfo  { char ssid[33]; int8_t rssi; uint8_t channel; bool secure; };
struct ChatLine { char text[CHAT_COLS + 1]; uint8_t kind; int16_t rssi; };  // kind: 0 system, 1 me, 2 them
struct FileEntry { char name[40]; bool dir; uint32_t size; };
struct BootLine { char label[24]; uint8_t status; };  // 0 pending, 1 ok, 2 fail, 3 skipped

struct UiState {
  Screen screen = SCR_BOOT;
  uint32_t ms = 0;               // animation clock
  int menuSel = 0;

  // board capabilities (set once at boot)
  bool touchUi = false;          // touchscreen: tap/drag hints, "<" back in the header, footer buttons
  bool hasRadio = true;          // false = no LoRa on this board: COMMS is greyed out
  const char* boardName = "T-DECK";

  // power
  int battPct = 0;
  float battV = 0;

  // boot
  BootLine boot[BOOT_LINES];
  int bootCount = 0;
  int bootTotal = BOOT_LINES;    // steps this board runs, for the progress bar

  // system
  char chip[24] = "";
  int cpuMhz = 0, cores = 0;
  uint32_t flashMB = 0;
  uint32_t heapFree = 0, heapTotal = 0, psramFree = 0, psramTotal = 0;
  uint32_t uptimeS = 0;
  char mac[18] = "";
  bool radioOk = false, inputOk = false, sdOk = false;   // inputOk: keyboard (T-Deck) or touch panel

  // scan
  bool scanning = false;
  int netCount = 0, netSel = 0, netScroll = 0;
  NetInfo nets[MAX_NETS];

  // comms
  ChatLine chat[MAX_CHAT];
  int chatCount = 0;
  int unread = 0;
  char input[101] = "";
  char callsign[16] = "";
  float freqMhz = 0;
  int lastRssi = 0;

  // touch calibration
  int calStep = 0;               // index of the crosshair to tap next
  bool calFailed = false;        // last attempt's points didn't agree; asking again

  // tools
  int toolSel = 0;
  int brightness = 12;
  bool scanlines = true, glow = true;

  // files
  char path[96] = "/";
  int fileCount = 0, fileSel = 0, fileScroll = 0;
  FileEntry files[MAX_FILES];
  float sdUsedGB = 0, sdTotalGB = 0;
  const char* title = "";
};

extern const char* const MENU_LABELS[MENU_COUNT];

void drawScreen(Canvas& g, const UiState& s);

// Touch: what is under screen point (x, y) on the current screen. Lives next to the drawing code
// so layout and hit areas stay in one place. index is absolute (list scroll already applied).
enum HitKind : uint8_t { HIT_NONE, HIT_BACK, HIT_ITEM, HIT_ACTION };
struct UiHit { HitKind kind; int index; };
UiHit uiHitTest(const UiState& s, int x, int y);
