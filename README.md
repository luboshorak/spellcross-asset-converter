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

## 0.7.0 — semantic assets and coverage

Modern PAK conversion now recognizes retail SAMPLES.FS unsigned 8/16-bit PCM, stereo PCM and RIFF PCM, including extensionless entries. Output is signed PCM16 WAV at the original rate/channels, verified by re-reading and comparing every sample. JEDNOTKY.DEF (206/207-byte records) exports named fields and complete record hex per unit. FONT_001.FNT exports indexed atlas glyphs and font.json with byte-code positions and spacing. Dataset manifest coverage_by_format counts converted/decoded/raw/failed/container by logical package and format. No claim of full semantic parity: unit text bytes remain source encoded, font code page is not Unicode-mapped, unknown unit attributes retained, video conversion and other definitions/maps remain open. No original data removed.

## 0.8.0 — campaign and tactical maps

Convert block-structured DEF/TUT to ordered script.json (original keywords, repeated commands, exact arguments, block boundaries, unknown syntax count; source text when decodable). Includes campaign chapters, mission scripts, research, upgrade and formation definitions, bonuses and tutorial. DOS EOF markers are recognized. Convert recognized tactical DTA maps to map.json plus exact two-byte ground/object layer arrays; metadata includes animation placement, both counterattack layers and ambient sound placement, resource names and uninterpreted tail. Distinguishes tactical maps from DTA tiles by the retail map header. Asset Browser displays structured JSON for maps, units and scripts. See docs/campaign-v1.md. This is data conversion, not new runtime implementation; plain class tables, semantic cross-reference validation, custom terrain formats, and modern video/music conversion remain open.

The browser keeps original Text and Structured JSON separate. Sound class lists, object class definitions, commander names and cheat lists now have classes.json rather than only unstructured text. SAMPLES.DEF retains all rows in auxiliary groups including unnumbered groups.

## 0.9.0 — multimedia PAK export

MIDI files with valid SMF structure are preserved byte-for-byte; XMIDI is converted using the existing SR converter (conversion mode 0, no instrument remapping) in the private sc_xmidi_worker executable. Both the IFF container and output SMF are checked. The legacy XMIDI parser is isolated from the GUI process but is not a hardened untrusted-file sandbox. For CAN/SMK/DPK/DP2 choose FFmpeg in Extraction / Conversion (auto-discovered alongside the app or on PATH). FFmpeg is an optional runtime dependency, not required for building or browsing. Output is FFV1 lossless RGB in MKV with FLAC audio if available; source frame dimensions and rate, PCM rate/channels and all samples are retained. The generated MKV is decoded again and compared byte-for-byte against source RGB and sample-for-sample against PCM. No shortest/trim or resampling. DP2 with missing companion audio is not marked converted. Missing FFmpeg retains original movies and reports raw; other conversion failures retain source bytes and report failed. Temporary staging uses disk space up to two decoded RGB copies and is removed after each record. Existing memory limits remain; long clips may fail. Actual original CD movie/MIDI corpus and timestamp parity still need testing. Modern runtime loader remains out of scope.

CTest automatically adds an independent FFV1/FLAC round-trip test when Python 3 and FFmpeg are found at configure time. The normal build remains usable without them. External processes are invoked using argument arrays with an explicit staging working directory; no shell command construction. The XMIDI worker suppresses Windows crash dialogs. Individual external media subprocesses currently run synchronously, so cancellation is between assets rather than inside a running FFmpeg encode.

## 0.9.1 — PAK read-back validation

Modern export reopens every completed package before reporting success. Validation also works independently from the Validation tab (select the dataset directory). It streams ZIP entries and checks declared lengths and CRC32, rejects unsafe/duplicate entry names, checks JSON syntax and schema versions, original byte range references, fallback sizes, payload existence, palette sizes, decoded grayscale atlas dimensions and rectangle/page/palette/record references, font glyph counts, map layer lengths, movie metadata and dataset coverage/atlas totals. No extraction to disk is performed. Failures are reported; incomplete newly generated datasets follow existing cleanup behavior. Validation is structural, not a byte-for-byte comparison against an independently supplied original game, not a cryptographic signature, and not a test of gameplay semantics or all inter-asset resource links. Cancellation is between packages. Limits: 64 MiB per JSON/atlas PNG, 256 MiB aggregate JSON per package, 8 GiB per other entry and 32 GiB total per package.

## 0.9.2 — UI graphics, FSU layouts and retail language tables

Original strategic and main-menu indexed images, help screens, RAM frame parts and CLK territory masks use the original renderer dimensions/layout instead of HEX-only display. Palette banks are selected by asset; CLK uses grayscale data values. FSU inventory retains the original member ordinal; browser sequence and export order use it. Atlas metadata contains `fsu_layout` with static slope/azimuth and animated azimuth/frame sequences (including DPEKJ slope animation), or an explicit error if conversion/layout is incomplete. Frame timing, fire origins from external auxiliary context files and exact compositing are not inferred. Retail positional language tables preserve entry IDs and decode per extension; narrative text follows SR control/wrapping rules. Auto (retail edition) chooses CP895/Czech, Windows-1250/Polish, Windows-1252/Italian or English according to source edition; ambiguous editions fall back to the previous UTF-8/CP895 heuristic, with manual encoding override. All originals retained. Four-language full corpus and all remaining unknown image dimensions still need testing.

