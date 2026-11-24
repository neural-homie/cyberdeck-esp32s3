#!/usr/bin/env python3
"""Trim arduino-cli's 16 MB merged image to its written part, so it can be flashed at 0x0
from a browser (esptool-js) without uploading megabytes of 0xFF.

usage: trim_bin.py MERGED_BIN OUTPUT_BIN
"""
import sys

src, dst = sys.argv[1], sys.argv[2]
data = open(src, "rb").read()
# Sanity: ESP image magic for the bootloader (0x0) and the app (0x10000), partition table magic at 0x8000.
if data[0] != 0xE9 or data[0x10000] != 0xE9 or data[0x8000:0x8002] != b"\xaa\x50":
    sys.exit(f"{src} does not look like an ESP32-S3 merged image")
end = len(data.rstrip(b"\xff"))
end = (end + 4095) // 4096 * 4096
open(dst, "wb").write(data[:end])
print(f"{dst}: {end} bytes (flash at 0x0)")
