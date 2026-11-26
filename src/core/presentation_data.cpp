// FSU group analysis and text rules adapted from MIT restoration tools / SR.
#include "spellcross/presentation_data.hpp"
#include <algorithm>
#include <map>
#include <stdexcept>
namespace sc {
namespace {
std::string upper(std::string s){for(auto& c:s)if(c>='a'&&c<='z')c-='a'-'A';return s;}
std::string leaf(std::string s){auto p=s.find_last_of("/\\:");return upper(p==s.npos?s:s.substr(p+1));}
}
AnimationLayout fsu_layout(std::string_view group,const std::vector<std::string>& n){
    if(n.empty())throw std::runtime_error("Empty FSU resource");
    for(auto& s:n)if(s.size()!=3)throw std::runtime_error("Invalid FSU sprite name");
    AnimationLayout r;auto total=n.size();
    auto f=std::find_if(n.begin(),n.end(),[](auto& s){return s[0]=='F';})-n.begin();
    auto leading=[&](std::size_t end,int column){std::size_t i=0;while(i<end&&n[i][column]==n[0][column])++i;return i;};
    auto stat=[&](std::size_t start,std::size_t count,std::size_t az){
        if(!az||count%az)throw std::runtime_error("Invalid static FSU layout");
        r.static_azimuths=az;r.static_slopes=count/az;
        for(std::size_t s=0;s<count/az;++s){std::vector<std::size_t> row;for(std::size_t a=0;a<az;++a)row.push_back(start+s*az+a);r.stat.push_back(std::move(row));}
    };
    auto anim=[&](std::size_t count,std::size_t az,bool slopes){
        if(!az||count%az)throw std::runtime_error("Invalid animated FSU layout");
        r.frames=count/az;r.animation_azimuths=slopes?0:az;r.animation_slopes=slopes?az:0;
        for(std::size_t a=0;a<az;++a){std::vector<std::size_t> row;for(std::size_t k=0;k<r.frames;++k)row.push_back(slopes?k+a*r.frames:a+k*az);r.anim.push_back(std::move(row));}
    };
    if(std::size_t(f)!=total){stat(f,total-f,total-f);if(f)anim(f,leading(f,1),false);}
    else if(leaf(std::string(group))=="DPEKJ")anim(total,13,true);
    else{
        auto i=std::find_if(n.begin(),n.end(),[](auto& s){return s[1]!='0';})-n.begin();
        if(std::size_t(i)!=total)anim(total,leading(i,1),false);
        else stat(0,total,leading(total,0));
    }
    return r;
}
std::string language_for(const Asset& a,std::string_view edition){
    auto n=leaf(a.name);auto p=n.find_last_of('.');auto ext=p==n.npos?"":n.substr(p+1);
    if(ext=="CZ"||ext=="CZE")return "cs";if(ext=="ENG"||ext=="EN")return "en";
    if(ext=="POL"||ext=="PL")return "pl";if(ext=="ITA"||ext=="IT")return "it";
    return std::string(edition);
}
std::string edition_language(const Inventory& inv){
    bool cz=false,en=false,pl=false,it=false;std::uint64_t size=0;
    for(auto& a:inv.assets){
        if(leaf(block_name(a))!="COMMON.FS")continue;
        auto n=leaf(a.name);
        if(n=="JEDNOTKY.DEF")size=a.size;
        if(n=="UNITS.CZ")cz=true;if(n=="UNITS.ENG")en=true;if(n=="UNITS.PL")pl=true;if(n=="UNITS.IT")it=true;
    }
    if(size&&size%206==0&&size%207!=0&&cz)return "cs";
    if(size&&size%207==0&&size%206!=0){if(pl&&!en&&!it)return "pl";if(it&&!en&&!pl)return "it";if(en&&!pl&&!it)return "en";}
    if(int(cz)+int(en)+int(pl)+int(it)==1)return cz?"cs":en?"en":pl?"pl":"it";
    return "unknown";
}
std::optional<std::vector<Bytes>> language_table(const Asset& a,std::span<const std::uint8_t> d){
    if(!a.member||leaf(block_name(a))!="COMMON.FS"||language_for(a,"unknown")=="unknown")return std::nullopt;
    static const std::map<std::string,unsigned> min={{"BIG_MAP",30},{"BUTTONS",72},{"DISPLAY",2},{"HODNOSTI",18},{"INFOTEXT",35},{"MESIACE",12},{"MOSTY",4},{"MURY",4},{"OTHER",59},{"RESEARCH",140},{"SELTAB",5},{"SPECOBJ",11},{"UNITS",87},{"UPGRADES",22}};
    auto name=leaf(a.name);auto it=min.find(name.substr(0,name.find_last_of('.')));if(it==min.end())return std::nullopt;
    std::vector<Bytes> rows;Bytes line;
    auto flush=[&](){auto p=std::find_if(line.begin(),line.end(),[](auto b){return b!=' '&&b!='\t';});if(p!=line.end()&&*p!=';')rows.push_back(line);line.clear();};
    for(std::size_t i=0;i<d.size();++i){auto b=d[i];if(b==26)break;if(b=='\r'||b=='\n'){flush();if(b=='\r'&&i+1<d.size()&&d[i+1]=='\n')++i;}else line.push_back(b);}flush();
    if(rows.size()<it->second||((it->first=="UNITS"||it->first=="HODNOSTI")&&rows.size()!=it->second))throw std::runtime_error("Invalid positional language table size");
    return rows;
}
Bytes narrative_display(std::span<const std::uint8_t> d){
    Bytes out;unsigned char last=0;
    for(std::size_t i=0;i<d.size();++i){auto b=d[i];if(b=='~'||b==127||(b<32&&b!='\n'&&b!='\r'&&b!='\t'))continue;
        if(b=='\r'||b=='\n'){if(b=='\r'&&i+1<d.size()&&d[i+1]=='\n')++i;if(last=='.'||last=='\n')out.push_back('\n');else if(!out.empty()&&out.back()!=' '&&out.back()!='\n')out.push_back(' ');last='\n';}
        else{if(b!=' ')last=b;out.push_back(b);}
    }return out;
}
}
