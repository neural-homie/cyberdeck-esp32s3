# Cyberdeck firmware. Needs arduino-cli on PATH for setup/build/flash/monitor,
# a C++17 compiler for preview, python3 for dist.
#
# BOARD picks the hardware: es3c28p (LCDWiki 2.8" ESP32-S3 touch display, default) or tdeck (LILYGO T-Deck).

BOARD     ?= es3c28p
FQBN      ?= esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB
ESP32_URL := https://espressif.github.io/arduino-esp32/package_esp32_index.json
ESP32_VER ?= 3.3.12
RADIOLIB  ?= 7.8.1
PORT      ?=
CXX       ?= c++
BUILD     := build

BOARD_ID_tdeck   := 1
BOARD_ID_es3c28p := 2
BOARD_ID := $(BOARD_ID_$(BOARD))
ifeq ($(BOARD_ID),)
$(error BOARD must be es3c28p or tdeck, got '$(BOARD)')
endif
FW := $(BUILD)/fw-$(BOARD)
# Core and library sources embed __FILE__ paths in the image. Rewrite the home directory to ~ so
# published images (make dist) don't carry the builder's username.
PREFIX_MAP := $(if $(HOME),-fmacro-prefix-map=$(HOME)=~)

.PHONY: help setup build build-all check-port flash monitor preview dist clean

help:
	@echo "make preview                 render every screen for both boards to build/preview/*.png (no hardware needed)"
	@echo "make setup                   install the ESP32 core $(ESP32_VER) and RadioLib $(RADIOLIB) into arduino-cli"
	@echo "make build [BOARD=...]       compile the firmware into $(BUILD)/fw-<board> (BOARD=es3c28p|tdeck, default es3c28p)"
	@echo "make build-all               compile for every board"
	@echo "make flash PORT=... [BOARD=] upload (e.g. /dev/cu.usbmodem1101, /dev/ttyACM0, COM5)"
	@echo "make monitor PORT=...        serial monitor at 115200"
	@echo "make dist [BOARD=...]        build, then write dist/cyberdeck-<board>-0x0.bin for the web flasher"

setup:
	arduino-cli core update-index --additional-urls $(ESP32_URL)
	arduino-cli core install esp32:esp32@$(ESP32_VER) --additional-urls $(ESP32_URL)
	arduino-cli lib install "RadioLib@$(RADIOLIB)"

build:
	arduino-cli compile --fqbn $(FQBN) --warnings default $(COMPILE_OPTS) \
	  --build-property "compiler.cpp.extra_flags=-DDECK_BOARD=$(BOARD_ID) $(PREFIX_MAP)" \
	  --build-property "compiler.c.extra_flags=$(PREFIX_MAP)" \
	  --output-dir $(FW) Cyberdeck

build-all:
	$(MAKE) build BOARD=es3c28p
	$(MAKE) build BOARD=tdeck

check-port:
	@test -n "$(PORT)" || (echo "Set PORT, e.g. make flash PORT=/dev/cu.usbmodem1101  (arduino-cli board list shows it)"; exit 1)

flash: check-port build
	arduino-cli upload --fqbn $(FQBN) -p $(PORT) --input-dir $(FW) Cyberdeck

monitor: check-port
	arduino-cli monitor -p $(PORT) -c baudrate=115200

preview:
	@mkdir -p $(BUILD)/preview
	$(CXX) -std=c++17 -O2 -Wall -Wextra -ICyberdeck tools/preview/preview.cpp Cyberdeck/gfx.cpp Cyberdeck/ui.cpp -o $(BUILD)/preview/render
	$(BUILD)/preview/render $(BUILD)/preview

# --clean: arduino-cli otherwise reuses a cached core.a built without PREFIX_MAP
dist:
	$(MAKE) build BOARD=$(BOARD) COMPILE_OPTS=--clean
	@mkdir -p dist
	python3 tools/trim_bin.py $(FW)/Cyberdeck.ino.merged.bin dist/cyberdeck-$(BOARD)-0x0.bin

clean:
	rm -rf $(BUILD)
