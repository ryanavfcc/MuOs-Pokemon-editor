# Pokémon Editor v0.8 — save safety and UI groundwork

This is a **development source package**, not a tested `.muxapp`.

### Changes from v0.7
- Save Health Check tab: validity of both checksum slots, game label, dirty status, create a numbered backup.
- FireRed / LeafGreen / Emerald save files can be opened for checksum inspection **read-only**; editing remains locked.
- Ruby/Sapphire edits are restricted to explicitly recognised filenames, not arbitrary unknown saves.
- Prevent backing out of a file while there are unsaved edits (save first; to discard restart without saving).
- Five-tab layout, clear read-only warning, adjusted navigation and safe backup controls.

### Not yet shipped
- Functional NPC-triggered encounters / ROM modification / ACE (unsafe to claim with a save editor).
- Authentic sprite images in the compiled build: the image preview is a **concept mockup**, not the current UI.
- Full event database / `.pk3` import / general 386-species editor / validated FireRed and LeafGreen writes.
- Reliable confirmation for every game in emulator or on hardware.

### Building
Replace `src/main.c` in your GitHub repository; run existing Actions build. This is NOT verified on real SDL2 ARM64 hardware. Keep separate copies of every save.
