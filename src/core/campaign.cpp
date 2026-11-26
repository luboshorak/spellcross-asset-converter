// Retail layouts based on Spellcross restoration tools (MIT),
// level.cpp, SpellDEF and SpellMap::Load. No engine execution in the converter.
#include "spellcross/campaign.hpp"
#include "spellcross/pack.hpp"
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <string_view>
namespace sc {
namespace {
using Span=std::span<const std::uint8_t>;
using pack::json;
std::string trim(std::string_view s){
    auto ws=[](char c){return c==' '||c=='\t'||c=='\r'||c=='\n';};
    while(!s.empty()&&ws(s.front()))s.remove_prefix(1);
    while(!s.empty()&&ws(s.back()))s.remove_suffix(1);
    return std::string(s);
}
std::string lower(std::string s){
    for(auto& c:s)if(c>='A'&&c<='Z')c+='a'-'A';return s;
}
bool identifier(std::string_view s){
    if(s.empty())return false;
    auto alpha=[](char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_';};
    if(!alpha(s.front()))return false;
    for(char c:s)if(!alpha(c)&&!(c>='0'&&c<='9'))return false;
    return true;
}
struct Call{std::string name;std::vector<std::string> args;};
std::optional<Call> call(std::string_view line,bool bare){
    auto p=line.find('(');
    if(p==std::string_view::npos){
        auto n=trim(line);if(bare&&identifier(n))return Call{n,{}};return std::nullopt;
    }
    auto end=line.rfind(')');
    if(end==std::string_view::npos||end<p||!trim(line.substr(end+1)).empty())return std::nullopt;
    auto name=trim(line.substr(0,p));if(!identifier(name))return std::nullopt;
    auto args=line.substr(p+1,end-p-1);
    if(args.find_first_of("(){}")!=std::string_view::npos)return std::nullopt;
    Call c{name,{}};
    if(!args.empty())for(;;){
        auto comma=args.find(',');c.args.push_back(trim(args.substr(0,comma)));
        if(comma==std::string_view::npos)break;args.remove_prefix(comma+1);
    }
    return c;
}
void arguments(std::ostream& o,const Call& c){
    o<<"\"name\":"<<json(c.name)<<",\"keyword\":"<<json(lower(c.name))<<",\"args\":[";
    for(std::size_t i=0;i<c.args.size();++i){if(i)o<<',';o<<json(c.args[i]);}o<<']';
}
struct Reader {
    Span d;std::size_t p{};
    Span take(std::size_t n){
        if(p>d.size()||n>d.size()-p)throw std::runtime_error("Truncated tactical map at "+std::to_string(p));
        auto b=d.subspan(p,n);p+=n;return b;
    }
    unsigned u8(){return take(1)[0];}
    unsigned u16(){auto b=take(2);return unsigned(b[0])|(unsigned(b[1])<<8);}
    std::uint32_t u32(){auto b=take(4);return std::uint32_t(b[0])|(std::uint32_t(b[1])<<8)|(std::uint32_t(b[2])<<16)|(std::uint32_t(b[3])<<24);}
    int s16(){auto x=u16();return x>=32768?int(x)-65536:int(x);}
    std::int64_t s32(){auto x=u32();return x>=0x80000000u?std::int64_t(x)-0x100000000ll:x;}
    std::string fixed(std::size_t n){auto b=take(n);return std::string(b.begin(),std::find(b.begin(),b.end(),0));}
    std::string zero(){
        std::string s;for(std::size_t i=0;i<256;++i){auto c=u8();if(!c)return s;s+=char(c);}
        throw std::runtime_error("Unterminated map sound name");
    }
    unsigned count(unsigned max){auto n=u32();if(n>max)throw std::runtime_error("Map record count out of bounds");return n;}
};
void names(std::ostream& o,Reader& r,unsigned count,bool fixed){
    o<<'[';for(unsigned i=0;i<count;++i){if(i)o<<',';o<<json(fixed?r.fixed(8):r.zero());}o<<']';
}
void hex(std::ostream& o,Span d){
    constexpr char digits[]="0123456789abcdef";o<<'"';for(auto b:d)o<<digits[b>>4]<<digits[b&15];o<<'"';
}
}
std::optional<std::string> script_definition(const Asset& a,Span d){
    if(a.format!="DEF"&&a.format!="TUT")return std::nullopt;
    if(d.empty()||d.size()>16u*1024u*1024u)return std::nullopt;
    for(auto b:d)if(b<32&&b!=9&&b!=10&&b!=13&&b!=26)return std::nullopt;
    std::string src(d.begin(),d.end());
    std::istringstream input(src);std::ostringstream o;
    o<<"{\"schema\":\"spellcross-script\",\"version\":1,\"source_encoding\":\"byte-preserving JSON escapes\",\"records\":[\n";
    std::string line;std::size_t ln=0,blocks=0,commands=0,unknown=0,depth=0;bool first=true;
    // Flat ordered syntax records keep repeating calls and block boundaries
    // intact, including unknown commands. Do not infer defaults or execute.
    while(std::getline(input,line)){
        ++ln;
        auto raw=line;
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        auto eof=line.find(char(26));
        if(eof!=std::string::npos){
            // DOS EOF only accepted when the remaining bytes are whitespace.
            auto tail=line.substr(eof+1);
            if(!trim(tail).empty())throw std::runtime_error("Unexpected bytes after DOS EOF");
            line.resize(eof);
        }
        auto semi=line.find(';');
        auto code=trim(line.substr(0,semi));
        if(code.empty()){if(eof!=std::string::npos)break;continue;}
        if(!first)o<<",\n";first=false;
        o<<"{\"line\":"<<ln<<",\"source\":"<<json(raw);
        if(code=="}"){
            if(!depth)throw std::runtime_error("Unmatched script closing brace at line "+std::to_string(ln));
            --depth;o<<",\"kind\":\"end\",\"depth\":"<<depth;
        }else if(code.back()=='{'){
            auto c=call(trim(std::string_view(code).substr(0,code.size()-1)),true);
            if(c){o<<",\"kind\":\"block\",\"depth\":"<<depth<<',';arguments(o,*c);++blocks;++depth;}
            else{o<<",\"kind\":\"unknown\",\"depth\":"<<depth;++unknown;}
        }else if(auto c=call(code,false)){
            o<<",\"kind\":\"call\",\"depth\":"<<depth<<',';arguments(o,*c);++commands;
        }else{o<<",\"kind\":\"unknown\",\"depth\":"<<depth;++unknown;}
        o<<'}';
        if(eof!=std::string::npos)break;
    }
    if(!blocks)return std::nullopt; // plain class lists and unrelated text
    if(depth)throw std::runtime_error("Unclosed script block");
    o<<"\n],\"block_count\":"<<blocks<<",\"command_count\":"<<commands<<",\"unknown_count\":"<<unknown<<"}\n";
    return o.str();
}
std::optional<MapPayload> tactical_map(const Asset& a,Span d){
    // DTA also denotes terrain tile resources. Version byte + known terrain
    // header distinguish retail tactical maps from their graphics.
    if(a.format!="DTA"||d.size()<26||d[8]!=0x12)return std::nullopt;
    Reader r{d};auto l1off=r.u32(),l1=r.u32();r.u8();
    auto w=r.u16(),h=r.u16();auto terrain=r.fixed(13);
    auto t=lower(terrain);if(t!="t11"&&t!="pust"&&t!="devast")return std::nullopt;
    if(l1>4095)throw std::runtime_error("Map L1 table exceeds 12-bit index range");
    std::size_t cells=std::size_t(w)*h;
    if(!w||!h||cells>1000000||!l1)throw std::runtime_error("Invalid map dimensions or L1 table");
    MapPayload out;std::ostringstream o;
    o<<"{\"schema\":\"spellcross-tactical-map\",\"version\":1,\"width\":"<<w<<",\"height\":"<<h
     <<",\"terrain\":"<<json(terrain)<<",\"source_l1_offset\":"<<l1off<<",\"ground\":{\"entry\":\"ground.u16le\",\"sprites\":";
    names(o,r,l1,true);
    if(r.p!=l1off)throw std::runtime_error("Unexpected map L1 offset");
    auto ground=r.take(cells*2);out.ground.assign(ground.begin(),ground.end());
    for(std::size_t i=0;i<cells;++i){
        auto code=unsigned(ground[i*2])|(unsigned(ground[i*2+1])<<8);
        if((code&4095)>=l1)throw std::runtime_error("Map ground index out of bounds");
    }
    o<<",\"index_mask\":4095,\"elevation_shift\":12},\"objects\":{\"entry\":\"objects.u8x2\",\"sprites\":";
    auto l2=r.count(255);names(o,r,l2,true);
    auto objects=r.take(cells*2);out.objects.assign(objects.begin(),objects.end());
    for(std::size_t i=0;i<cells;++i)if(objects[i*2]>l2)throw std::runtime_error("Map object index out of bounds");
    o<<",\"zero_means_empty\":true,\"index_base\":1}";
    for(int layer=3;layer<=4;++layer){
        o<<(layer==3?",\"anm\":{":",\"pnm\":{")<<"\"resources\":";
        auto n=r.count(65535);names(o,r,n,true);o<<",\"placements\":[";
        auto count=n?r.count(1000000):0;
        for(unsigned i=0;i<count;++i){
            if(i)o<<',';auto frame=r.u8();int limit=layer==3?r.s16():int(r.u16());
            int x=layer==3?r.s16():int(r.u16()),y=layer==3?r.s16():int(r.u16());
            auto id=r.u16();if(id>=n)throw std::runtime_error("Map animation index out of bounds");
            o<<"{\"resource\":"<<id<<",\"frame_offset\":"<<frame<<",\"frame_limit\":"<<limit<<",\"x\":"<<x<<",\"y\":"<<y;
            if(layer==4){auto dy=r.s32(),dx=r.s32();o<<",\"x_offset\":"<<dx<<",\"y_offset\":"<<dy;}
            o<<'}';
        }
        o<<"]}";
    }
    for(int layer=5;layer<=6;++layer){
        o<<(layer==5?",\"enemy_counterattack_posts\":[":",\"player_counterattack_posts\":[");
        auto n=r.count(1000000);for(unsigned i=0;i<n;++i){
            auto pos=r.u16();if(pos>=cells)throw std::runtime_error("Counterattack post outside map");if(i)o<<',';o<<pos;
        }o<<']';
    }
    for(int layer=7;layer<=8;++layer){
        o<<(layer==7?",\"loop_sounds\":{":",\"random_sounds\":{")<<"\"resources\":";
        auto n=r.count(256);names(o,r,n,false);o<<",\"placements\":[";
        auto count=r.count(1000000);
        for(unsigned i=0;i<count;++i){
            auto pos=r.u16(),id=r.u8();if(pos>=cells||id>=n)throw std::runtime_error("Invalid map sound placement");
            if(i)o<<',';o<<"{\"tile\":"<<pos<<",\"resource\":"<<id<<'}';
        }o<<"]}";
    }
    o<<",\"trailing_hex\":";hex(o,d.subspan(r.p));o<<"}\n";
    out.metadata=o.str();return out;
}
}
