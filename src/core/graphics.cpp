// LZ and FSU scanline layout based on the MIT-licensed Spellcross Map Editor
// by Stanislav Maslan (2021-2025). Bounded implementation; no unaligned reads.
#include "spellcross/graphics.hpp"
#include <algorithm>
#include <stdexcept>
#include <limits>
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
}
