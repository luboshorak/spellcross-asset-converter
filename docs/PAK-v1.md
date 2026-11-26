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
