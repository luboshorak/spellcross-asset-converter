// Layouts based on MIT restoration tools (Stanislav Maslan), spell_sound.cpp,
// riff_loader.cpp, spell_units.cpp and spell_font.cpp. Bounded byte readers.
#include "spellcross/semantic.hpp"
#include "spellcross/pack.hpp"
#include <algorithm>
#include <sstream>
#include <set>
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

bool legacy_support_asset(const Asset& a){
    auto n=leaf(a.name),full=upper(a.name);
    std::replace(full.begin(),full.end(),'\\','/');
    const auto block=leaf(block_name(a));
    // Owner-approved archival exceptions: exact source identity and size.
    // Do not suppress a different or full-sized video with the same name.
    if(a.member&&block=="COMMON.FS"&&n=="BOXER.LST"&&a.format=="LST"&&a.size==225)return true;
    if(a.member&&block=="MOVIE.FS"&&n=="TEMP.DPK"&&a.format=="DPK"&&a.size==525)return true;
    if(block=="INSTALL.FS"&&n.size()==7&&n.starts_with("MSG")&&
       std::all_of(n.begin()+3,n.end(),[](char c){return c>='0'&&c<='9';}))return true;
    if(!a.member&&full=="AUTORUN.INF"&&a.format=="INF"&&a.size==48)return true;
    if(!a.member&&full=="INSTAL~1.EXE"&&a.format=="EXE"&&a.size==400384)return true;
    if(a.member&&full.ends_with("INSTALL.DTA::SETSOUND.BAT")&&a.format=="BAT"&&a.size==42)return true;
    // Explicit DOS support paths and filenames, not a blanket rule for
    // unknown extensions or all contents of INSTALL.DTA.
    if(full.find("::DRIVERS/")!=std::string::npos||full.starts_with("DRIVERS/")){
        return a.format=="DIG"||a.format=="MDI"||n=="1024.GUS"||
            n=="FATMAN.AD"||n=="FATMAN.OPL"||n=="MSSDRVR.LST"||
            n=="DOS4GW.EXE"||n=="MSSW95.EXE"||n=="SETSOUND.EXE";
    }
    if(!a.member&& (n=="DOS4GW.EXE"||n=="INSTALUJ.EXE"||n=="INSTALL.EXE"||
       n=="INSTALUJ.PCX"||n=="SPELCROS.EXE"||n=="SETUP.BAT"))return true;
    return full.find("INSTALL.DTA::")!=std::string::npos&&
        (n=="DOS4GW.EXE"||n=="SPELCROS.EXE"||n=="SETUP.BAT")&&
        full.find("::",full.find("INSTALL.DTA::")+13)==std::string::npos;
}
std::optional<RetailTable> retail_table(const Asset& a,Span d){
    const auto n=leaf(a.name),block=leaf(block_name(a));
    std::ostringstream o;
    std::string kind;
    auto header=[&](const char* k){kind=k;o<<"{\"schema\":\"spellcross-retail-table\",\"version\":1,\"kind\":"<<pack::json(kind);};
    auto values=[&](Span b){o<<'[';for(std::size_t i=0;i<b.size();++i){if(i)o<<',';o<<unsigned(b[i]);}o<<']';};
    if(block=="COMMON.FS"&&a.format=="SSD"&&n.size()==12&&n.starts_with("LEVEL_")&&n.ends_with(".SSD")){
        if(d.size()!=128)throw std::runtime_error("Unexpected SSD adjacency table length");
        header("territory-adjacency");
        o<<",\"masks\":[";
        for(unsigned i=0;i<32;++i){if(i)o<<',';o<<u32(d,i*4);}
        o<<"],\"neighbors\":[";
        for(unsigned i=0;i<32;++i){
            if(i)o<<',';o<<'[';bool first=true;
            const auto mask=u32(d,i*4);
            for(unsigned bit=0;bit<32;++bit)if(mask&(std::uint32_t(1)<<bit)){
                if(!first)o<<',';first=false;o<<bit;
            }
            o<<']';
        }
        o<<']';
    }else if(block=="COMMON.FS"&&n=="GAMMA.TBL"){
        if(d.size()!=704||std::any_of(d.begin(),d.end(),[](auto v){return v>63;}))
            throw std::runtime_error("Unexpected gamma table layout");
        header("gamma-table");o<<",\"rows\":[";
        for(unsigned i=0;i<11;++i){if(i)o<<',';values(d.subspan(i*64,64));}
        o<<']';
    }else if((block=="T11.FS"||block=="PUST.FS"||block=="DEVAST.FS")&&n=="MAPCOLOR.COL"){
        if(d.size()!=11)throw std::runtime_error("Unexpected terrain color table length");
        header("terrain-color-indices");o<<",\"values\":";values(d);
        o<<",\"slot_semantics\":\"unverified\"";
    }else if(block=="INFO.FS"&&(n=="PALETA.DRK"||n=="PALETA.LGH"||n=="PALETA.NRM")){
        const bool normal=n=="PALETA.NRM";
        if(d.size()!=(normal?768u:256u))throw std::runtime_error("Unexpected INFO palette/filter length");
        header(normal?"info-rgb-palette":"info-index-filter");
        o<<",\"role\":"<<pack::json(normal?"normal":n=="PALETA.DRK"?"dark":"light")<<",\"values\":";values(d);
    }else if(block=="COMMON.FS"&&(n=="_BUY.PAL"||n=="_INFO.PAL"||n=="_RSEARCH.PAL"||n=="_STATS.PAL")){
        // A CRLF-only record contains no RGB triples. Do not invent a bank.
        if(d.size()!=2||d[0]!=13||d[1]!=10)return std::nullopt;
        header("empty-palette");o<<",\"values\":[],\"original_bytes\":";values(d);
    }else return std::nullopt;
    o<<'}';return RetailTable{kind,o.str()};
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
        const auto cls=r[0x3a];const auto fire=en?u16(r,0x47):unsigned(r[0x47]);
        auto boolean=[&](const char* name,bool value){o<<",\""<<name<<"\":"<<(value?"true":"false");};
        o<<",\"class_semantics\":{\"target_class\":"<<pack::json((cls&0x30)==0?"air":(cls&0x30)==0x10?"light":(cls&0x30)==0x20?"armored":"unknown");
        boolean("turret",cls&1);boolean("walk",cls&2);boolean("fly",cls&4);boolean("hover",cls&8);boolean("organic",cls&0x40);
        o<<",\"unknown_bits\":"<<unsigned(cls&0x80)<<"}";
        o<<",\"fire_semantics\":{\"flags\":"<<fire;
        boolean("indirect",fire&2);boolean("steals_ap",fire&4);boolean("cannot_fire_from_slopes",fire&0x10);
        boolean("indirect_missile",fire&0x20);boolean("fire_projectile",fire&0x40);boolean("fire_sensitive",fire&0x80);boolean("fire_healed",fire&0x100);
        o<<",\"uncertain_bits\":"<<(fire&~0x1f6u)<<"}";
        o<<",\"projectile_targets\":{\"light\":"<<((r[off(0x94)]&1)?"true":"false");
        boolean("armored",r[off(0x94)]&2);boolean("air",r[off(0x94)]&4);
        o<<",\"unknown_bits\":"<<unsigned(r[off(0x94)]&0xf8)<<"}";
        o<<",\"unmapped_bytes\":{\"0x44\":"<<unsigned(r[off(0x44)])
         <<",\"0xa3\":"<<unsigned(r[off(0xa3)])<<",\"0xbc\":"<<unsigned(r[off(0xbc)])
         <<",\"0xbe\":"<<unsigned(r[off(0xbe)])<<"}";
        o<<",\"original_hex\":\"";constexpr char hex[]="0123456789abcdef";
        for(auto b:r)o<<hex[b>>4]<<hex[b&15];o<<"\"}";
    }
    o<<"\n]}\n";return o.str();
}
// Optional editor-generated units.fsa, not part of ordinary retail ISO.
// Layout: FSUarchive::LoadAuxData (Maslan, MIT). Keep signed coordinates
// exactly as supplied and never synthesize default fire origins.
std::optional<std::string> fsu_context(const Asset& a,Span d){
    if(a.format!="FSA"||leaf(a.name)!="UNITS.FSA")return std::nullopt;
    constexpr char magic[]="SpellFSUcontextV1.0";
    if(d.size()<sizeof(magic)||!std::equal(std::begin(magic),std::end(magic),d.begin()))
        throw std::runtime_error("Unsupported FSA context header");
    std::size_t p=sizeof(magic);
    auto read32=[&](){auto x=u32(d,p);p+=4;return x;};
    auto read16=[&](){auto x=u16(d,p);p+=2;return x;};
    auto count=[&](unsigned limit){auto n=read32();if(n>limit)throw std::runtime_error("FSA count outside limit");return n;};
    auto coord=[&](){auto v=read32();return v>=0x80000000u?std::int64_t(v)-0x100000000ll:std::int64_t(v);};
    auto n=count(10000);std::vector<std::string> names;std::set<std::string> seen;
    for(unsigned i=0;i<n;++i){
        auto len=read16();if(!len||len>64)throw std::runtime_error("Invalid FSA resource name");
        check(d,p,len);std::string name(d.begin()+p,d.begin()+p+len);p+=len;
        // The editor's p16 string length includes its terminating NUL.
        // Also accept earlier non-terminated fixtures/exports. Never strip
        // embedded NULs or multiple terminators, and check normalized IDs.
        if(!name.empty()&&name.back()=='\0')name.pop_back();
        if(name.empty()||std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32||c>=127;})||!seen.insert(upper(name)).second)
            throw std::runtime_error("Invalid or duplicate FSA resource name");
        names.push_back(std::move(name));
    }
    std::ostringstream o;o<<"{\"schema\":\"spellcross-fsu-context\",\"version\":1,\"resources\":[";
    auto point=[&](){auto x=coord(),y=coord();o<<'['<<x<<','<<y<<']';};
    auto points=[&](unsigned n){o<<'[';for(unsigned i=0;i<n;++i){if(i)o<<',';point();}o<<']';};
    for(unsigned i=0;i<n;++i){
        if(i)o<<',';o<<"{\"group\":"<<pack::json(names[i])<<",\"static\":[";
        auto slopes=count(13);
        for(unsigned j=0;j<slopes;++j){if(j)o<<',';o<<"{\"fire_origins\":";points(count(256));o<<",\"fire_center\":";point();o<<'}';}
        o<<"],\"animation\":{\"fire_origins\":";points(count(256));o<<",\"fire_center\":";point();o<<"}}";
    }
    if(p!=d.size())throw std::runtime_error("Unexpected FSA trailing bytes");
    o<<"]}";return o.str();
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
