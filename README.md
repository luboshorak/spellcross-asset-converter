# Spellcross Asset Converter

Desktop asset investigation and conversion toolkit for **Spellcross Reloaded**.
C++20, wxWidgets and CMake. No command-line application is required.

## 0.3.0 — asset browser and sprite export

- Open an installed game folder, mounted disc, standalone FS or FSU archive, or a conventional 2048-byte-sector ISO 9660 image (primary volume).
- Browse and filter files and FS/FSU members, preview the first 4096 stored bytes as hex/ASCII, export selected records or all FS/FSU members.
- Structural bounds validation and warning log; export a TSV inventory.
- Read-only input, fresh numbered output directories, no archive-controlled destination paths. Duplicate names remain distinguishable.
- Raw extraction always preserves stored bytes. LZ/LZ0 and FSU sprites can be decoded on selection for preview. Sprite scanlines preserve original indices, opacity, origin and trailing-byte count; UI renders shadows approximately and combines the first MAP.PAL bank with UNITS.PAL and SYSTEM.PAL (missing banks fall back to grayscale). PAL RGB triples are shown as swatches.

The application currently does **not** identify language editions, decode other graphics formats or maps, automatically choose terrain palettes, decode nested LZ layers, calculate checksums, convert modern formats, or support raw BIN/CUE, Joliet-only, UDF or multi-extent ISO files. Scan and extraction are synchronous in this milestone. Original content is not included.

## Build

Install a C++20 compiler, CMake 3.20+, and wxWidgets 3.2 (core/base).
On Ubuntu: `sudo apt install build-essential cmake libwxgtk3.2-dev`.
On Windows, the CI uses MSYS2 UCRT64 with GCC, CMake, Ninja and wxWidgets 3.2 MSW.
From the appropriate development shell:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Launch the built **SpellcrossConverter** desktop executable. On Windows with MSYS2, run from the UCRT64 environment (runtime DLLs must be available). For core-only development use `-DSC_CONVERTER_GUI=OFF`; this builds the library and tests, not a CLI.

## Architecture

- `include/spellcross/`, `src/core/`: GUI-independent inventory, readers and extraction.
- `src/gui/`: desktop wxWidgets front end, styled in the Reloaded colour family.
- `tests/`: synthetic fixtures only.
- Future decoding and conversion services will extend the core; the GUI may later be hosted by Reloaded.

The FS record layout was verified against the MIT-licensed Spellcross Map Editor FSarchive implementation by Stanislav Maslan (2021), also used by Spellcross Reloaded. See `THIRD_PARTY_NOTICES.md`.

MIT license. This repository distributes source code, not original Spellcross game assets.

### Asset Browser

Resizable tree / preview split. Search by logical path and filter by format. Image, stored HEX, decoded HEX and metadata are independent tabs. Fit / zoom and scroll; previous/next or PageUp/PageDown for visible sprites in a group. Play sequence shows name-ordered frames at 160 ms, **not** reconstructed game animation/azimuth timing. Clear deselects the asset. Raw and decoded extraction remain separate.

Sprite export (single or entire resource group) writes a fresh output folder, one folder per sprite: `preview.png` (RGBA, approximate shadow), `indices.png` (RGB grayscale values equal to original palette indices), `opacity.png` (grayscale mask), `sprite.json` (offsets, palette, known entries and source metadata), `original.bin` (exact stored bytes). The index/mask plus metadata retain decoded pixel semantics; original bytes retain everything not yet interpreted. Export and full supported-decoder validation show cancellable progress. Failed sprites are logged individually. PNG bundle is an intermediate format, not a game-ready pack.