## 0.9.3 — original INSTALL.DTA / ISO import

Open an original ISO, mounted CD directory, or INSTALL.DTA directly. The original DOS installer directory block and file records are parsed with bounded reads; all directories and files are indexed, including nested FS/FSU archives. Physical offsets are composed with the surrounding ISO extent, so no installation or temporary extraction is required. Loose CD assets remain in the same inventory. The Asset Browser expands INSTALL.DTA into original directories and archives, and both conversion modes can select the complete installer subtree. PAK categories use the nearest actual FS/FSU archive, not the outer installer. Original INSTALL.DTA and nested archives remain byte-exact in their respective packages; this introduces deliberate duplication across packages until a cross-package original-range contract exists. Names retain installer provenance. Malformed indexes emit warnings and do not expose invalid members. Synthetic fixtures cover standalone, mounted directory and embedded ISO input, nested FS/FSU and a CD sidecar, offsets and malformed tables. Actual retail ISO end-to-end testing remains required.

## 0.9.4 — repair retail graphics previews

PNM and ANM frame offsets are relative to the byte after the frame count (as in the original SR decoders). Selecting an animation parent previews it on a shared-origin canvas, with previous/next and loop playback (preview speed, not original timing); individual tree frames remain accessible. SELECT_A–M.LZ0 use masked sprite scanlines; other LZ0 overlays have explicit geometry and hierarchy palette banks. BML 37x24 and RTA_SEL 132x16 are inferred from actual retail rectangular borders, not confirmed by an original renderer call. Known raw widths tolerate up to two retail trailing bytes. ICO/BTN preserve original index 0 and opacity but preview opaque zero using palette index 254, matching AddICO; atlas metadata records that rule. Independently audited original EN installed corpus: 24 LZ0 decompressed, 54 BTN and 67 ICO scanlines parsed, 62 PNM / 786 frame headers and scanlines parsed. Actual full C++ GUI QA and exact colors/other editions remain pending.

## 0.9.5 — contextual UI palettes and additional raw images

Strategic BIGMB/BIGM2/BMPAN and VM buttons use the strategic palette banks, tactical INFO buttons/glyphs use INFO.FS::PALETA.NRM, normal tactical buttons retain the tactical palette. The default generic tactical preview prefers T11 terrain; terrain-specific resources retain their own MAP.PAL. Hierarchy, units and options screens select their page-specific 128..191 bank. BM_LSTA.LZ is 65x89 (verified visually from the 5785 retail pixels), VMU_SLCT 146x17 and end-turn parts 41x36 use existing SR geometry. Tutorial hover variants 200x31, splash images 640x480, plus additional inferred raw geometries (DIFF 200x31, MM_LOAD 402x255, scrollbar arrows 22x28, LEDs) are supported. Inferred geometry is not proof of original screen placement or all color contexts. Remaining unknown LZ assets remain raw, not guessed; no engine changes.

## 0.9.6 — terrain scanline padding and graphics coverage audit

The 80-pixel terrain tile decoder now consumes and validates all scanline groups, ignoring only zero-valued alignment pixels beyond the logical width (the original renderer trims trailing zero padding). Nonzero overflow, invalid masks and truncated rows still fail. This also fixes ANM frames. Independent audit of the supplied installed English retail corpus found 5,382 of 6,590 DTA terrain images and all 194 ANM frames rejected by the former check solely for zero padding; none contained nonzero overflow or invalid masks. Regression fixtures cover full and masked padded groups, invalid padding, invalid masks, truncation and the nested ANM path. Validation tab's Audit graphics coverage checks real image decoding for FSU, LZ/LZ0, DTA, ANM/PNM, ICO/BTN, CUR, GFK, CLK and FNT, separates recognized non-images from unknown and failed items, and logs individual asset names. The audit is a geometry/decode check, not visual or palette parity, and covers candidates only, not the whole game. Full C++ retail export still needs verification.

## 0.9.7 — Reloaded UI theme and layout

The converter uses the Reloaded strategic UI palette, grid-painted panels and owner-drawn buttons, with a matching installed Fixedsys face when available (monospace fallback). Main and preview navigation use real buttons and a wxSimplebook instead of native notebook tab strips. Preview toolbar and export controls use equal-cell grids; the splitter limits interactive resizing to keep the preview usable. Validation actions stay above a scrollable report, so long audit output no longer displaces them. Native file dialogs, choices, tree, scrollbars and system menu remain OS widgets. This is a converter-only visual port; no SR or Build.ps1 changes. Windows and Linux GUI behavior still needs visual QA.

