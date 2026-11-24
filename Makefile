# Cyberdeck for LILYGO T-Deck. Needs arduino-cli on PATH for setup/build/flash/monitor,
# a C++17 compiler for preview, python3 for dist.

FQBN      ?= esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB
ESP32_URL := https://espressif.github.io/arduino-esp32/package_esp32_index.json
ESP32_VER ?= 3.3.12
RADIOLIB  ?= 7.8.1
PORT      ?=
CXX       ?= c++
BUILD     := build

.PHONY: help setup build check-port flash monitor preview dist clean

help:
	@echo "make preview          render every screen to build/preview/*.png (no hardware needed)"
	@echo "make setup            install the ESP32 core $(ESP32_VER) and RadioLib $(RADIOLIB) into arduino-cli"
	@echo "make build            compile the firmware into build/fw"
	@echo "make flash PORT=...   upload to a T-Deck (e.g. /dev/ttyACM0, /dev/cu.usbmodem1101, COM5)"
	@echo "make monitor PORT=... serial monitor at 115200"
	@echo "make dist             build, then write dist/cyberdeck-tdeck-0x0.bin for the web flasher"

setup:
	arduino-cli core update-index --additional-urls $(ESP32_URL)
	arduino-cli core install esp32:esp32@$(ESP32_VER) --additional-urls $(ESP32_URL)
	arduino-cli lib install "RadioLib@$(RADIOLIB)"

build:
	arduino-cli compile --fqbn $(FQBN) --warnings default --output-dir $(BUILD)/fw Cyberdeck

check-port:
	@test -n "$(PORT)" || (echo "Set PORT, e.g. make flash PORT=/dev/ttyACM0  (arduino-cli board list shows it)"; exit 1)

flash: check-port build
	arduino-cli upload --fqbn $(FQBN) -p $(PORT) --input-dir $(BUILD)/fw Cyberdeck

monitor: check-port
	arduino-cli monitor -p $(PORT) -c baudrate=115200

preview:
	@mkdir -p $(BUILD)/preview
	$(CXX) -std=c++17 -O2 -Wall -Wextra -ICyberdeck tools/preview/preview.cpp Cyberdeck/gfx.cpp Cyberdeck/ui.cpp -o $(BUILD)/preview/render
	$(BUILD)/preview/render $(BUILD)/preview

dist: build
	@mkdir -p dist
	python3 tools/trim_bin.py $(BUILD)/fw/Cyberdeck.ino.merged.bin dist/cyberdeck-tdeck-0x0.bin

clean:
	rm -rf $(BUILD)
