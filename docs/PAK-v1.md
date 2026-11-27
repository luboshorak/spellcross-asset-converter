# Spellcross modern data pack v1 (standalone export contract)

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

This is a standalone export contract, not certification of game behavior.
Unit definitions, map layers, ordered scripts, fonts, audio and video have
modern payloads as described below. Animation timing, engine interpretation,
and visual parity remain open. No native PAK loader in SR is provided by this
project. No original commercial resources are distributed with the converter.

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

### Retail tables and archival roles (0.9.12)

PAK v1 keeps the original five record statuses for compatibility. An optional `role: "legacy-only"` on a raw record identifies explicitly recognized DOS executables, sound drivers and installation support resources; it does not discard bytes or imply all raw assets are optional. The audit separately counts legacy-only, placeholder (known damaged terrain with a synthetic frame), and unresolved legacy-required. `runtime_ready` remains false.

`records/N/retail-table.json` uses schema `spellcross-retail-table`, version 1. Supported kinds: territory-adjacency (32 unsigned little-endian 32-bit masks, redundant zero-based bit index lists), gamma-table (11 rows of 64 values in 0..63), terrain-color-indices (11 exact bytes; slot semantics unverified), info-rgb-palette (768 exact bytes, role normal), info-index-filter (256 exact bytes, role dark or light), and empty-palette (empty values, original CRLF bytes). No inferred palette values or adjacency edges are added. Read-back checks shape, ranges, identity, source length and mask/list agreement. Original-byte parity is not independently verified. Original archive ownership and stable IDs are unchanged pending cross-package reference design.

### Known archival exceptions (0.9.12.1)

`legacy-only` includes the owner-approved COMMON.FS::BOXER.LST (225 bytes) and MOVIE.FS::TEMP.DPK (525 bytes). Classification checks container, filename, format and length on export and read-back. The latter is a deliberate archival policy, not a proof of absent game references. Both remain byte-preserved raw records with no modern payload. MAP files with valid tactical-map headers use the existing eight-layer map schema; TEMPFILE.MAP is converted, not discarded. No runtime-ready certification follows from this policy.

### Global resource catalog (0.9.12.2)

New exports declare `"resource_catalog":"resources.json"` in the dataset manifest. The sidecar uses schema `spellcross-resource-catalog`, version 1. It is generated from verified package manifests, not the pre-conversion source inventory. It records the dataset partial flag and edition language when present.

- `records` is sorted by exact stable ID. Each record identifies its owner package and category, logical name, format, status, payload kind and audit assessment, original entry/offset/size, optional standalone fallback, optional primary payload and atlas entry/frame count. All entry names and ranges are local to the named owner package. Frame ordinals remain in the atlas manifest; use the record ID to select frames. The catalog does not copy asset bytes.
- `by_name` maps normalized full logical names to arrays of exact record IDs. `by_leaf` maps normalized final components to arrays of IDs. Normalization is ASCII uppercase plus backslash-to-forward-slash, without Unicode case folding or removal of provenance. Never choose the first candidate silently; filter by expected category, format and provenance, and report ambiguity if more than one remains. Exact IDs are case-sensitive and retain the original duplicate ordinal. IDs are not promised identical across different installation/ISO source layouts.
- `atlas_groups` lists each verified atlas manifest with group name, owning package, entry, frame count and whether a validated FSU layout exists. `by_group` and `by_group_leaf` map normalized group names to arrays of zero-based indexes into this list, not record IDs. This supports whole unit resource lookup rather than ambiguous frame basenames such as A00. Index positions are local to this catalog, not stable identifiers.
- All records are indexed, including archival, failed, and synthetic placeholder records. Presence in the catalog is not proof of runtime suitability. Use status and assessment when resolving. There is no automatic conversion of unresolved game references yet.

Read-back validation regenerates the catalog from all verified package manifests and compares parsed JSON values, rejecting stale/missing/tampered entries, altered owners, aliases or locators, and duplicate global IDs. Whitespace and object-key order are immaterial; ordered arrays are part of the contract. Declared catalog paths must be exactly resources.json. Older v1 datasets without this declaration are accepted and the audit can reconstruct a catalog without altering the input dataset.

