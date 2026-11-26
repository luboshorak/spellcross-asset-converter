#include "spellcross/pack.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace sc::pack {
std::string json(std::string_view s){
    std::ostringstream o;o<<'"';
    for(unsigned char c:s){
        if(c=='"'||c=='\\')o<<'\\'<<char(c);
        else if(c<32||c>=127)o<<"\\u00"<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(c)<<std::dec;
        else o<<char(c);
    }
    o<<'"';return o.str();
}
static std::string upper(std::string s){
    for(auto& c:s)if(c>='a'&&c<='z')c-='a'-'A';return s;
}
static std::string basename(std::string s){
    auto p=s.find_last_of("/\\:");return p==std::string::npos?s:s.substr(p+1);
}
std::string category(const Asset& a){
    auto b=upper(basename(block_name(a)));
    if(b=="UNITS.FSU")return "units";
    if(b=="T11.FS"||b=="PUST.FS"||b=="DEVAST.FS")return "terrain";
    if(b=="COMMON.FS")return "common";
    if(b=="INFO.FS")return "info";
    if(b=="RESEARCH.FS")return "research";
    if(b=="TEXTS.FS")return "localization";
    if(b=="SAMPLES.FS"||b=="MUSIC.FS")return "audio";
    if(b=="MOVIE.FS"||b=="SPEAKER.FS")return "movies";
    if(!b.empty())return "other";
    if(a.format=="CAN"||a.format=="SMK"||a.format=="DPK"||a.format=="DP2")return "movies";
    if(a.format=="WAV"||a.format=="MID"||a.format=="MIDI"||a.format=="XMI")return "audio";
    if(a.format=="DEF"||a.format=="DTA"||a.format=="TUT")return "campaign";
    return "other";
}
std::string group(const Asset& a){
    auto s=sprite_group(a);if(!s.empty())return s;
    if(a.format=="ANM"||a.format=="PNM")return a.name;
    auto b=block_name(a);return b.empty()?"loose/"+category(a):b;
}
std::string stable_id(const Asset& a,std::size_t occurrence){
    // Escaped logical name, with an explicit ordinal for duplicate names.
    // No host paths, physical ISO offsets or inventory-wide indexes.
    return a.name+"#"+std::to_string(occurrence);
}
Atlas::Atlas(int side):side_(side){
    if(side<1||side>4096)throw std::runtime_error("Invalid atlas page size");
    indices_.resize(std::size_t(side)*side);opacity_.resize(indices_.size());
}
std::optional<Rect> Atlas::add(const Sprite& s){
    if(s.width<=0||s.height<=0||s.width>side_||s.height>side_)return std::nullopt;
    auto n=std::size_t(s.width)*s.height;
    if(s.indices.size()!=n||s.opaque.size()!=n)throw std::runtime_error("Invalid sprite planes");
    int x=x_,y=y_,row=row_;
    if(x+s.width>side_){x=0;y+=row;row=0;}
    if(y+s.height>side_)return std::nullopt;
    Rect r{x,y,s.width,s.height};
    for(int j=0;j<s.height;++j){
        auto src=std::size_t(j)*s.width,dst=std::size_t(y+j)*side_+x;
        std::copy_n(s.indices.begin()+src,s.width,indices_.begin()+dst);
        std::copy_n(s.opaque.begin()+src,s.width,opacity_.begin()+dst);
    }
    used_w_=std::max(used_w_,x+s.width);used_h_=std::max(used_h_,y+s.height);
    x_=x+s.width;y_=y;row_=std::max(row,s.height);
    return r;
}
static Bytes crop(const Bytes& data,int stride,int w,int h){
    Bytes out(std::size_t(w)*h);
    for(int y=0;y<h;++y)std::copy_n(data.begin()+std::size_t(y)*stride,w,out.begin()+std::size_t(y)*w);
    return out;
}
Bytes Atlas::indices()const{return crop(indices_,side_,used_w_,used_h_);}
Bytes Atlas::opacity()const{return crop(opacity_,side_,used_w_,used_h_);}
std::size_t PaletteTable::intern(const Palette& p){
    for(std::size_t i=0;i<values.size();++i)
        if(values[i].rgb==p.rgb&&values[i].known==p.known)return i;
    values.push_back(p);return values.size()-1;
}
}
