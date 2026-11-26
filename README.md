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

## 0.4.0 — archive batch conversion

In Extraction / Conversion, choose All game data or a logical archive, then Convert and package. A fresh directory contains one standard ZIP per FS/FSU block (including its original container) and one for loose files, with blocks.tsv and scan warnings. Each ZIP has manifest.tsv mapping numeric entry IDs to original logical paths, format, stored size and explicit converted/decoded/raw/failed status. Every entry retains original.bin; supported graphics also receive PNG/index/mask/JSON, supported text receives UTF-8, PAL receives JSON, other successfully decompressed LZ receives decoded.bin. Unsupported or failed conversions retain original bytes. I/O failure or cancellation removes incomplete output. This is an intermediate layout, not yet readable by the game engine. Text encoding can be selected in Asset Browser (default valid UTF-8 else CP895; other choices CP852/1250/1252). Binary DEF is not treated as text. Known raw LZ dimensions, ICO/BTN/CUR/GFK, and FSU use shared image decoding for preview and batch. Nested LZ probing is conservative (implicit 256-symbol dictionary only); other formats such as PNM, terrain/map definitions, audio and video remain original-only. PNG preview palette/shadows are approximate; indices/mask and original preserve data.

TEXTS.FS event/mission names without conventional extensions and RESEARCH.FS INF entries are recognized by container context. INFO.FS portraits (437×390) and known chrome dimensions use matching per-image palette files or PALETA.NRM; missing palettes are marked unknown. ZIP64 is used for large archive entry counts.

## 0.4.1 — recursive browser

Expand compressed LZ/LZ0 files and FSU sprites using the tree arrow; each decoded layer appears as an actual child. Nested LZ is probed with bounded decoding, nested FS tables are validated, named nested FS/FSU and PNM frames are expandable. Select any virtual node to view its own bytes, image, text or metadata and use Extract decoded to export that exact layer. Data is decoded on demand (64 MiB per layer, maximum depth 12); tree nodes retain recipes rather than decompressed buffers. Unrecognized payloads stay binary; heuristic identification is not a guarantee of format. PNM preview uses the existing indexed scanline representation; animation timing and other container types such as ANM/CAN remain unsupported by the recursive viewer. Batch conversion layout is unchanged.

## 0.5.0 — native media preview

Select a CAN, SMK, DPK or DP2 file and click Play video. The player decodes a bounded clip and plays indexed video using its actual per-frame palettes and available PCM audio (wxSound, platform-dependent). DP2 companion WAVE is searched by original basename, including extensionless assets. Video preview uses the existing Reloaded CAN/Smacker decoders pinned in vendor/reloaded, and DPK/DP2 delta layout from the MIT restoration tools. No FFmpeg dependency for preview. Frame memory is limited; long movies may fail the preview limit. Sound/video timing is preview-only, not an engine-grade playback clock. Modern export format remains provisional. Native video library is built alongside the executable, and its license/source and CPAL notices must accompany distributions.

Terrain DTA in T11/PUST/DEVAST and ANM frames have graphical previews. Terrain uses its own MAP.PAL and CYCLE.PAL banks where present. ANM and movies are not yet converted into a final runtime format.

## 0.6.0 — modern PAK prototype

Choose All game data or a source archive in Extraction / Conversion and click Convert to modern PAK. Output is a fresh directory with manifest.json and logical ZIP64 .pak packages. FSU resources, supported UI/terrain graphics and supported PNM/ANM frames are packed into bounded 2048-pixel atlas pages with two lossless data PNG planes, shared palette tables and group JSON. PNG planes are decoded and compared to their source values before writing. Text uses UTF-8; RGB palettes and index filters remain byte-exact. Original source containers are retained once, converted members reference their source range, and unconverted/failed members have direct raw fallback. Original paths and duplicate ordinals identify records. The output includes actual atlas metrics. Diagnostic ZIP export remains available. See docs/PAK-v1.md. This is not yet a playable replacement for retail data: binary definitions, map semantics, fonts, sound/video conversion and animation timing/azimuth reconstruction remain open. No SR loader changes.
