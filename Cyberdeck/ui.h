// Screen layouts. Pure drawing from a UiState snapshot, no hardware access.
#pragma once
#include <stdint.h>
#include "gfx.h"

enum Screen : uint8_t { SCR_BOOT, SCR_HOME, SCR_SYSTEM, SCR_SCAN, SCR_COMMS, SCR_TOOLS, SCR_FILES };

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

  // power
  int battPct = 0;
  float battV = 0;

  // boot
  BootLine boot[BOOT_LINES];
  int bootCount = 0;

  // system
  char chip[24] = "";
  int cpuMhz = 0, cores = 0;
  uint32_t flashMB = 0;
  uint32_t heapFree = 0, heapTotal = 0, psramFree = 0, psramTotal = 0;
  uint32_t uptimeS = 0;
  char mac[18] = "";
  bool radioOk = false, kbOk = false, sdOk = false;

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
