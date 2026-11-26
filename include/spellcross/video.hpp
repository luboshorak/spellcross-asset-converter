#pragma once
#include "spellcross/graphics.hpp"
namespace sc {
struct Video {
    int width{},height{};
    double fps{};
    std::vector<Bytes> frames;
    std::vector<std::array<std::uint8_t,768>> palettes;
    Bytes wave;
    std::string audio_note;
};
bool is_video(const Asset&);
Video decode_video(const Inventory&,const Asset&);
Bytes pcm_wave(std::span<const std::int16_t>,int rate,int channels);
}