Physical ownership remains in the existing source-container-based PAKs. Shared atlas entries and original container ranges are referenced in place; no additional copies are required. Campaign scripts/maps can therefore remain in common.pak and refer to units/terrain/audio via the catalog once semantic reference rules are defined. Cross-package duplication of historical INSTALL.DTA/contained FS data is still retained intentionally; this change does not redesign original-range storage. Cross-resource links, playback and gameplay parity remain uncertified, and runtime_ready remains false.

### Game metadata and reference audit (combined 0.9.13 / 0.9.14)

The dataset manifest may declare `"game_metadata":"game-metadata.json"`. New exports use this extension; existing PAKs can be read and audited without changing the source dataset. The sidecar schema is `spellcross-game-metadata`, version 1. It is built from CRC-checked PAK payloads and verified again on read-back; it is not a loader, executable script engine or runtime certification. The authoritative source record IDs and package/payload locators remain in resources.json.

- Unit JSON retains the complete original record hex and all existing decoded fields. Explicit class/movement and known fire/projectile bit semantics are additive, and uncertain bits/bytes are reported separately. Read-back compares mapped fields against the embedded exact record bytes using the edition-aware decoder. This does not independently compare the original ISO.
- Optional user-supplied `units.fsa` in a scanned source directory (editor context, not ordinary retail ISO) uses the bounded SpellFSUcontextV1.0 parser, signed coordinate pairs, per-slope static fire origins and centers, and animated fire origins and center. It is exported as fsu-context.json and checked against the FSU group layout shape. No supplied file or origin is synthesized. Each FSU group explicitly records supplied-shape-match, not-supplied, ambiguous, layout-unavailable or shape-review; animation timing remains unverified.
- Tactical map read-back retains bounded ground/object arrays while streaming ZIP data; checks real tile indices, dimensions, encoding, local sprite/animation/sound indices, posts and sound placements. Map references use the exact terrain archive scope.
- `definitions` enumerates numeric or symbolic IDs in unit/class/research/upgrade/experience/formation/commander/music tables and chapter-local territory/mission/event definitions. `script_blocks` retains ordered blocks/calls, line numbers, source args and typed decimal integer args where applicable; repeated calls remain ordered, ORconnections=any and WaitForResearch=all are distinct. Original ordered syntax remains authoritative, including lines not mapped into the semantic view. Unknown commands are counted by domain and keyword.
- `references` contains source record ID, field/line selector, expected kind, scope, original target, outcome and all candidate locators. Definition candidates identify owning record ID plus table ID; atlas-group candidates identify package and group/manifest entry. Resource candidates identify package, record ID and payload. Resolution is scoped by archive/definition set and ASCII case normalization; filename stems are allowed only for applicable asset families. Full/partial dataset distinction is explicit. Never silently choose the first of multiple candidates or claim that a missing link in a partial selection is a corrupt original. Missing/ambiguous/unsupported/frame-limit-review/context-shape-review findings are reported, not fatal to structurally intact datasets. Placeholder and optional-empty are separate outcomes.
- `game-references.tsv` is a diagnostic projection of the same edges, including source, field, kind, scope, target, result and candidate locators. The JSON sidecar is machine-readable. `pak-audit.txt` summarizes coverage, outcomes and unreviewed commands. Retain all original content and continue to declare runtime_ready:false until complete semantics and real CZ/EN parity are established.

Implemented reference rules cover unit FSU groups and animation frame limits, PNM, icon/projectile/info graphics, sound classes and transform/create action unit IDs; class sample and object-sound links; tactical terrain sprites, ANM/PNM and ambient samples; campaign map/mission/event/territory, unit, research, upgrade, music, video, text and tutorial links for known commands. Resource and data structures not covered by these rules are not certified. Timing, original fire origins without FSA, uncertain flags/bytes, font encoding, terrain color slot meaning, objective/event execution and other unreviewed commands remain explicit metadata gaps. This is the first integrated implementation of the two milestones, not a declaration that all retail semantics have been solved.

#### FSA retail-reference compatibility (0.9.14.1)

The editor's p16 string prefix includes the final NUL byte. Strip exactly one optional terminal NUL before validating and indexing the name; reject empty, embedded-NUL, multiple-terminator and duplicate case-insensitive names. Nonterminated earlier fixtures remain readable. Preserve signed coordinate values without interpreting zeros as invalid or missing. Report actual origin/center counts, zero counts and per-group editor-supplied provenance; a matching FSU shape is not proof of original DOS coordinate parity. The uploaded reference (99,144 bytes, 137 groups, 11,040 origin pairs and 766 center pairs) passed an isolated C++ decoder check and an independent coordinate-by-coordinate comparison. It is not bundled or redistributed. Full converter CI remains required.

