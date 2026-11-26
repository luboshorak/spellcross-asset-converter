#pragma once
#include "spellcross/graphics.hpp"
namespace sc {
struct Pcm { int rate{},channels{}; std::vector<std::int16_t> samples; };
std::optional<Pcm> decode_sample(const Asset&,std::span<const std::uint8_t>);
std::optional<std::string> unit_definitions(const Asset&,std::span<const std::uint8_t>);
// Known small retail tables, retaining all original values and unknown tails.
struct RetailTable {std::string kind,json;};
std::optional<RetailTable> retail_table(const Asset&,std::span<const std::uint8_t>);
// Conservative classification of known DOS support files, never all raw files.
bool legacy_support_asset(const Asset&);
struct BitmapFont { std::vector<Sprite> glyphs; int first_code=32; int spacing=2; int default_height{}; };
std::optional<BitmapFont> decode_font(const Asset&,std::span<const std::uint8_t>);
}