## 0.9.8 — remaining retail graphics (first pass)

- Recognize the five verified DOS filename/identifier records in COMMON.FS as text; keep the original compressed bytes.
- Decode IBG_600, NAMELSTA, DIFFIC, GOTOLSTA, LED_OFF/ON and PATTERN using dimensions reconstructed from the English retail assets. Palette contexts and Czech visual parity are not yet verified.
- Decode the loose SPELCROS.ICO as a Windows single-entry indexed 8-bit DIB with its own RGB palette and AND transparency mask, separately from the native game ICO scanlines.
- Czech ISO-only assets (including MUSIC.FS and INSTALL.FS), ambiguous raw dimensions, INFOLSTA edition differences and DEVAST DMA0_270 remain open. No speculative classification or fabricated pixels.

## 0.9.9 — batch graphics evidence and shared resource families

- Use shared RAM1/RAM2 frame geometry across COMMON.FS and INSTALL.FS, strictly checking decoded byte lengths. The pattern texture remains unknown until its width is established. Preserve existing transparency conventions.
- Recognize validated MIDI/XMIDI content during the graphics audit regardless of extension; archive or filename alone is not evidence that a resource is music.
- Reject zero-height terrain headers explicitly; DMA0_270.DTA in the supplied English installation has 186 leading zero bytes and cannot be faithfully reconstructed from neighboring tiles. Original bytes remain preserved.
- After a completed graphics audit, optionally export one `graphics-audit.zip` for all unresolved records. It contains the coverage report, source names and status, original/decoded payloads (up to 64 MiB per record), relevant palette banks, decoded lengths, and candidate indexed dimensions. Candidate dimensions are diagnostic hypotheses, not successful image conversions. No full ISO is needed for subsequent analysis of these gaps.
- This is not a visual-parity test and does not claim complete Czech CD coverage. Remaining optional installer/media artwork and raw layouts must be verified against actual bytes before conversion.

## 0.9.10 — remaining Czech retail graphic geometry

The Czech ISO audit bundle from 0.9.9 reported 30,214 decoded graphics, 122 recognized non-images, 9 unknown and 2 failed records. The remaining decodable indexed images were reconstructed directly from the supplied decoded pixel arrays:

| Asset | Width × height | Notes |
|---|---|---|
| INFO.FS INFOLSTA.LZ | 433 × 64 | Keep original 433 × 63 variant |
| COMMON.FS DIFFIC.LZ | 249 × 180 | Localized Czech dialog; retain 249 × 152 variant |
| COMMON.FS IL_CISLA.LZ | 40 × 7 | Digits 0–9; one trailing zero byte retained in original |
| COMMON.FS JRC_LOGO.LZ | 640 × 480 | Matching JRC_LOGO.PAL |
| COMMON.FS / INSTALL.FS RAM1PAT.DTA | 85 × 79 | Texture geometry inferred from row continuity in both independently indexed copies |
| COMMON.FS RES_BAR.LZ | 260 × 16 | Reconstructed bar |
| INSTALL.FS BAR.LZ | 320 × 38 | Reconstructed installer bar |
| INSTALL.FS BTN_DOWN.LZ / BTN_UP.LZ | 20 × 20 | Installer buttons |

All ten decodable unresolved records now have geometry rules, with synthetic regressions for their exact lengths. DMA0_270.DTA still has a zero-height header and 186 leading zero bytes in the original Czech and English sources; it remains a reported failure and is preserved byte-for-byte, not fabricated or mislabeled as valid. Expected on the same Czech ISO: 30,224 graphics, 122 non-images, zero unknown, one failed, 31,325 frames. These are predicted audit totals, not a completed full-ISO run. Installer palette bytes were absent from the diagnostic bundle; color parity (including inferred contexts for other UI graphics) is not certified. Geometry coverage is not a blocker for subsequent PAK integration; damaged sources and optional unverified assets must remain explicit in the manifest.

## 0.9.11 — complete PAK export audit

The existing **Validate / audit PAK dataset** action now reads the actual ZIP/ZIP64 entries, validates the package and dataset contracts, and reports the status and recognized payload type of every record. Successful exports automatically write `pak-audit.txt` and `pak-audit.tsv` beside the root manifest; the standalone audit can save the same reports into a new directory without changing the existing dataset.

The audit separates modern-payload (recognized output representation, not runtime-certified), semantic-review, legacy-required, and preserved-container records. It identifies missing FSU layouts, animation and font metadata gaps, map/script/unit/class reference review, and raw/decoded/failed records. Physical uncompressed entry sizes are counted once per package, separately from ZIP file sizes and shared logical references. During conversion the read-back audit matches every selected source ID, name, format, category and stored size; independent validation without the original inventory cannot certify source completeness or byte parity. Duplicate IDs across packages are rejected. The status `runtime_ready: false` is unchanged. The audit is a diagnostic milestone, not proof that every payload is semantically complete or that the game is playable. See [development roadmap](docs/ROADMAP.md).
