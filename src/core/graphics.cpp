// LZ and FSU scanline layout based on the MIT-licensed Spellcross Map Editor
// by Stanislav Maslan (2021-2025). Bounded implementation; no unaligned reads.
#include "spellcross/graphics.hpp"
#include <algorithm>
#include <stdexcept>
#include <limits>
#include <sstream>
#include <iomanip>
#include <string_view>
namespace sc {
Bytes decode_lz(std::span<const std::uint8_t> data,std::size_t max_output) {
    if(data.size()<3)throw std::runtime_error("Truncated LZ header");
    const unsigned n=unsigned(data[0])|(unsigned(data[1])<<8), initial=data[2];
    if(n==0||n>256||initial<1||initial>12||n+1>=(1u<<initial))
        throw std::runtime_error("Invalid LZ dictionary header");
    const std::size_t header=3+(n==256?0:n);
    if(data.size()<=header)throw std::runtime_error("Empty LZ bitstream");
    std::vector<Bytes> dict;
    dict.reserve(4096);
    for(unsigned i=0;i<n;++i)dict.push_back(Bytes{static_cast<std::uint8_t>(n==256?i:data[3+i])});
    Bytes prev,out;
    unsigned width=initial;
    std::size_t pos=header*8;
    const auto total=data.size()*8;
    while(width<=12 && pos+width<total) {
        unsigned code=0;
        for(unsigned j=0;j<width;++j,++pos)
            code=(code<<1)|((data[pos/8]>>(7-pos%8))&1);
        if(code==n) {dict.resize(n);width=initial;continue;}
        Bytes word;
        if(code<dict.size())word=dict[code];
        else if(code==dict.size()&&!prev.empty()){word=prev;word.push_back(prev.front());}
        else throw std::runtime_error("Invalid LZ dictionary reference");
        if(word.empty()||dict.size()>=4096)throw std::runtime_error("LZ dictionary overflow");
        if(word.size()>max_output-out.size())throw std::runtime_error("LZ output limit exceeded");
        Bytes next=prev;next.push_back(word.front());
        if(next.size()>4096)throw std::runtime_error("LZ phrase limit exceeded");
        dict.push_back(std::move(next));
        out.insert(out.end(),word.begin(),word.end());
        prev=std::move(word);
        if(dict.size()+1 >= (1u<<width))++width;
        if(width>12 && pos+12<total)throw std::runtime_error("LZ dictionary missing reset");
    }
    if(out.empty())throw std::runtime_error("Empty decoded LZ stream");
    return out;
}
Bytes decode_asset(const Asset& a,std::size_t max_output) {
    if(a.size>max_output)throw std::runtime_error("Asset exceeds decode limit");
    auto data=preview(a,static_cast<std::size_t>(a.size));
    if(a.format=="LZ"||a.format=="LZ0"||a.format=="FSU-SPRITE")return decode_lz(data,max_output);
    return data;
}
Sprite decode_sprite(std::span<const std::uint8_t> d) {
    auto u16=[&](std::size_t p){return unsigned(d[p])|(unsigned(d[p+1])<<8);};
    auto s16=[&](std::size_t p){int v=static_cast<int>(u16(p));return v>=32768?v-65536:v;};
    if(d.size()<4)throw std::runtime_error("Truncated sprite header");
    Sprite out;out.height=static_cast<int>(u16(0));out.y=s16(2);
    if(out.height>2048)throw std::runtime_error("Sprite height exceeds limit");
    struct Row{int x;Bytes pixels,mask;};
    std::vector<Row> rows;
    rows.reserve(out.height);
    int xmin=32767,xmax=-32768;
    std::size_t pos=4;
    for(int y=0;y<out.height;++y) {
        if(d.size()-pos<4)throw std::runtime_error("Truncated sprite row");
        Row row{s16(pos),{},{}};
        unsigned full=d[pos+2],partial=d[pos+3];pos+=4;
        if(4*full+8*partial>d.size()-pos)throw std::runtime_error("Truncated sprite chunks");
        for(unsigned j=0;j<4*full;++j){row.pixels.push_back(d[pos++]);row.mask.push_back(255);}
        for(unsigned j=0;j<partial;++j,pos+=8)
            for(unsigned k=0;k<4;++k){row.pixels.push_back(d[pos+k]);row.mask.push_back(d[pos+4+k]?0:255);}
        while(!row.mask.empty()&&!row.mask.back()){row.mask.pop_back();row.pixels.pop_back();}
        if(!row.mask.empty()){xmin=std::min(xmin,row.x);xmax=std::max(xmax,row.x+static_cast<int>(row.mask.size()));}
        rows.push_back(std::move(row));
    }
    out.trailing_bytes=d.size()-pos;
    if(xmax<=xmin){out.x=0;out.width=1;}
    else{out.x=xmin;out.width=xmax-xmin;}
    if(out.width>4096)throw std::runtime_error("Sprite width exceeds limit");
    out.indices.resize(static_cast<std::size_t>(out.width)*out.height);
    out.opaque.resize(out.indices.size());
    for(int y=0;y<out.height;++y) {
        auto& row=rows[y];if(row.mask.empty())continue;
        auto at=static_cast<std::size_t>(y)*out.width+(row.x-out.x);
        std::copy(row.pixels.begin(),row.pixels.end(),out.indices.begin()+at);
        std::copy(row.mask.begin(),row.mask.end(),out.opaque.begin()+at);
    }
    return out;
}
Palette tactical_palette(const Inventory& inv) {
    Palette p;
    for(unsigned i=0;i<256;++i)for(unsigned c=0;c<3;++c)p.rgb[i*3+c]=static_cast<std::uint8_t>(i);
    auto bank=[&](std::string_view suffix,unsigned first,unsigned count) {
        for(auto& a:inv.assets) {
            auto name=a.name;
            for(auto& c:name)if(c>='a'&&c<='z')c-='a'-'A';
            if(!name.ends_with(suffix)||a.size!=count*3)continue;
            try {
                auto bytes=preview(a,count*3);
                std::copy(bytes.begin(),bytes.end(),p.rgb.begin()+first*3);
                std::fill_n(p.known.begin()+first,count,true);
                return;
            }catch(const std::exception&){}
        }
    };
    bank("::MAP.PAL",0,128);
    bank("::UNITS.PAL",128,96);
    bank("::SYSTEM.PAL",224,32);
    return p;
}
static std::string json_string(const std::string& s) {
    std::ostringstream o;o<<'"';
    for(unsigned char ch:s) {
        if(ch=='"'||ch=='\\')o<<'\\'<<char(ch);
        else if(ch<32||ch>=127)o<<"\\u00"<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(ch)<<std::dec;
        else o<<char(ch);
    }
    o<<'"';return o.str();
}
std::string sprite_metadata(const Asset& a,const Sprite& s,const Palette& p) {
    std::ostringstream o;
    o<<"{\n  \"schema\": \"spellcross-sprite-v1\",\n  \"source_name\": "<<json_string(a.name)
     <<",\n  \"source_name_encoding\": \"byte-preserving JSON escapes\",\n  \"source_offset\": "<<a.offset<<",\n  \"stored_size\": "<<a.size
     <<",\n  \"width\": "<<s.width<<",\n  \"height\": "<<s.height
     <<",\n  \"x_offset\": "<<s.x<<",\n  \"y_offset\": "<<s.y
     <<",\n  \"trailing_bytes\": "<<s.trailing_bytes
     <<",\n  \"shadow_index\": 253,\n  \"preview_shadow_alpha\": 128,"
     <<"\n  \"images\": {\"preview\":\"preview.png\",\"indices\":\"indices.png\",\"opacity\":\"opacity.png\"},"
     <<"\n  \"stored_bytes\": \"original.bin\",\n  \"palette_rgb\": [";
    for(std::size_t i=0;i<p.rgb.size();++i){if(i)o<<',';o<<unsigned(p.rgb[i]);}
    o<<"],\n  \"palette_known\": [";
    for(std::size_t i=0;i<p.known.size();++i){if(i)o<<',';o<<(p.known[i]?"true":"false");}
    o<<"]\n}\n";return o.str();
}
std::string sprite_group(const Asset& a) {
    if(a.format!="FSU-SPRITE")return {};
    auto slash=a.name.find_last_of('/');
    return slash==std::string::npos?std::string{}:a.name.substr(0,slash);
}
std::vector<std::size_t> sprite_group_members(const Inventory& inv,const Asset& a) {
    std::vector<std::size_t> result;
    const auto group=sprite_group(a);
    if(group.empty())return result;
    for(std::size_t i=0;i<inv.assets.size();++i)
        if(sprite_group(inv.assets[i])==group && inv.assets[i].backing==a.backing)
            result.push_back(i);
    // FSU records are catalogued by logical name; preserve original sprite
    // table order using a stable sort only when names match (inventory order).
    // For playback this is explicitly a name-ordered contact sequence, not
    // inferred azimuth/animation semantics.
    return result;
}
}
