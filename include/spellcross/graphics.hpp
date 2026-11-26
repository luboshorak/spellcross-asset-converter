#pragma once
#include "spellcross/inventory.hpp"
#include <array>
#include <span>

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
};
Sprite decode_sprite(std::span<const std::uint8_t> data);
struct Palette {
    std::array<std::uint8_t,768> rgb{};
    std::array<bool,256> known{};
};
// Default tactical palette (first MAP.PAL bank in catalogue, then units/system).
// Unknown entries remain explicitly marked and shown as grayscale.
Palette tactical_palette(const Inventory&);
}
