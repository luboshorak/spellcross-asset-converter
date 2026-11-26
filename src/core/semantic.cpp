// Layouts based on MIT restoration tools (Stanislav Maslan), spell_sound.cpp,
// riff_loader.cpp, spell_units.cpp and spell_font.cpp. Bounded byte readers.
#include "spellcross/semantic.hpp"
#include "spellcross/pack.hpp"
#include <algorithm>
#include <sstream>
#include <stdexcept>
namespace sc {
namespace {
using Span=std::span<const std::uint8_t>;
void check(Span d,std::size_t p,std::size_t n){if(p>d.size()||n>d.size()-p)throw std::runtime_error("Truncated semantic record");}
unsigned u16(Span d,std::size_t p){check(d,p,2);return unsigned(d[p])|(unsigned(d[p+1])<<8);}
std::uint32_t u32(Span d,std::size_t p){check(d,p,4);return std::uint32_t(d[p])|(std::uint32_t(d[p+1])<<8)|(std::uint32_t(d[p+2])<<16)|(std::uint32_t(d[p+3])<<24);}
std::string upper(std::string s){for(auto& c:s)if(c>='a'&&c<='z')c-='a'-'A';return s;}
std::string leaf(std::string s){auto p=s.find_last_of("/\\:");return upper(p==std::string::npos?s:s.substr(p+1));}
bool tag(Span d,std::size_t p,std::string_view s){return p<=d.size()&&s.size()<=d.size()-p&&std::equal(s.begin(),s.end(),d.begin()+p);}
Pcm pcm(Span d,int rate,int channels,int bits,bool unsigned16){
    if(rate<4000||rate>192000||(channels!=1&&channels!=2)||(bits!=8&&bits!=16)||d.empty()||d.size()%(channels*(bits/8)))
        throw std::runtime_error("Invalid PCM sample parameters");
    Pcm p{rate,channels,{}};p.samples.reserve(d.size()/(bits/8));
    if(bits==8)for(auto b:d)p.samples.push_back(static_cast<std::int16_t>((int(b)-128)*256));
    else for(std::size_t i=0;i<d.size();i+=2){
        int v=u16(d,i);if(unsigned16)v-=32768;else if(v>=32768)v-=65536;
        p.samples.push_back(static_cast<std::int16_t>(v));
    }
    return p;
}
Pcm riff(Span d){
    if(!tag(d,0,"RIFF")||!tag(d,8,"WAVE"))throw std::runtime_error("Invalid RIFF/WAVE");
    std::size_t end=std::size_t(u32(d,4))+8;
    if(end<12||end>d.size())throw std::runtime_error("Invalid RIFF size");
    int rate=0,channels=0,bits=0;Span samples;bool fmt=false,have_data=false;
    for(std::size_t p=12;p<end;){
        check(d.first(end),p,8);auto len=u32(d,p+4);check(d.first(end),p+8,len);
        if(tag(d,p,"fmt ")){
            if(fmt||len<16||u16(d,p+8)!=1)throw std::runtime_error("Unsupported WAVE format");
            channels=u16(d,p+10);rate=u32(d,p+12);bits=u16(d,p+22);fmt=true;
            if(u16(d,p+20)!=channels*(bits/8))throw std::runtime_error("Invalid WAVE block alignment");
        }else if(tag(d,p,"data")){
            if(have_data)throw std::runtime_error("Multiple WAVE data chunks");
            samples=d.subspan(p+8,len);have_data=true;
        }
        const bool data_chunk=tag(d,p,"data");
        p+=8+std::size_t(len);
        if(len&1){
            // Retail WAVE may omit final alignment after odd-sized PCM data.
            // Never tolerate missing samples or missing intermediate padding.
            if(p==end&&p==d.size()&&data_chunk&&fmt&&have_data)break;
            if(p>=end)throw std::runtime_error("Missing RIFF padding");
            ++p;
        }
        if(fmt&&have_data)break; // original loader tolerates unused trailing chunks
    }
    if(!fmt||!have_data)throw std::runtime_error("Missing WAVE chunks");
    return pcm(samples,rate,channels,bits,false);
}
}
std::optional<Pcm> decode_sample(const Asset& a,Span d){
    if(tag(d,0,"RIFF")&&tag(d,8,"WAVE"))return riff(d);
    if(!a.member||leaf(block_name(a))!="SAMPLES.FS")return std::nullopt;
    auto n=leaf(a.name);
    if(n.starts_with("1-"))return pcm(d,11025,1,16,true);
    if(n.starts_with("8"))return pcm(d,11025,1,8,false);
    if(n.starts_with("M"))return pcm(d,11025,2,16,true); // all interleaved samples, not half the stereo buffer
    if(n.starts_with("U"))return riff(d);
    return std::nullopt;
}
std::optional<std::string> unit_definitions(const Asset& a,Span d){
    if(leaf(a.name)!="JEDNOTKY.DEF")return std::nullopt;
    bool cz=!d.empty()&&d.size()%206==0, en=!d.empty()&&d.size()%207==0;
    if(cz==en)throw std::runtime_error("Unknown or ambiguous JEDNOTKY.DEF record size");
    auto stride=en?207u:206u;
    std::ostringstream o;o<<"{\"schema\":\"spellcross-units\",\"version\":1,\"record_size\":"<<stride<<",\"source_encoding\":\"original bytes\",\"units\":[\n";
    for(std::size_t id=0;id<d.size()/stride;++id){
        auto r=d.subspan(id*stride,stride);if(id)o<<",\n";
        auto off=[&](unsigned p){return p+(en&&p>=0x48?1:0);};
        auto num=[&](const char* n,unsigned p){o<<",\""<<n<<"\":"<<unsigned(r[off(p)]);};
        auto str=[&](const char* n,unsigned p,unsigned len,bool resource=false){
            std::string s;for(unsigned i=0;i<len;++i){auto b=r[off(p)+i];if(!b||(resource&&b=='_'))break;s+=char(b);}
            o<<",\""<<n<<"\":"<<pack::json(s);
        };
        auto params=[&](const char* n,unsigned a,unsigned b,unsigned c){o<<",\""<<n<<"\":["<<unsigned(r[off(a)])<<','<<unsigned(r[off(b)])<<','<<unsigned(r[off(c)])<<']';};
        o<<"{\"id\":"<<id;
        str("name",0,28);str("info",0x1c,9);str("graphics",0x25,6,true);str("aux_graphics",0x2b,6,true);str("icon",0x31,9);
        num("class_flags",0x3a);num("ap",0x3b);num("forest_move_ap",0x3c);num("move_ap",0x3d);num("count",0x3e);
        num("attack_light",0x3f);num("attack_armored",0x40);num("attack_air",0x41);num("attack_objects",0x42);
        num("defence",0x43);num("res2",0x44);num("initiative",0x45);num("fire_range",0x46);
        o<<",\"fire_flags\":"<<(en?u16(r,0x47):unsigned(r[0x47]));num("shot_ap",0x48);
        str("light_hit",0x49,9);str("light_animation",0x52,6,true);str("light_shot",0x58,9);num("light_frames",0x61);
        str("armor_hit",0x62,9);str("armor_animation",0x6b,6,true);str("armor_shot",0x71,9);num("armor_frames",0x7a);
        str("air_hit",0x7b,9);str("air_animation",0x84,6,true);str("air_shot",0x8a,9);num("air_frames",0x93);
        num("projectile_visible",0x94);str("projectile",0x95,13);num("action_id",0xa2);num("action_id_high_raw",0xa3);
        str("action_animation",0xa4,6,true);num("action_frames",0xaa);num("action_ap",0xab);params("action_params",0xac,0xad,0xae);
        num("direct_sight",0xaf);num("visible_count",0xb0);num("dig_turns",0xb1);str("die_animation",0xb2,6,true);num("die_frames",0xb8);
        num("die_action_id",0xbb);params("die_action_params",0xb9,0xba,0xbd);
        num("sound_move",0xbf);num("sound_light",0xc0);num("sound_armored",0xc1);num("sound_air",0xc2);num("sound_hit",0xc3);
        num("sound_action",0xc4);num("sound_selection",0xc5);
        o<<",\"exp_min\":"<<u32(r,off(0xc6))<<",\"exp_max\":"<<u32(r,off(0xca));
        o<<",\"original_hex\":\"";constexpr char hex[]="0123456789abcdef";
        for(auto b:r)o<<hex[b>>4]<<hex[b&15];o<<"\"}";
    }
    o<<"\n]}\n";return o.str();
}
std::optional<BitmapFont> decode_font(const Asset& a,Span d){
    if(leaf(a.name)!="FONT_001.FNT")return std::nullopt;
    check(d,0,3);unsigned count=d[0];if(!count)throw std::runtime_error("Empty font");
    BitmapFont f;f.default_height=d[2];std::size_t p=1;
    for(unsigned i=0;i<count;++i){
        check(d,p,2);int w=d[p++],h=d[p++];if(!w||!h)throw std::runtime_error("Empty font glyph");
        auto stride=(w+7)/8;check(d,p,std::size_t(stride)*h);
        Sprite s;s.width=w;s.height=h;s.shadow_index=false;s.indices.resize(std::size_t(w)*h);s.opaque.resize(s.indices.size());
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){
            auto v=(d[p+y*stride+x/8]>>(7-x%8))&1;s.indices[y*w+x]=v;s.opaque[y*w+x]=v?255:0;
        }
        p+=std::size_t(stride)*h;f.glyphs.push_back(std::move(s));
    }
    if(p!=d.size())throw std::runtime_error("Unexpected font trailing data");
    // Match engine's synthesized blank/control and space symbols.
    std::vector<Sprite> blank;
    for(int i=0;i<32;++i){Sprite s;s.width=i==31?4:1;s.height=f.default_height;s.shadow_index=false;s.indices.resize(s.width*s.height);s.opaque.resize(s.indices.size());blank.push_back(std::move(s));}
    blank.insert(blank.end(),std::make_move_iterator(f.glyphs.begin()),std::make_move_iterator(f.glyphs.end()));
    f.glyphs=std::move(blank);f.first_code=0;return f;
}
}
