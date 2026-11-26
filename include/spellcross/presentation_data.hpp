#pragma once
#include "spellcross/graphics.hpp"
#include <string_view>
namespace sc {
struct AnimationLayout {unsigned static_azimuths{},static_slopes{},animation_azimuths{},animation_slopes{},frames{};std::vector<std::vector<std::size_t>> stat,anim;};
AnimationLayout fsu_layout(std::string_view group,const std::vector<std::string>& names);
std::string language_for(const Asset&,std::string_view edition);
std::string edition_language(const Inventory&);
std::optional<std::vector<Bytes>> language_table(const Asset&,std::span<const std::uint8_t>);
Bytes narrative_display(std::span<const std::uint8_t>);
}