### 0.9.14.2 script and reference audit refinement

GUI export parses recognized ordered scripts using the selected retail text encoding (CP895 for Czech auto), then serializes actual UTF-8. `source_encoding: "UTF-8"` identifies that representation; old byte-preserving syntax is still accepted by the parser, and the original source ranges remain unchanged. The structured script and companion text use the same decoder. New audit rules review objective and special-event shapes (including 0–100% probability and scoped EventData references), zero-argument NightMission, and signed upgrade Range. Objective instance IDs and linear tile positions remain original values; runtime semantics and bounds are not certified by these shape checks. Static-only azimuth sets referenced by single-frame attacks receive `static-frame-reference`, not a fabricated animated sequence. Missing mission outcome texts receive `missing-end-text` only for full selections; missing definitions and other missing resources remain visible. Missing reference rows have a diagnosis comparing the exported original inventory with the typed index, not claiming independent source-byte parity. The TSV diagnosis column is additive. Re-export older datasets to obtain the updated deterministic game metadata sidecar; a stale sidecar will fail canonical comparison. No source values are patched.

### 0.9.14.3 English retail extensions

Indexed one-plane 8-bit RLE PCX uses original indices, fully opaque mask and its embedded 256-color palette. RAD00–49 use 78×76 indexed arrays, EN INSTALL.FS BAR uses 342×32 (CZ remains 320×38); MASK is a 604×272 binary-valued indexed plane (0/255), not a newly invented alpha mask. Geometry reconstructed from actual decoded pixel arrays; installer palette remains uncertain if no unambiguous original palette is available. QH records use `spellcross-quick-help` version 1 with screen [640,480], ordered regions {rect:[x,y,w,h],text} and region_count; bounds verified, original text bytes preserved. CHEATS.LZ text is retained in source order without reversing its encoded strings. Zero-length BAD in TEXTS.FS is an intentionally empty converted text resource. Explicit installer archival records remain source-only. EN full datasets with no MUSIC.FS inventory get `edition-absent` for missing music track references; a present archive or partial selection retains normal missing rules. CZ is not required to contain QH. Tutorial and slideshow directives retain original arguments; local event IDs and slideshow image links checked, execution/timing not certified. Re-export old sidecars for changed audit rules.

### 0.9.15 acceptance contract and independent reader

The root and every package manifest now carry `converter_version`, taken from the CMake project version. This is independent of the schema version (still 1). A stamped dataset rejects packages with a different or absent converter version. Unstamped v1 datasets remain readable under the existing rules. Re-export rather than mixing package files from different runs.

During a new export, the converter reopens the input and compares every physically stored original entry byte-for-byte during streaming ZIP read-back; each logical record's range must point to the exact original backing file and offset of the selected scan record. Fallback entries must equal the whole corresponding source asset. A source change, missing entry, mismapped range or byte difference fails the export. This checks selected asset bytes, not unused sectors of the entire CD, and does not prove that every derived pixel or rule has identical game behavior. Standalone validation without the original source only checks the archive and metadata; the audit explicitly records that limitation.

A native consumer starts at manifest.json, resolves exact logical IDs via resources.json, opens the declared package, and reads only modern payloads. Ambiguous leaf names are not silently resolved. Atlas indices and opacity must be decoded as data planes; use the palette table rather than treating indices as RGB. Preserve original bytes for archival recovery, but never rely on them as a hidden prerequisite for a modern loader. See examples/pak_reader.py for a read-only stdlib example and tests/test_pak_reader.py for its synthetic native-only fixture. This example does not render the game or replace full validation. Runtime readiness remains false until a real engine has demonstrated gameplay parity.

### 1.0.0 standalone release

PAK schema version remains 1. No changes to the payload contract or conversion algorithms from 0.9.15. The full Czech and English acceptance exports at 0.9.15 reported successful source comparison and zero unresolved formats or unreviewed script commands. See ROADMAP.md for measured counts and qualifications. A complete validated 0.9.15 dataset does not require re-export solely because the converter version is now 1.0.0. Do not mix package versions. The runtime_ready field remains false; this release defines a standalone export contract, not a playable replacement or certified game engine.
