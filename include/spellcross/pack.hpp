#pragma once
#include "spellcross/graphics.hpp"
#include <map>
namespace sc::pack {
inline constexpr int schema_version=1;
std::string json(std::string_view);
std::string category(const Asset&);
std::string group(const Asset&);
std::string stable_id(const Asset&,std::size_t occurrence);
struct Rect {int x{},y{},width{},height{};};
// One bounded page, row-packed without resampling or rotation. The index and
// opacity planes have identical geometry and contain exact original values.
class Atlas {
    int side_,x_{},y_{},row_{},used_w_{},used_h_{};
    Bytes indices_,opacity_;
public:
    explicit Atlas(int side=2048);
    std::optional<Rect> add(const Sprite&);
    bool empty()const{return used_h_==0;}
    int width()const{return used_w_;}
    int height()const{return used_h_;}
    Bytes indices()const;
    Bytes opacity()const;
};
struct PaletteTable {
    std::vector<Palette> values;
    std::size_t intern(const Palette&);
};
}
