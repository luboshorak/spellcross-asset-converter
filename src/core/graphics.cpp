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
    bank("T11.FS::MAP.PAL",0,128);
    if(!p.known[0])bank("::MAP.PAL",0,128);
    bank("COMMON.FS::UNITS.PAL",128,96);
    bank("COMMON.FS::SYSTEM.PAL",224,32);
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
    // A container owns its own block even when embedded in INSTALL.DTA.
    if(a.format=="FS"||a.format=="FSU"||a.format=="INSTALL-DTA")return a.name;
    if(a.member){
        auto i=a.name.rfind("::");
        if(i!=std::string::npos){
            auto parent=a.name.substr(0,i);
            auto leaf=parent.substr(parent.find_last_of("/\\:")+1);
            for(auto& c:leaf)if(c>='a'&&c<='z')c-='a'-'A';
            // Installer members outside FS/FSU behave like loose installed files.
            if(leaf!="INSTALL.DTA")return parent;
        }
    }
    return {};
}
static bool in_archive(const Asset& a,std::string_view archive){
    auto block=block_name(a);
    auto slash=block.find_last_of("/\\:");
    if(slash!=std::string::npos)block=block.substr(slash+1);
    for(auto& c:block)if(c>='a'&&c<='z')c-='a'-'A';
    return a.member&&block==archive;
}
bool is_text_asset(const Asset& a,std::span<const std::uint8_t> d){
    if(d.empty())return false;
    // Retail COMMON.FS stores DOS filenames and identifiers in LZ streams.
    // Require printable ASCII in these specifically identified records.
    const auto name=upper_name(a);
    if(a.format=="LZ"&&in_archive(a,"COMMON.FS")&&
       (name=="C02.LZ"||name=="C03.LZ"||name=="C04.LZ"||name=="C05.LZ"||name=="ELMONDO.LZ")){
        return d.size()<=260&&std::all_of(d.begin(),d.end(),[](auto b){return b>=32&&b<=126;});
    }
    bool named=a.format=="DEF"||a.format=="TXT"||a.format=="BRF"||a.format=="TUT"||
        a.format=="ENG"||a.format=="CZE"||a.format=="POL"||a.format=="ITA"||
        a.format=="CZ"||a.format=="EN"||a.format=="PL"||a.format=="IT"||
        a.format=="INI"||a.format=="CSV"||
        in_archive(a,"TEXTS.FS")||(in_archive(a,"RESEARCH.FS")&&a.format=="INF")||
        (in_archive(a,"INFO.FS")&&a.format=="LST");
    if(!named)return false;
    const bool narrative=in_archive(a,"TEXTS.FS")||in_archive(a,"RESEARCH.FS")||(in_archive(a,"INFO.FS")&&a.format=="LST");
    if(!narrative)for(auto b:d)if(b<32&&b!=9&&b!=10&&b!=13&&b!=26)return false;
    return true;
}
// Native geometry from SR strategic renderer and UI coordinate catalogue.
struct RawUi {int w,h;bool transparent;std::string palette;};
static std::optional<RawUi> raw_ui(const Asset& a){
    auto n=upper_name(a);
    if(in_archive(a,"INFO.FS")&&a.format=="DTA"&&n=="PATTERN.DTA")return RawUi{32,32,false,"info"};
    // The RAM1/RAM2 frame family shares the same geometry in the
    // original COMMON.FS. Reuse it for installer copies only when the
    // decoded byte count matches the actual known frame geometry.
    if(a.format=="DTA"&&(in_archive(a,"COMMON.FS")||in_archive(a,"INSTALL.FS"))&&
       n.size()>4&&n.starts_with("RAM")&&(n[3]=='1'||n[3]=='2')){
        if(n.substr(4)=="HORZ.DTA")return RawUi{76,10,true,"frame"};
        if(n.substr(4)=="ROH.DTA")return RawUi{10,10,true,"frame"};
        if(n.substr(4)=="VERT.DTA")return RawUi{10,69,true,"frame"};
    }
    if(a.member&&!block_name(a).empty()&&!in_archive(a,"COMMON.FS"))return std::nullopt;
    // Dimensions verified by reconstructing the English retail pixel arrays.
    if(n=="DIFFIC.LZ")return RawUi{249,152,true,"MAINMENU.PAL"};
    if(n=="GOTOLSTA.LZ")return RawUi{176,91,true,"tactical"};
    if(n=="LED_OFF.LZ"||n=="LED_ON.LZ")return RawUi{9,8,true,"tactical"};
    if(n=="MAINMENU.LZ"||n=="MAINMENU.BIN")return RawUi{640,480,false,"MAINMENU.PAL"};
    if(n=="MAINM_BG.LZ")return RawUi{255,272,false,"MAINMENU.PAL"};
    if((n.starts_with("MAINM_")||n.starts_with("MAINMD"))&&n.ends_with(".LZ")&&n.size()==10)return RawUi{255,62,true,"MAINMENU.PAL"};
    if(n=="TUTMNU.LZ")return RawUi{250,214,true,"MAINMENU.PAL"};
    if(n=="CAULDRON.LZ")return RawUi{640,480,false,"CAULDRON.PAL"};
    if(n=="HELP1024.LZ0")return RawUi{1024,768,false,"HELP1024.PAL"};
    if(n=="HELP800.LZ0")return RawUi{800,600,false,"HELP800.PAL"};
    if(n=="HELP640.LZ0")return RawUi{640,480,false,"HELP640.PAL"};
    if(n=="BIG_MAP.LZ")return RawUi{640,480,false,"strategy"};
    // BM_LSTA geometry confirmed by the retail 5785-pixel image and
    // continuous vertical frame edges at x=0 and x=64.
    if(n=="BM_LSTA.LZ")return RawUi{65,89,false,"strategy"};
    if(n=="VMU_SLCT.LZ")return RawUi{146,17,true,"units"};
    if(n=="ET_BTN0.LZ"||n=="ET_BTN1.LZ"||n=="ET_PAN.LZ")return RawUi{41,36,false,"strategy"};
    if(n=="SB_UP.LZ"||n=="SB_DOWN.LZ")return RawUi{22,28,true,"strategy"};
    if(n=="O_LEDOFF.LZ"||n=="O_LEDON.LZ")return RawUi{14,14,true,"options"};
    if(n=="ULED_OFF.LZ"||n=="ULED_ON.LZ"||n=="ZLED_ON.LZ")return RawUi{4,4,true,"tactical"};
    if(n.size()>=6&&n.starts_with("T0")&&n[2]>='1'&&n[2]<='4'&&
        (n.substr(3)==".LZ"||n.substr(3)=="C.LZ"||n.substr(3)=="CH.LZ"))
        return RawUi{200,31,true,"MAINMENU.PAL"};
    if(n=="DIFF01.LZ"||n=="DIFF02.LZ"||n=="DIFF03.LZ"||n=="DIFF04.LZ")return RawUi{200,31,true,"MAINMENU.PAL"};
    if(n=="MM_LOAD.LZ")return RawUi{402,255,true,"MAINMENU.PAL"};
    if(n=="LOGO0001.LZ"||n=="NO_CD.LZ"||n=="PICTURE.LZ"||n=="SCI_LOGO.LZ"||n=="SKUSKA.LZ")
        return RawUi{640,480,false,n.substr(0,n.size()-3)+".PAL"};
    // Hierarchy overlay dimensions used by form_level.cpp and strategic_hierarchy_layout.h.
    if(n=="BRG_ACT.LZ0")return RawUi{138,171,true,"hierarchy"};
    if(n=="PLK_ACT.LZ0")return RawUi{138,67,true,"hierarchy"};
    if(n=="PRP_ACT.LZ0")return RawUi{142,89,true,"hierarchy"};
    if(n=="UNT_ACT.LZ0")return RawUi{150,22,true,"hierarchy"};
    // Inferred from exact retail decoded lengths and contiguous rectangular borders.
    if(n=="BML_OFF.LZ0"||n=="BML_ON.LZ0")return RawUi{37,24,true,"strategy"};
    if(n=="RTA_SEL.LZ0")return RawUi{132,16,true,"hierarchy"};
    if((n.starts_with("LEVEL_")||n.starts_with("HMLA__"))&&n.size()==11&&n.ends_with(".LZ"))return RawUi{379,259,n.starts_with("HMLA"),"LEVEL_"+n.substr(6,2)+".PAL"};
    if(n.starts_with("VM")&&n.ends_with("_FULL.LZ"))return RawUi{575,480,false,"strategy"};
    static const std::map<std::string,std::pair<int,int>> dims={
        {"BUY.LZ",{406,464}},{"HIERARCH.LZ",{406,464}},{"UNITS.LZ",{406,464}},
        {"RSRCH_BG.LZ",{406,464}},{"INFO.LZ",{412,464}},{"STATS.LZ",{569,464}},
        {"FACTORY.LZ",{569,464}},{"OPTIONS.LZ",{569,464}},
        {"VMM_LST1.LZ",{163,41}},{"VMM_LST2.LZ",{406,174}},
        {"VMU_LST1.LZ",{154,41}},{"VMU_LST2.LZ",{241,141}},
        {"VMB_LST1.LZ",{163,41}},{"VMB_LST2.LZ",{241,141}},
        {"VMB_DIS.LZ",{146,17}},{"VMB_DIS2.LZ",{132,16}},{"VMR_LST1.LZ",{120,40}},
        {"VMO_BAR.LZ",{10,12}},{"SB_BG01.LZ",{163,429}},{"SB_BG02.LZ",{163,472}},
        {"SB_BG03.LZ",{163,287}},{"SB_BG04.LZ",{163,287}},{"SB_BAR01.LZ",{16,373}},
        {"SB_BAR02.LZ",{16,417}},{"SB_BAR03.LZ",{16,231}}
    };
    if(auto it=dims.find(n);it!=dims.end())return RawUi{it->second.first,it->second.second,false,"strategy"};
    return std::nullopt;
}
// The loose SPELCROS.ICO is a Windows ICONDIR/DIB, unlike the game ICO
// scanline format. The retail file has one bottom-up 8-bit indexed image.
struct WindowsIcon {unsigned w,h,colors;std::size_t palette,pixels,mask,stride,mask_stride,end;};
static bool windows_icon_asset(const Asset& a){
    return a.format=="ICO"&&upper_name(a)=="SPELCROS.ICO"&&block_name(a).empty();
}
static WindowsIcon windows_icon(std::span<const std::uint8_t> d){
    auto u16=[&](std::size_t p){return unsigned(d[p])|(unsigned(d[p+1])<<8);};
    auto u32=[&](std::size_t p){return std::uint32_t(d[p])|(std::uint32_t(d[p+1])<<8)|(std::uint32_t(d[p+2])<<16)|(std::uint32_t(d[p+3])<<24);};
    if(d.size()<22||u16(0)!=0||u16(2)!=1||u16(4)!=1)throw std::runtime_error("Unsupported Windows ICO directory");
    const unsigned w=d[6]?d[6]:256,h=d[7]?d[7]:256;
    const std::size_t off=u32(18),len=u32(14);
    if(off<22||off>d.size()||len>d.size()-off||len<40||
       u32(off)!=40||u32(off+4)!=w||u32(off+8)!=2*h||
       u16(off+12)!=1||u16(off+14)!=8||u32(off+16)!=0)
        throw std::runtime_error("Unsupported Windows ICO bitmap");
    const unsigned colors=u32(off+32)?u32(off+32):256;
    if(colors>256)throw std::runtime_error("Invalid Windows ICO palette");
    const std::size_t stride=(w+3u)&~3u,mask_stride=((w+31u)/32u)*4u;
    const auto palette=off+40,pixels=palette+4u*colors,mask=pixels+stride*h,end=mask+mask_stride*h;
    if(end>off+len)throw std::runtime_error("Truncated Windows ICO image");
    return {w,h,colors,palette,pixels,mask,stride,mask_stride,end};
}
Palette asset_palette(const Inventory& inv,const Asset& a,const Palette& tactical){
    auto n=upper_name(a);
    if(windows_icon_asset(a)){
        auto d=decode_asset(a);auto ico=windows_icon(d);Palette p{};
        for(unsigned i=0;i<ico.colors;++i){
            auto pos=ico.palette+4u*i;
            p.rgb[3*i]=d[pos+2];p.rgb[3*i+1]=d[pos+1];p.rgb[3*i+2]=d[pos];p.known[i]=true;
        }
        return p;
    }
    if(in_archive(a,"T11.FS")||in_archive(a,"PUST.FS")||in_archive(a,"DEVAST.FS")){
        Palette p=tactical;
        for(auto& entry:inv.assets){
            if(block_name(entry)!=block_name(a))continue;
            auto bank=upper_name(entry);unsigned first=0,count=0;
            if(bank=="MAP.PAL"&&entry.size==384)count=128;
            if(bank=="CYCLE.PAL"&&entry.size==30){first=240;count=10;}
            if(!count)continue;
            try{auto d=preview(entry,count*3);std::copy(d.begin(),d.end(),p.rgb.begin()+first*3);
                std::fill_n(p.known.begin()+first,count,true);}catch(const std::exception&){}
        }
        return p;
    }
    if(in_archive(a,"INFO.FS")&&(a.format=="LZ"||(a.format=="DTA"&&n=="PATTERN.DTA"))){
        Palette p{};
        for(unsigned i=0;i<256;++i)for(int c=0;c<3;++c)p.rgb[i*3+c]=static_cast<std::uint8_t>(i);
        bool chrome=n=="IBG_768.LZ"||n=="IBG_600.LZ"||n=="INFOPAN.LZ"||n=="INFOLSTA.LZ"||n=="NAMELSTA.LZ"||n=="PATTERN.DTA";
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
    auto ui=raw_ui(a);
    // BTN/ICO have no embedded palette. Select the screen where the resource
    // is actually rendered, not just the file extension. A single shared
    // asset can still be drawn with a different palette by its consumer.
    const bool icon=a.format=="ICO"||a.format=="BTN";
    auto prefix=[&](std::string_view p){return n.starts_with(p);};
    bool info_icon=icon&&(prefix("IINFO_")||prefix("IEXIT_")||prefix("TEXTUP.")||prefix("TEXTDOWN.")||
        prefix("INFOPREV.")||prefix("INFONEXT.")||prefix("INFOPICT.")||prefix("INFOPREM.")||
        prefix("INFOGOTO.")||prefix("INFOTEXT."));
    if(info_icon||((in_archive(a,"INFO.FS"))&&icon)){
        for(auto& entry:inv.assets){
            if(!in_archive(entry,"INFO.FS")||upper_name(entry)!="PALETA.NRM"||entry.size<768)continue;
            try{auto data=preview(entry,768);Palette p{};std::copy(data.begin(),data.end(),p.rgb.begin());p.known.fill(true);return p;}
            catch(const std::exception&){}
        }
        Palette unknown{};for(unsigned i=0;i<256;++i)unknown.rgb[i*3]=unknown.rgb[i*3+1]=unknown.rgb[i*3+2]=i;
        return unknown;
    }
    const bool strategic_icon=icon&&(prefix("VM")||prefix("BIGMB")||prefix("BIGM2")||prefix("BMPAN")||n=="LASTTERT.ICO");
    if(ui&&ui->palette=="tactical")return tactical;
    if(ui&&ui->palette=="frame"){
        if(in_archive(a,"COMMON.FS"))return tactical;
        // The installer palette is not the tactical palette. Only claim
        // colors when the containing archive has one unambiguous full bank.
        Palette p{};
        for(unsigned i=0;i<256;++i)p.rgb[3*i]=p.rgb[3*i+1]=p.rgb[3*i+2]=i;
        const Asset* only=nullptr;
        for(auto& entry:inv.assets)if(block_name(entry)==block_name(a)&&
           entry.backing==a.backing&&(entry.format=="PAL"||entry.format=="NRM")&&entry.size==768){
            if(only)return p;
            only=&entry;
        }
        if(only)try{auto bytes=preview(*only,768);std::copy(bytes.begin(),bytes.end(),p.rgb.begin());p.known.fill(true);}
        catch(const std::exception&){}
        return p;
    }
    if(a.format=="CLK"){
        Palette p{};for(unsigned i=0;i<256;++i){p.rgb[i*3]=p.rgb[i*3+1]=p.rgb[i*3+2]=i;p.known[i]=true;}return p;
    }
    if(!ui&&!strategic_icon)return tactical;
    std::string context=ui?ui->palette:"strategy";
    if(context=="strategy"){
        if(prefix("VMH_")||prefix("HIERARCH"))context="hierarchy";
        else if(prefix("VMU_")||n=="UNITS.LZ")context="units";
        else if(prefix("VMO_")||n=="OPTIONS.LZ")context="options";
    }
    Palette p{};
    for(unsigned i=0;i<256;++i)p.rgb[i*3]=p.rgb[i*3+1]=p.rgb[i*3+2]=i;
    auto bank=[&](std::string_view suffix,unsigned first,unsigned count){
        for(auto& entry:inv.assets){
            if(upper_name(entry)!=suffix||entry.size!=count*3||(!in_archive(entry,"COMMON.FS")&&entry.member&&!block_name(entry).empty()))continue;
            try{auto b=preview(entry,count*3);std::copy(b.begin(),b.end(),p.rgb.begin()+first*3);
                std::fill_n(p.known.begin()+first,count,true);return;}
            catch(const std::exception&){}
        }
    };
    if(context!="strategy"&&context!="hierarchy"&&context!="units"&&context!="options"&&!context.starts_with("LEVEL_"))bank(context,0,256);
    else{
        bank("STRATEGY.PAL",0,256);bank("_SHARED1.PAL",0,128);bank("BIG_MAP.PAL",192,64);
        std::string middle=context=="hierarchy"?"_HIERAR.PAL":context=="units"?"_UNITS.PAL":context=="options"?"_OPTIONS.PAL":context.starts_with("LEVEL_")?context:"";
        if(!middle.empty()){
            std::fill_n(p.known.begin()+128,64,false);
            for(unsigned i=128;i<192;++i)p.rgb[i*3]=p.rgb[i*3+1]=p.rgb[i*3+2]=i;
            bank(middle,128,64);
        }
    }
    return p;
}
static bool terrain_archive(const Asset& a){
    return in_archive(a,"T11.FS")||in_archive(a,"PUST.FS")||in_archive(a,"DEVAST.FS");
}
// The terrain scanline layout follows Sprite::Decode, Maslan (MIT).
static Sprite terrain_sprite(std::span<const std::uint8_t> d){
    if(d.size()<8)throw std::runtime_error("Truncated terrain sprite");
    Sprite s;s.width=80;s.height=d[0];s.shadow_index=false;
    // A zero-height terrain header with remaining nonzero data is corrupt,
    // not an empty valid image. Never invent missing scanlines.
    if(!s.height)throw std::runtime_error("Invalid zero-height terrain header; original retained");
    std::uint32_t origin=std::uint32_t(d[4])|std::uint32_t(d[5])<<8|std::uint32_t(d[6])<<16|std::uint32_t(d[7])<<24;
    s.y=static_cast<std::int32_t>(origin);
    s.indices.resize(std::size_t(s.width)*s.height);s.opaque.resize(s.indices.size());
    std::size_t pos=8;
    for(int y=0;y<s.height;++y){
        if(d.size()-pos<4)throw std::runtime_error("Truncated terrain scanline");
        unsigned x=d[pos],full=d[pos+2],part=d[pos+3];pos+=4;
        if(4*full+8*part>d.size()-pos)throw std::runtime_error("Truncated terrain scanline data");
        // Retail Sprite::Decode trims trailing transparent alignment pixels.
        // Consume and check all encoded groups, even if a zero pad pixel
        // extends beyond the logical 80-pixel tile. Never write out of bounds
        // and never silently discard a nontransparent source pixel.
        auto put=[&](unsigned pixel){
            if(x<80){
                auto at=std::size_t(y)*80+x;
                s.indices[at]=static_cast<std::uint8_t>(pixel);s.opaque[at]=pixel?255:0;
            }else if(pixel)throw std::runtime_error("Nontransparent terrain pixel outside 80-pixel tile");
            ++x;
        };
        for(unsigned j=0;j<full*4;++j)put(d[pos++]);
        for(unsigned j=0;j<part;++j,pos+=8){
            for(unsigned k=0;k<4;++k){if(d[pos+4+k]!=(d[pos+k]?0:255))throw std::runtime_error("Invalid terrain opacity mask");put(d[pos+k]);}
        }
    }
    s.trailing_bytes=d.size()-pos;return s;
}
std::optional<Sprite> decode_image(const Asset& a,std::span<const std::uint8_t> d){
    if(a.format=="ANM-FRAME"||(a.format=="DTA"&&terrain_archive(a)))return terrain_sprite(d);
    if(a.format=="FSU-SPRITE")return decode_sprite(d);
    auto asset_name=upper_name(a);
    if(a.format=="LZ0"&&asset_name.size()==12&&asset_name.starts_with("SELECT_")&&asset_name[7]>='A'&&asset_name[7]<='M'&&asset_name.ends_with(".LZ0")){
        // SELECT_A..M are masked sprite scanlines, not raw bitmaps.
        auto mask=decode_sprite(d);mask.shadow_index=false;return mask;
    }
    Sprite s;s.shadow_index=false;
    auto u16=[&](std::size_t i){return unsigned(d[i])|(unsigned(d[i+1])<<8);};
    auto allocate=[&](){
        if(s.width<=0||s.height<=0||s.width>4096||s.height>4096||
           std::uint64_t(s.width)*s.height>16000000)throw std::runtime_error("Invalid image dimensions");
        s.indices.resize(std::size_t(s.width)*s.height);s.opaque.resize(s.indices.size());
    };
    if(a.format=="CLK"){
        if(d.size()<4)throw std::runtime_error("Truncated CLK header");
        s.height=u16(0);s.width=u16(2);allocate();
        std::size_t table=4+2u*s.height;if(table>d.size())throw std::runtime_error("Truncated CLK offsets");
        for(int y=0;y<s.height;++y){
            auto p=std::size_t(u16(4+2*y)),end=y+1<s.height?std::size_t(u16(6+2*y)):d.size();
            if(p<table||end<p||end>d.size())throw std::runtime_error("Invalid CLK row offset");
            unsigned x=0;while(p+1<end&&x<unsigned(s.width)){
                auto run=d[p++],value=d[p++];unsigned n=std::min<unsigned>(run,s.width-x);
                std::fill_n(s.indices.begin()+std::size_t(y)*s.width+x,n,value);x+=n;
            }
        }
        std::fill(s.opaque.begin(),s.opaque.end(),255);return s;
    }
    if(windows_icon_asset(a)){
        auto ico=windows_icon(d);s.width=ico.w;s.height=ico.h;allocate();
        for(unsigned y=0;y<ico.h;++y){
            auto row=ico.h-1-y;
            for(unsigned x=0;x<ico.w;++x){
                auto at=std::size_t(y)*ico.w+x;
                auto index=d[ico.pixels+row*ico.stride+x];
                if(index>=ico.colors)throw std::runtime_error("Windows ICO palette index outside table");
                s.indices[at]=index;
                s.opaque[at]=(d[ico.mask+row*ico.mask_stride+x/8]&(0x80u>>(x%8)))?0:255;
            }
        }
        s.trailing_bytes=d.size()-ico.end;return s;
    }
    if(a.format=="ICO"||a.format=="BTN"||a.format=="PNM-FRAME"){
        if(d.size()<7)throw std::runtime_error("Truncated ICO header");
        s.y=int(u16(0));if(s.y>=32768)s.y-=65536;
        s.opaque_zero_uses_254=a.format!="PNM-FRAME";
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
    if(auto ui=raw_ui(a)){
        s.width=ui->w;s.height=ui->h;opaque_zero=!ui->transparent;
        if(n=="MAINM_BG.LZ"&&d.size()>=255u*237&&d.size()<=255u*237+64)s.height=237;
        auto expected=std::size_t(s.width)*s.height;
        if(ui->palette=="frame"&&d.size()!=expected)return std::nullopt;
        if(d.size()<expected||d.size()>expected+64)throw std::runtime_error("Unexpected known UI image size");
    }else if(in_archive(a,"INFO.FS")&&a.format=="LZ"){
        if(n=="IBG_768.LZ"){s.width=1024;s.height=768;opaque_zero=true;}
        else if(n=="IBG_600.LZ"){s.width=800;s.height=600;opaque_zero=true;}
        else if(n=="NAMELSTA.LZ"){s.width=200;s.height=80;}
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
        // No arbitrary width for unknown RAM pattern textures.
        else return std::nullopt;
        if(!s.height){if(d.size()%s.width>2)throw std::runtime_error("Raw image length not divisible by known width (except retail padding)");s.height=static_cast<int>(d.size()/s.width);}
    }
    allocate();
    if(s.indices.size()>d.size()-start)throw std::runtime_error("Truncated raw image");
    std::copy_n(d.begin()+start,s.indices.size(),s.indices.begin());
    for(std::size_t i=0;i<s.indices.size();++i)s.opaque[i]=(s.indices[i]||opaque_zero)?255:0;
    s.trailing_bytes=d.size()-start-s.indices.size();
    return s;
}
std::string graphics_evidence(std::span<const std::uint8_t> d){
    std::ostringstream out;
    out<<"decoded_bytes="<<d.size()<<"\n";
    out<<"first_bytes=";
    for(std::size_t i=0;i<std::min<std::size_t>(32,d.size());++i)
        out<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(d[i]);
    out<<std::dec<<"\n";
    // Raw 8-bit arrays carry no width. Enumerate exact possible geometry
    // and up to two retail alignment bytes for diagnosis ONLY; never use
    // this list as proof that an image is decoded.
    out<<"candidate_indexed_dimensions (not verified):";
    for(unsigned padding=0;padding<=2;++padding){
        if(d.size()<=padding)continue;
        auto pixels=d.size()-padding;
        if(pixels>16000000)continue;
        for(unsigned w=1;w<=4096;++w){
            if(pixels%w||pixels/w>4096)continue;
            out<<" "<<w<<"x"<<pixels/w;
            if(padding)out<<"+"<<padding;
        }
    }
    out<<"\n";
    return out.str();
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
     <<",\n  \"opaque_zero_display_index\": "<<(s.opaque_zero_uses_254?"254":"null")
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
    std::stable_sort(result.begin(),result.end(),[&](auto x,auto y){return inv.assets[x].source_ordinal<inv.assets[y].source_ordinal;});
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
std::vector<LayerStep> pnm_layers(std::span<const std::uint8_t> b,bool anm=false){
    if(b.empty()||!b[0]||b.size()<1+4u*b[0])throw std::runtime_error("Invalid PNM frame table");
    std::vector<std::size_t> offsets;
    for(unsigned i=0;i<b[0];++i){
        // AnimPNM::Decode and AnimL1::Decode advance past the count byte
        // before interpreting every offset. Both formats share this base.
        auto off=std::size_t(layer_u32(b,1+4*i))+1;
        if(off<1+4u*b[0])throw std::runtime_error("PNM frame overlaps table");
        layer_bounds(b,off,anm?8:7);offsets.push_back(off);
    }
    auto sorted=offsets;std::sort(sorted.begin(),sorted.end());
    std::vector<LayerStep> out;
    for(std::size_t i=0;i<offsets.size();++i){
        auto next=std::upper_bound(sorted.begin(),sorted.end(),offsets[i]);
        auto end=next==sorted.end()?b.size():*next;
        auto name="Frame "+std::to_string(i+1);
        out.push_back({false,offsets[i],end-offsets[i],name,anm?"ANM-FRAME":"PNM-FRAME",name});
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
    return fmt=="LZ"||fmt=="LZ0"||fmt=="FSU-SPRITE"||fmt=="PNM"||fmt=="ANM"||
        ((fmt=="FS"||fmt=="FSU")&&(!steps.empty()||(a.member&&!a.indexed_container)));
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
    if(fmt=="PNM"||fmt=="ANM")return pnm_layers(data,fmt=="ANM");
    auto decoded=decode_lz(data,layer_limit);
    std::string type="DECODED",child_name=name;
    if(fmt=="FSU-SPRITE")type="FSU-DECODED";
    else{
        auto dot=child_name.find_last_of('.');
        if(dot!=std::string::npos)child_name.resize(dot);
        auto explicit_format=layer_format(child_name);
        if(explicit_format=="FS"||explicit_format=="FSU"||explicit_format=="PNM"||explicit_format=="ANM"||
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
       last.format=="FS"||last.format=="FSU"||last.format=="PNM"||last.format=="ANM")return std::nullopt;
    return decode_image(virtual_asset,data);
}
bool layer_text(const Asset& a,const LayerPath& steps,std::span<const std::uint8_t> data){
    if(steps.empty())return is_text_asset(a,data);
    Asset v=a;v.format=steps.back().format;v.name=steps.back().name;
    return v.format=="TEXT"?text_bytes(data):is_text_asset(v,data);
}

}
