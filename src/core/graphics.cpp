// LZ and FSU scanline layout based on the MIT-licensed Spellcross Map Editor
// by Stanislav Maslan (2021-2025). Bounded implementation; no unaligned reads.
#include "spellcross/graphics.hpp"
#include <algorithm>
#include <stdexcept>
#include <limits>
#include <sstream>
#include <iomanip>
#include <string_view>
#include <map>
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
    if(compressed(a)){
        data=decode_lz(data,max_output);
        // Only probe nested LZ for explicitly named LZ assets. Restrict
        // detection to the implicit full dictionary and successful decode.
        if(a.format!="FSU-SPRITE")for(int depth=1;depth<4;++depth){
            if(data.size()<5||data[0]!=0||data[1]!=1||data[2]!=9)break;
            try{auto next=decode_lz(data,max_output);if(next==data)break;data=std::move(next);}
            catch(const std::exception&){break;}
        }
    }
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
static std::string upper_name(const Asset& a){
    auto i=a.name.find_last_of("/\\:");
    auto n=a.name.substr(i==std::string::npos?0:i+1);
    for(auto& ch:n)if(ch>='a'&&ch<='z')ch-='a'-'A';
    return n;
}
bool compressed(const Asset& a){return a.format=="LZ"||a.format=="LZ0"||a.format=="FSU-SPRITE";}
std::string block_name(const Asset& a){
    if(a.member){auto i=a.name.find("::");if(i!=std::string::npos)return a.name.substr(0,i);}
    if(a.format=="FS"||a.format=="FSU")return a.name;
    return {};
}
static bool in_archive(const Asset& a,std::string_view archive){
    auto block=block_name(a);
    auto slash=block.find_last_of("/\\");
    if(slash!=std::string::npos)block=block.substr(slash+1);
    for(auto& c:block)if(c>='a'&&c<='z')c-='a'-'A';
    return a.member&&block==archive;
}
bool is_text_asset(const Asset& a,std::span<const std::uint8_t> d){
    if(d.empty())return false;
    bool named=a.format=="DEF"||a.format=="TXT"||a.format=="BRF"||a.format=="TUT"||
        a.format=="ENG"||a.format=="CZE"||a.format=="POL"||a.format=="ITA"||
        a.format=="CZ"||a.format=="EN"||a.format=="PL"||a.format=="IT"||
        a.format=="INI"||a.format=="CSV"||
        in_archive(a,"TEXTS.FS")||(in_archive(a,"RESEARCH.FS")&&a.format=="INF")||
        (in_archive(a,"INFO.FS")&&a.format=="LST");
    if(!named)return false;
    for(auto b:d)if(b<32&&b!=9&&b!=10&&b!=13)return false;
    return true;
}
Palette asset_palette(const Inventory& inv,const Asset& a,const Palette& tactical){
    auto n=upper_name(a);
    if(in_archive(a,"INFO.FS")&&a.format=="LZ"){
        Palette p{};
        for(unsigned i=0;i<256;++i)for(int c=0;c<3;++c)p.rgb[i*3+c]=static_cast<std::uint8_t>(i);
        bool chrome=n=="IBG_768.LZ"||n=="INFOPAN.LZ"||n=="INFOLSTA.LZ";
        auto wanted=chrome?std::string("PALETA.NRM"):n.substr(0,n.size()-3)+".PAL";
        for(auto& entry:inv.assets){
            if(block_name(entry)!=block_name(a)||upper_name(entry)!=wanted)continue;
            const unsigned count=chrome?256:192;
            if(entry.size<count*3)continue;
            try{auto data=preview(entry,count*3);std::copy(data.begin(),data.end(),p.rgb.begin());
                std::fill_n(p.known.begin(),count,true);return p;}catch(const std::exception&){}
        }
        return p;
    }
    if(!((n.starts_with("VM")&&(a.format=="ICO"||a.format=="BTN"))||n=="LASTTERT.ICO"))return tactical;
    Palette p=tactical;
    auto bank=[&](std::string_view suffix,unsigned first,unsigned count){
        for(auto& entry:inv.assets){
            if(upper_name(entry)!=suffix||entry.size!=count*3)continue;
            try{auto b=preview(entry,count*3);std::copy(b.begin(),b.end(),p.rgb.begin()+first*3);
                std::fill_n(p.known.begin()+first,count,true);return;}
            catch(const std::exception&){}
        }
    };
    bank("STRATEGY.PAL",0,256);bank("_SHARED1.PAL",0,128);bank("BIG_MAP.PAL",192,64);
    return p;
}
std::optional<Sprite> decode_image(const Asset& a,std::span<const std::uint8_t> d){
    if(a.format=="FSU-SPRITE")return decode_sprite(d);
    Sprite s;s.shadow_index=false;
    auto u16=[&](std::size_t i){return unsigned(d[i])|(unsigned(d[i+1])<<8);};
    auto allocate=[&](){
        if(s.width<=0||s.height<=0||s.width>4096||s.height>4096||
           std::uint64_t(s.width)*s.height>16000000)throw std::runtime_error("Invalid image dimensions");
        s.indices.resize(std::size_t(s.width)*s.height);s.opaque.resize(s.indices.size());
    };
    if(a.format=="ICO"||a.format=="BTN"||a.format=="PNM-FRAME"){
        if(d.size()<7)throw std::runtime_error("Truncated ICO header");
        s.y=int(u16(0));if(s.y>=32768)s.y-=65536;
        s.x=int(u16(2));s.width=int(u16(4));s.height=d[6];allocate();
        std::size_t p=7;
        for(int y=0;y<s.height;++y){
            int x=-s.x;
            while(true){
                if(p>=d.size())throw std::runtime_error("Truncated ICO row");
                if(d[p]==255){++p;break;}
                if(d.size()-p<2)throw std::runtime_error("Truncated ICO chunk");
                x+=d[p++];unsigned n=d[p++];
                if(n>d.size()-p||(n&&(x<0||x>s.width||n>unsigned(s.width-x))))throw std::runtime_error("ICO chunk outside image");
                for(unsigned j=0;j<n;++j){auto at=std::size_t(y)*s.width+x+j;s.indices[at]=d[p+j];s.opaque[at]=(a.format=="PNM-FRAME"&&d[p+j]==0)?0:255;}
                p+=n;x+=n;
            }
        }
        s.trailing_bytes=d.size()-p;return s;
    }
    std::size_t start=0;
    bool opaque_zero=false;
    auto n=upper_name(a);
    if(in_archive(a,"INFO.FS")&&a.format=="LZ"){
        if(n=="IBG_768.LZ"){s.width=1024;s.height=768;opaque_zero=true;}
        else if(n=="INFOPAN.LZ"){s.width=640;s.height=480;}
        else if(n=="INFOLSTA.LZ"){s.width=433;s.height=63;}
        else{s.width=437;s.height=390;opaque_zero=true;}
        const auto expected=std::size_t(s.width)*s.height;
        if(d.size()<expected||d.size()>expected+2)return std::nullopt;
    }else if(a.format=="CUR"){
        if(d.size()<2)throw std::runtime_error("Truncated CUR header");
        s.width=d[0];s.height=d[1];start=2;
    }else if(a.format=="GFK"){s.width=21;s.height=21;}
    else{
        static const std::map<std::string,int> widths={{"LISTA_0.LZ",640},{"LISTA_1.LZ",640},{"LISTA_0B.LZ0",160},{"LISTAPAT.LZ",32},{"GU_LISTA.LZ",145},{"LEV_GFK.LZ",9},{"M_ACCOMP.LZ",340},{"M_FAILED.LZ",340},{"WM_STAT.LZ",408},{"OPT_BAR.LZ",10},{"MAP_OPT.LZ",436}};
        auto it=widths.find(n);
        if(it!=widths.end())s.width=it->second;
        else if(n.starts_with("I_")&&a.format=="LZ")s.width=60;
        else if(n=="I_ATTACK"||n=="I_LOWER"||n=="I_MOVE"||n=="I_UPPER"||n=="I_SELECT"){s.width=20;s.height=20;opaque_zero=true;}
        else if(n=="I_TAB"){s.width=60;s.height=45;opaque_zero=true;}
        else return std::nullopt;
        if(!s.height){if(d.size()%s.width)throw std::runtime_error("Raw image length not divisible by known width");s.height=static_cast<int>(d.size()/s.width);}
    }
    allocate();
    if(s.indices.size()>d.size()-start)throw std::runtime_error("Truncated raw image");
    std::copy_n(d.begin()+start,s.indices.size(),s.indices.begin());
    for(std::size_t i=0;i<s.indices.size();++i)s.opaque[i]=(s.indices[i]||opaque_zero)?255:0;
    s.trailing_bytes=d.size()-start-s.indices.size();
    return s;
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
     <<",\n  \"shadow_index\": "<<(s.shadow_index?"253":"null")<<",\n  \"preview_shadow_alpha\": "<<(s.shadow_index?"128":"null")<<","
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
namespace {
constexpr std::size_t layer_limit=64*1024*1024;
constexpr std::size_t max_depth=12;
constexpr std::size_t max_children=100000;
std::uint32_t layer_u32(std::span<const std::uint8_t> b,std::size_t p){
    if(p>b.size()||b.size()-p<4)throw std::runtime_error("Truncated nested record");
    return std::uint32_t(b[p])|(std::uint32_t(b[p+1])<<8)|(std::uint32_t(b[p+2])<<16)|(std::uint32_t(b[p+3])<<24);
}
unsigned layer_u16(std::span<const std::uint8_t> b,std::size_t p){
    if(p>b.size()||b.size()-p<2)throw std::runtime_error("Truncated nested record");
    return unsigned(b[p])|(unsigned(b[p+1])<<8);
}
void layer_bounds(std::span<const std::uint8_t> b,std::size_t p,std::size_t n){
    if(p>b.size()||n>b.size()-p)throw std::runtime_error("Nested record outside parent");
}
std::string layer_name(std::span<const std::uint8_t> b,std::size_t p,std::size_t n){
    layer_bounds(b,p,n);std::string s;
    for(std::size_t i=0;i<n&&b[p+i];++i){
        auto ch=b[p+i];if(ch<32||ch>126||ch=='/'||ch=='\\'||ch==':')
            throw std::runtime_error("Invalid nested member name");
        s+=char(ch);
    }
    if(s.empty()||s=="."||s=="..")throw std::runtime_error("Invalid nested member name");
    return s;
}
std::string layer_format(const std::string& n){
    auto dot=n.find_last_of('.');
    if(dot==std::string::npos)return "UNKNOWN";
    auto ext=n.substr(dot+1);
    for(auto& c:ext)if(c>='a'&&c<='z')c-='a'-'A';
    return ext;
}
bool lz_header(std::span<const std::uint8_t> b){
    if(b.size()<5)return false;
    auto n=layer_u16(b,0),w=unsigned(b[2]);
    return n>0&&n<=256&&w>=2&&w<=12&&n+1<(1u<<w)&&b.size()>3+(n==256?0:n);
}
std::vector<LayerStep> fs_layers(std::span<const std::uint8_t> b){
    auto count=layer_u32(b,0);
    if(!count||count>max_children||count>(b.size()-4)/21)throw std::runtime_error("Invalid nested FS count");
    std::vector<LayerStep> out;out.reserve(count);
    for(std::size_t i=0;i<count;++i){
        auto p=4+i*21;auto n=layer_name(b,p,13);
        auto off=layer_u32(b,p+13),size=layer_u32(b,p+17);
        layer_bounds(b,off,size);
        if(size&&off<4+count*21)throw std::runtime_error("Nested FS member overlaps header");
        out.push_back({false,off,size,n,layer_format(n),n});
    }
    return out;
}
std::vector<LayerStep> fsu_layers(std::span<const std::uint8_t> b){
    auto count=layer_u32(b,4);
    if(!count||count>10000||count>(b.size()-8)/9)throw std::runtime_error("Invalid nested FSU count");
    std::vector<LayerStep> out;
    for(std::size_t i=0;i<count;++i){
        auto p=8+9*i;auto group=layer_name(b,p,5);auto off=layer_u32(b,p+5);
        auto n=layer_u16(b,off);
        if(off<8+count*9||n>max_children-out.size())throw std::runtime_error("Invalid FSU resource");
        layer_bounds(b,off+2,n*9);
        for(std::size_t j=0;j<n;++j){
            auto pos=off+2+j*9;auto name=group+"/"+layer_name(b,pos,3);
            auto start=layer_u32(b,pos+3),size=layer_u16(b,pos+7);layer_bounds(b,start,size);
            if(size<3)throw std::runtime_error("Truncated FSU sprite");
            out.push_back({false,start,size,name,"FSU-SPRITE",name});
        }
    }
    return out;
}
// PNM frame offsets from AnimPNM::Decode in the MIT restoration tools.
// Limit each frame to the next distinct offset (or end of animation).
std::vector<LayerStep> pnm_layers(std::span<const std::uint8_t> b){
    if(b.empty()||!b[0]||b.size()<1+4u*b[0])throw std::runtime_error("Invalid PNM frame table");
    std::vector<std::size_t> offsets;
    for(unsigned i=0;i<b[0];++i){
        auto off=layer_u32(b,1+4*i);
        if(off<1+4u*b[0])throw std::runtime_error("PNM frame overlaps table");
        layer_bounds(b,off,7);offsets.push_back(off);
    }
    auto sorted=offsets;std::sort(sorted.begin(),sorted.end());
    std::vector<LayerStep> out;
    for(std::size_t i=0;i<offsets.size();++i){
        auto next=std::upper_bound(sorted.begin(),sorted.end(),offsets[i]);
        auto end=next==sorted.end()?b.size():*next;
        auto name="Frame "+std::to_string(i+1);
        out.push_back({false,offsets[i],end-offsets[i],name,"PNM-FRAME",name});
    }
    return out;
}
bool text_bytes(std::span<const std::uint8_t> d){
    if(d.empty())return false;
    std::size_t ascii=0;
    for(auto b:d){
        if(b<32&&b!=9&&b!=10&&b!=13)return false;
        if((b>=32&&b<=126)||b==9||b==10||b==13)++ascii;
    }
    return ascii>=d.size()-d.size()/5;
}
}
bool can_expand(const Asset& a,const LayerPath& steps){
    if(steps.size()>=max_depth)return false;
    auto fmt=steps.empty()?a.format:steps.back().format;
    return fmt=="LZ"||fmt=="LZ0"||fmt=="FSU-SPRITE"||fmt=="PNM"||
        ((fmt=="FS"||fmt=="FSU")&&(!steps.empty()||a.member));
}
Bytes layer_bytes(const Asset& a,const LayerPath& steps,std::size_t limit){
    if(steps.size()>max_depth||a.size>limit)throw std::runtime_error("Browser layer limit exceeded");
    auto data=preview(a,static_cast<std::size_t>(a.size));
    for(auto& step:steps){
        if(step.decompress)data=decode_lz(data,limit);
        else{layer_bounds(data,step.offset,step.size);Bytes slice(data.begin()+step.offset,data.begin()+step.offset+step.size);data=std::move(slice);}
        if(data.size()!=step.size)throw std::runtime_error("Layer changed; rescan the source");
    }
    return data;
}
std::vector<LayerStep> layer_children(const Asset& a,const LayerPath& steps){
    if(!can_expand(a,steps))return {};
    auto data=layer_bytes(a,steps,layer_limit);
    auto fmt=steps.empty()?a.format:steps.back().format;
    auto name=steps.empty()?a.name:steps.back().name;
    if(fmt=="FS")return fs_layers(data);
    if(fmt=="FSU")return fsu_layers(data);
    if(fmt=="PNM")return pnm_layers(data);
    auto decoded=decode_lz(data,layer_limit);
    std::string type="DECODED",child_name=name;
    if(fmt=="FSU-SPRITE")type="FSU-DECODED";
    else{
        auto dot=child_name.find_last_of('.');
        if(dot!=std::string::npos)child_name.resize(dot);
        auto explicit_format=layer_format(child_name);
        if(explicit_format=="FS"||explicit_format=="FSU"||explicit_format=="PNM"||
           explicit_format=="ICO"||explicit_format=="BTN"||explicit_format=="CUR"||
           explicit_format=="PAL"||explicit_format=="TXT"||explicit_format=="DEF"||
           explicit_format=="LZ"||explicit_format=="LZ0")type=explicit_format;
        else if(lz_header(decoded)){
            // Header alone is not enough: verify the entire bounded decode
            // before offering an additional compressed layer.
            try{auto next=decode_lz(decoded,layer_limit);if(next!=decoded)type="LZ";}
            catch(const std::exception&){}
        }
        if(type=="DECODED"){
            // FS has no magic; require a nonempty completely valid table
            // and printable member names before offering a nested archive.
            try{auto members=fs_layers(decoded);if(!members.empty())type="FS";}catch(const std::exception&){}
        }
        if(type=="DECODED"&&text_bytes(decoded))type="TEXT";
    }
    return {{true,0,decoded.size(),"Decoded ["+type+"]",type,child_name}};
}
std::optional<Sprite> layer_image(const Asset& a,const LayerPath& steps,std::span<const std::uint8_t> data){
    if(steps.empty())return std::nullopt;
    const auto& last=steps.back();
    if(last.format=="FSU-DECODED")return decode_sprite(data);
    Asset virtual_asset=a;virtual_asset.format=last.format;virtual_asset.name=last.name;
    if(last.format=="DECODED"||last.format=="TEXT"){
        // Retain original named raw-LZ image dimensions for direct payloads.
        if(steps.size()==1&&last.decompress)return decode_image(a,data);
        if(steps.size()>1&&last.decompress){
            virtual_asset.name=steps[steps.size()-2].name;
            virtual_asset.format=steps[steps.size()-2].format;
            return decode_image(virtual_asset,data);
        }
        return std::nullopt;
    }
    if(last.format=="LZ"||last.format=="LZ0"||last.format=="FSU-SPRITE"||
       last.format=="FS"||last.format=="FSU"||last.format=="PNM")return std::nullopt;
    return decode_image(virtual_asset,data);
}
bool layer_text(const Asset& a,const LayerPath& steps,std::span<const std::uint8_t> data){
    if(steps.empty())return is_text_asset(a,data);
    Asset v=a;v.format=steps.back().format;v.name=steps.back().name;
    return v.format=="TEXT"?text_bytes(data):is_text_asset(v,data);
}

}
