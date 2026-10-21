# Pokémon Editor v1.3 — actual UI redesign

This update changes the **SDL2 drawing code**, rather than merely renaming tabs. It is still experimental source, NOT a compiled `.muxapp`.

## New layouts
- Fixed 640 × 480, low-resource dark pixel-art UI with turquoise highlights.
- Persistent left sidebar: Bag / PC Box / Party / Egg Lab / Events / Health.
- PC storage: 6 × 5 individual slot tiles with species-specific mini icons, selection focus, selected Pokémon name.
- Party: six separate Pokémon cards with mini sprites, names, and highlight.
- Pokémon editor: opens as a full separate screen, with a larger sprite and grouped field rows for species, shiny, held item, nature, moves, IVs/EVs and deletion.
- Egg Lab: large species preview, species name and egg options.
- Bag: separate catalogue panel and quantity settings.
- Events/Creator: separate mode selector and preview.
- Save browser/folder picker: matching updated design.
- Dedicated save and delete confirmation screens.

## Not changed
Underlying game save-editing code from v1.2, including its known limitations, is retained.

## Safety and current limitations
- UI has passed a C **syntax-only** check with SDL2 stubs. Actual ARM64 build and handheld display still need checking.
- Gen VII PokeAPI sprites are still used; this UI redesign **does not correct any incorrect sprite source assets**. `sprite-review-all-386` audit output in GitHub Actions can be used to identify mismatches.
- Game-specific mechanics (especially editing party species and legal eggs) are not completely validated.
- An edited save overwrites the original only after confirmation and an automatic backup. Close RetroArch first.
- All 386 icons are generated during GitHub Actions and packed into the executable, no device network required.

## Install source
Replace `src/main.c` in your repository with the file in this ZIP. Keep the existing `scripts/build_icons.py`, `scripts/build_names.py` and `.github/workflows/build.yml` **if they are already the v1.2 versions**. Otherwise upload the copies included here. Run GitHub Actions and send back the compiled artifact for `.muxapp` packaging.
