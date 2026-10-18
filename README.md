# muOS Pokémon editor project — first hardware test

This is a **read-only ARM64 SDL2 save browser prototype**, NOT yet the full save editor.
It can list 128 KiB `.sav` files in common SD card ROM/save folders and show their names.
It **does not edit Pokémon, export saves, or generate eggs yet**.

The GitHub Actions workflow compiles an ARM64 Linux binary and makes an artifact.
Compatibility with **muOS 2601.0 Jacaranda on RG35XX SP** is unverified.
Ubuntu's ARM64 SDL2 may not match the muOS runtime; a muOS-compatible
toolchain and packaging adjustments may be needed after hardware testing.

## Upload from iPhone
Unzip the source ZIP in Files. On GitHub open your repository, select
**Add file > Upload files**, and upload the extracted files/folders,
including `.github/workflows/build.yml` (hidden folders can be awkward
on iOS). If GitHub's mobile interface cannot upload folders, use a PC
or GitHub's web editor to create the files at their matching paths.

After committing, go to **Actions**, select **Build ARM64 muOS prototype**,
and run the workflow if needed. Download the `pokemon-browser-arm64`
artifact from the successful workflow run. This is a test build, not
a verified installation package.

Keep original Pokémon saves backed up. This version never writes to them.
