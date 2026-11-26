#pragma once
#include "spellcross/graphics.hpp"
namespace sc {
enum class MusicKind {none,midi,xmidi};
MusicKind music_kind(std::span<const std::uint8_t>);
void validate_midi(std::span<const std::uint8_t>);
// Container-level preflight before running the isolated legacy converter.
void validate_xmidi(std::span<const std::uint8_t>);
}
