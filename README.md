# Spellcross Asset Converter

Standalone desktop toolkit for browsing, extracting and converting original Spellcross game assets into modern PAK v1 datasets. C++20, wxWidgets and CMake.

**Version 1.0.0** — the standalone conversion contract is complete for the tested Czech and English retail editions. Original game data is not included. **Spellcross Reloaded can now load and use the generated PAK v1 datasets directly in its experimental native PAK runtime.** The converter is both a standalone asset toolkit and a data preparation tool for this new game runtime.

## What it does

- Read an original installed folder, mounted CD, conventional ISO 9660 image (2048-byte sectors), or FS/FSU archive.
- Browse original assets and decoded layers; preview graphics, text, sounds and supported video.
- Export modern ZIP64 PAK packages: lossless indexed PNG atlases with separate opacity and palette tables, structured JSON units/maps/scripts/localization, PCM WAV, MIDI and FFV1/FLAC MKV.
- Preserve original source entries, including archival resources and damaged assets. Check every preserved physical entry against the selected input byte-for-byte during export read-back.
- Validate the global resource catalog, map data, known references, and manifest version consistency.

## Quick start

1. Open your own original game CD/ISO or installed game folder.
2. Select **All game data** in Extraction / Conversion and run **Convert to modern PAK**.
3. Choose an output directory. FFmpeg is provisioned for supported Windows x64 builds; other builds require a compatible FFmpeg installation for movie conversion.
4. Keep the generated numbered Spellcross-PAK directory together. Its root manifest, all .pak files, resources.json and game-metadata.json constitute the dataset.
5. Review pak-audit.txt, pak-audit.tsv, game-references.tsv and pak-performance.txt. The Validate PAK action can check a completed dataset without the original source, but cannot independently compare source bytes in that mode.

## Play with Spellcross Reloaded

Native PAK support is available in the dedicated [experiment/native-pak-runtime branch](https://github.com/luboshorak/spellcross-reloaded/tree/experiment/native-pak-runtime) of Spellcross Reloaded. It is an experimental game runtime, not just an asset viewer or a proof-of-concept reader, and is separate from the stable releases.

1. Convert your own Czech or English retail game data using **All game data → Convert to modern PAK** as described above.
2. Validate the output and keep the entire generated dataset directory together.
3. Build or obtain a build of Spellcross Reloaded from the experimental branch, with its required runtime libraries and project-owned helper files.
4. In the startup data wizard, select **Use a native Spellcross PAK dataset (experimental)** and choose the directory containing `manifest.json` and `resources.json`. Do not select an individual `.pak` file.

Alternatively, launch the experimental build directly (adjust the path to your export):

```powershell
.\Spellcross.exe --native-pak "C:\Games\Spellcross-PAK-1" --no-update-check
```

To change an existing legacy game-data selection, launch with `--select-game-data`.

The native runtime reads modern payloads directly from the PAK dataset: indexed graphics and palettes, terrain and unit sprites, fonts, localized texts, sound and music, and FFV1/FLAC MKV videos. Native routes are implemented for the main menu, strategic campaign data, research and upgrades, mission dispatch and tactical map loading. Original FS/FSU entries preserved inside the packages are archival only in this mode; there is no extraction back to legacy archives or silent fallback to the original game data.

**Compatibility status:** PAK v1 is the shared conversion contract. Native gameplay is available experimentally; full campaign, save/load compatibility, rendering and gameplay parity still require runtime acceptance with real CZ/EN datasets. The manifest's `runtime_ready=false` flag is expected under the current contract and does not prevent use by the experimental runtime. Stable Reloaded releases are not assumed to support native PAK loading.

For technical details, build requirements and limitations, see [Native PAK runtime experiment](https://github.com/luboshorak/spellcross-reloaded/blob/experiment/native-pak-runtime/docs/NATIVE_PAK_EXPERIMENT.md).

## Retail acceptance (0.9.15)

Both full owner-supplied export audits passed the source-byte, inventory, structure, catalog and game-metadata checks. Windows and Linux CI passed.

| Metric | CZ | EN |
|---|---:|---:|
| Records | 32,643 | 33,291 |
| Converted | 32,576 | 33,232 |
| Atlas frames | 31,332 | 32,288 |
| Unresolved formats | 0 | 0 |
| Unreviewed script commands | 0 | 0 |
| Known references | 52,616 | 40,332 |
| Export time (owner machine) | 96.295 s | 69.756 s |

One damaged original terrain asset (DMA0_270.DTA) in each edition is explicitly represented by a transparent placeholder, with source bytes preserved. EN has no retail music archive; CZ has no QH context help. Missing original links, missing debrief text, optional FSA origins and unverified animation timing are reported, not invented or repaired. The export manifest retains `runtime_ready=false` as part of the current PAK v1 contract; this historical conversion audit is not a gameplay acceptance test of the experimental Reloaded runtime. These acceptance results describe the tested input editions, not every possible release or mod. No gameplay or rendering parity certification is claimed.

## Read the modern data

The standard-library Python example reads the catalog and modern payloads without FS/FSU/LZ decoders:

```powershell
python examples/pak_reader.py "C:\Games\Spellcross-PAK-1" unit 0
python examples/pak_reader.py "C:\Games\Spellcross-PAK-1" tile M01_01.DTA 0 0
python examples/pak_reader.py "C:\Games\Spellcross-PAK-1" group ABRMS
```

Use exact IDs or full group names where aliases collide. This is a read-only reference consumer, not a game engine. To run the game using the same modern dataset, use the experimental Spellcross Reloaded runtime described above.

## Build

C++20 compiler, CMake 3.20+, wxWidgets 3.2 (core/base/adv). Windows CI uses MSYS2 UCRT64; Ubuntu dependencies include build-essential, cmake, ninja-build, libwxgtk3.2-dev and ffmpeg.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Launch SpellcrossConverter with the required runtime libraries available. For core-only development use -DSC_CONVERTER_GUI=OFF. FFmpeg bundling on supported Windows builds can be disabled with -DSC_BUNDLE_FFMPEG=OFF.

## Documentation

- [PAK v1 specification](docs/PAK-v1.md)
- [Campaign and tactical maps](docs/campaign-v1.md)
- [Semantic assets](docs/semantic-assets.md)
- [Development roadmap and acceptance](docs/ROADMAP.md)
- [Development history](docs/CHANGELOG.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)

MIT license for project code; third-party components retain their own licenses. See notices. No original commercial assets are distributed.

The project builds on Spellcross restoration work by Stanislav Mašláň and Spellcross Reloaded by Luboš Horák.
