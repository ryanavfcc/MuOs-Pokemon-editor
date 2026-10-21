# Pokémon Editor v1.0 — automatic cheat-file installer (experimental)

The Encounter tab now attempts to write a game-named `.cht` file directly into the existing RetroArch cheats directory, using the mGBA/gpSP core subdirectory if found. **This is not verified as fully automatic gameplay activation.**

- FireRed/LeafGreen only; game codes may not match Rev 1/v1.1.
- A in ENCOUNTR installs the `.cht`, but never overwrites an existing cheat file. If none of the expected core subdirectories exist, it reports failure.
- In RetroArch, the user must enable `Quick Menu > Cheats > Auto-Apply Cheats During Game Load` once, and save the setting for the game.
- The save filename must match the ROM filename (before `.srm`/`.gba`).
- Do not leave encounter cheats enabled after catching the target.
- No ROM patches or NPC events. Normal shiny odds unchanged.
- This source has only been syntax checked; not ARM64 built or tested on an Anbernic.

Replace `src/main.c` in the existing repository, run GitHub Actions and send the compiled artifact for .muxapp packaging.
