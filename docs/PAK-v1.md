# Spellcross modern data pack v1 (draft runtime contract)

A .pak is a conventional ZIP/ZIP64 archive. The root manifest.json in the output
directory identifies packages and schema version. Packages are grouped by logical
category, not by original archive. Single-source-block conversions are also valid
partial datasets and are explicitly marked partial.

Each package contains manifest.json with a record per inventoried item. Logical
names retain the original path including :: separators. id is logical name plus
a duplicate ordinal; IDs do not incorporate physical ISO positions. JSON strings
escape non-ASCII source-name bytes as U+00xx (byte-preserving, not an assertion
of the original filename encoding). Text payloads are UTF-8. Numeric entry paths
are internal locators, not public IDs.

Graphics: one or more bounded atlas pages per logical group, with two PNG planes.
Each plane is a grayscale-value image (RGB channels identical, not palette RGB);
the exact 8-bit values are the contract. indices is the source palette index;
opacity is the independent source mask. No rotation, scaling or color transforms.
Rectangles use top-left coordinates in pixels. The loader must not color-manage,
premultiply, interpolate or gamma-transform these data planes. Each frame has its
source ID, source frame ordinal, rectangle, signed origin, shadow flag, palette
reference and trailing byte count. Shared palettes are 768-byte RGB tables plus
256-byte known-entry masks. PNG compression is lossless; unknown palette entries
must not be treated as authoritative. Atlas arrangement is a storage detail, not
animation timing or azimuth inference. Original containers preserve any unparsed
animation/terrain metadata, padding and ordering.

For source preservation, original FS/FSU containers are retained once, and
members record their relative offset and stored length. Unconverted/failed
members also have standalone original bytes for direct fallback. Loose assets
have their own original bytes. Known and unknown data are never silently dropped.
The manifest distinguishes converted, decoded, raw, failed and container.
Container and raw records do not imply modern runtime support. Converted graphics
mean pixel/geometry conversion only, not complete gameplay semantics.

This is a draft runtime contract: no loader in SR yet. Binary definitions, map
layers, full animation semantics, fonts, sound/video conversion, and unparsed
nested containers remain open. No original commercial resources are distributed
with the converter.

### Semantic extensions (0.7.0)

WAV payloads contain signed little-endian PCM16 with original sample rate and channel count; no resampling. Unit definitions have schema spellcross-units and original_hex per record, including unmapped bytes. Bitmap-font records point to font.json, which references the atlas by path and record ID; frame ordinal is original byte-code position, including generated control and space glyphs. Glyph index 1 means foreground, 0 background. Glyph width is the atlas rectangle width; spacing is separate. Coverage counts in the root manifest are per category/format and status; containers are not counted as converted assets. An asset marked converted need not yet have all runtime semantics mapped.

### Campaign extensions (0.8.0)

See [campaign-v1.md](campaign-v1.md) for the ordered script representation and eight-layer tactical map payload. Map binary paths in map.json are relative to that record's directory. Existing original-container ownership is retained (e.g. COMMON.FS maps and scripts live in common.pak); moving them across packages without duplicating source bytes requires a later cross-package reference contract. Unknown script commands are preserved without interpretation. The runtime_ready field remains false.

### Multimedia extensions (0.9.0)

Music payload music.mid is Standard MIDI (validated original or XMIDI converted using upstream mode 0); original XMI is retained, including any unsupported Miles-specific semantics. Movie records reference movie.json, which points to movie.mkv in the same record directory. FFV1 RGB and FLAC PCM are lossless relative to decoded frames and samples; the exporter checks both via FFmpeg round-trip. Source FPS and frame count are recorded; Matroska timestamp quantization and original playback clock are not yet independently verified. Original palette-index video and proprietary stream bytes remain in the original container. Missing companion DP2 audio or failed verification prevents converted status.

### Read-back validation (0.9.1)

Generated PAKs must pass ZIP CRC and length checks, manifest/schema and internal reference checks, PNG plane validation and aggregate record/atlas accounting. Validation does not imply runtime_ready, original-source identity, or resolved gameplay references. CRC32 detects accidental corruption, not deliberate tampering. External datasets can be checked without modifying them.

### FSU layout and language extensions (0.9.2)

`fsu_layout` inside atlas JSON identifies source-order static rows by slope/azimuth and animated rows by azimuth or (DPEKJ) slope, with logical record IDs, original grouping counts and 90-degree clockwise azimuth convention. It is not a timing or fire-origin table. Incomplete groups report `fsu_layout_error`. Language table JSON contains explicit zero-based entry IDs, UTF-8 text and language; original table bytes retained. Narrative UTF-8 follows SR display normalization, not a byte-identical transcription. CLK atlas planes encode territory IDs rather than RGB colors.

### Installer source nesting (0.9.3)

Source names may include multiple `::` delimiters, e.g. `DATA/INSTALL.DTA::DATA/COMMON.FS::LEVEL_02.DEF`. The nearest FS/FSU owns its package/block. INSTALL.DTA itself is retained in other.pak and nested archives in their own packages; original offsets in each package refer to the preserved container entry (not the whole ISO). Direct installer files are classified as loose assets. Selecting an installer includes its complete descendant subtree, not CD-side assets; select All game data for the full CD. IDs retain source provenance and can differ between an installed directory and an ISO input. Original bytes may be duplicated across package boundaries. No game loader changes.

### Graphics correction (0.9.4)

ICO/BTN frames optionally declare `opaque_zero_display_index:254`; original pixels stay unchanged and only an opaque zero is displayed with this palette entry. PNM/ANM source offsets are based at byte 1 of the animation container. SELECT LZ0 resources are indexed masked sprites. BML/RTA dimensions inferred from retail data; retain source bytes for verification.

### Contextual preview palettes (0.9.5)

Palette assignment is per resource use: strategic shared chrome, page-specific hierarchy/units/options bank, tactical INFO.PALETA.NRM, tactical terrain. The assigned palette is a default presentation context, not a restriction on how the same indexed resource can be drawn. Original index/mask planes remain unchanged.
