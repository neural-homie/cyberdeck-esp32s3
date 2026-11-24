# Handoff prompt

Open this folder in Claude Code, then paste everything below the line as your first message.
Add what you want to build next at the end, or leave it as is and Claude will ask.

---

I'm picking up a cyberdeck firmware project for ESP32-S3 handhelds. It's a retro terminal UI styled after docs/reference-ui.jpg,
and it builds for two boards: the LCDWiki 2.8" ES3C28P touch display (default, runs on real hardware) and the LILYGO T-Deck
(compiles, never tested on a T-Deck). CLAUDE.md has the architecture, rules, hardware notes, verified/unverified status and backlog.

Please:
1. Read CLAUDE.md, then skim Cyberdeck/ (start with boards.h) and tools/preview/preview.cpp.
2. Check that arduino-cli is installed. If it isn't, tell me how to install it on my OS and wait for me. Then run
   `make setup` and `make build-all` and confirm both boards compile on this machine.
3. Run `make preview` and look at build/preview/all_es3c28p.png (and all.png for the T-Deck) so you know what the UI looks like.
4. Ask me which board I have plugged in. Before flashing anything, check the chip's MAC with esptool (see "Flashing notes" in
   CLAUDE.md), and back up a board that has never run this firmware.
5. Show me the backlog from CLAUDE.md and ask what I want next.

Working rules: run `make preview` and look at the PNGs after every UI change, and `make build-all` after every change. Keep ui.cpp and
gfx.cpp free of Arduino code so the preview keeps working. Commit after each working step.
