#pragma once
#include "spellcross/graphics.hpp"
namespace sc {
// Ordered syntax representation, not a second implementation of game rules.
// Original arguments remain strings (leading zeroes, %, names, '-' etc.).
std::optional<std::string> script_definition(const Asset&,std::span<const std::uint8_t>,bool utf8=false);
std::optional<std::string> class_definition(const Asset&,std::span<const std::uint8_t>);
std::optional<std::string> quick_help(const Asset&,std::span<const std::uint8_t>);
struct MapPayload {
    std::string metadata;
    Bytes ground,objects;
};
std::optional<MapPayload> tactical_map(const Asset&,std::span<const std::uint8_t>);
}
