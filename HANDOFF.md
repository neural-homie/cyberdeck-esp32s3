# Handoff prompt

Open this folder in Claude Code, then paste everything below the line as your first message.
Add what you want to build next at the end, or leave it as is and Claude will ask.

---

I'm picking up a firmware project for my LILYGO T-Deck (ESP32-S3 handheld with keyboard, trackball, LoRa). It's a
cyberdeck UI styled after docs/reference-ui.jpg. It was built in another Claude session, and CLAUDE.md has the architecture, rules,
hardware notes and backlog.

Where it stands: the firmware compiles cleanly (arduino-cli, esp32 core 3.3.12, RadioLib 7.8.1), and the UI looks right
in the desktop preview. It has NOT run on real hardware yet. The display init, trackball, keyboard and battery code come from
LilyGO's examples but are unverified.

Please:
1. Read CLAUDE.md, then skim Cyberdeck/ and tools/preview/preview.cpp.
2. If this folder isn't a git repo yet, run git init and commit everything as the baseline.
3. Check that arduino-cli is installed. If it isn't, tell me how to install it on my OS and wait for me. Then run
   `make setup` and `make build` and confirm it compiles on this machine.
4. Run `make preview` and look at build/preview/all.png so you know what the UI looks like.
5. Ask me whether I've flashed it to my T-Deck yet and what happened. If something's wrong, use the first-boot checklist in
   CLAUDE.md. If I haven't flashed it, help me do it with `make flash` (run `arduino-cli board list` to find the port).
6. Once it works on the device, show me the backlog from CLAUDE.md and ask what I want next.

Working rules: run `make preview` and look at the PNGs after every UI change, and `make build` after every change. Keep ui.cpp and
gfx.cpp free of Arduino code so the preview keeps working. Commit after each working step.
