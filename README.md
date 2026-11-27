# Spellcross Asset Converter

Standalone desktop toolkit for browsing, extracting and converting original Spellcross game assets into modern PAK v1 datasets. C++20, wxWidgets and CMake.

**Version 1.0.0** — the standalone conversion contract is complete for the tested Czech and English retail editions. Original game data is not included. Spellcross Reloaded does not yet consume these modern datasets natively.

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

One damaged original terrain asset (DMA0_270.DTA) in each edition is explicitly represented by a transparent placeholder, with source bytes preserved. EN has no retail music archive; CZ has no QH context help. Missing original links, missing debrief text, optional FSA origins and unverified animation timing are reported, not invented or repaired. Runtime readiness remains false. These acceptance results describe the tested input editions, not every possible release or mod. No gameplay or rendering parity certification is claimed.

## Read the modern data

The standard-library Python example reads the catalog and modern payloads without FS/FSU/LZ decoders:

```powershell
python examples/pak_reader.py "C:\Games\Spellcross-PAK-1" unit 0
python examples/pak_reader.py "C:\Games\Spellcross-PAK-1" tile M01_01.DTA 0 0
python examples/pak_reader.py "C:\Games\Spellcross-PAK-1" group ABRMS
```

Use exact IDs or full group names where aliases collide. This is a read-only reference consumer, not a game engine.

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
