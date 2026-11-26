#pragma once
#include "spellcross/inventory.hpp"
#include <array>
#include <span>
#include <optional>

namespace sc {
using Bytes=std::vector<std::uint8_t>;
// Spellcross's MSB-first variant; preserves the previous phrase on dictionary
// reset, and follows the retail reader's final-byte termination convention.
Bytes decode_lz(std::span<const std::uint8_t> data,std::size_t max_output=64*1024*1024);
Bytes decode_asset(const Asset& asset,std::size_t max_output=64*1024*1024);
struct Sprite {
    int width{},height{},x{},y{};
    Bytes indices,opaque; // original index 0 can be opaque; index 253 is shadow
    std::size_t trailing_bytes{};
    bool shadow_index{true};
    bool opaque_zero_uses_254{}; // ICO/BTN display convention; retain original indices
};
Sprite decode_sprite(std::span<const std::uint8_t> data);
struct Palette {
    std::array<std::uint8_t,768> rgb{};
    std::array<bool,256> known{};
};
// Default tactical palette (first MAP.PAL bank in catalogue, then units/system).
// Unknown entries remain explicitly marked and shown as grayscale.
Palette tactical_palette(const Inventory&);
Palette asset_palette(const Inventory&,const Asset&,const Palette&);
// Export-only index, retaining original inventory order and selection rules.
Inventory palette_inventory(const Inventory&);
// A narrowly recognized damaged retail asset receives a synthetic transparent
// sentinel, never a successful decode or a claim of original geometry.
std::optional<Sprite> damaged_terrain_placeholder(const Asset&,std::span<const std::uint8_t>);
std::optional<Sprite> decode_image(const Asset&,std::span<const std::uint8_t>);
// Diagnostic-only raw dimension candidates; never used to mark graphics decoded.
std::string graphics_evidence(std::span<const std::uint8_t>);
bool compressed(const Asset&);
bool is_text_asset(const Asset&,std::span<const std::uint8_t>);
std::string block_name(const Asset&);
// Virtual browser nodes: only a bounded decode/slice recipe is retained,
// never a permanent copy of each decompressed asset.
struct LayerStep {
    bool decompress{};
    std::size_t offset{},size{};
    std::string label,format,name;
};
using LayerPath=std::vector<LayerStep>;
bool can_expand(const Asset&,const LayerPath&);
Bytes layer_bytes(const Asset&,const LayerPath&,std::size_t limit=64*1024*1024);
std::vector<LayerStep> layer_children(const Asset&,const LayerPath&);
std::vector<LayerStep> animation_frames(const Asset&,std::span<const std::uint8_t>);
std::optional<Sprite> layer_image(const Asset&,const LayerPath&,std::span<const std::uint8_t>);
bool layer_text(const Asset&,const LayerPath&,std::span<const std::uint8_t>);
// Sidecar metadata for lossless sprite reconstruction. The index and mask
// images store original values independently of the presentation palette.
std::string sprite_metadata(const Asset&, const Sprite&, const Palette&);
std::string sprite_group(const Asset&);
std::vector<std::size_t> sprite_group_members(const Inventory&,const Asset&);
}
