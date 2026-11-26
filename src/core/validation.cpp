#include "spellcross/validation.hpp"
#include <algorithm>
#include <charconv>
#include <set>
#include <stdexcept>
#include <variant>
namespace sc::validation {
namespace {
[[noreturn]] void fail(const std::string& msg){throw std::runtime_error(msg);}
struct J {
    using A=std::vector<J>;using O=std::map<std::string,J>;
    // JSON numbers kept as strings with a distinct type, avoiding rounding.
    struct N{std::string value;};
    std::variant<std::nullptr_t,bool,N,std::string,A,O> v;
    const O& obj()const{if(auto p=std::get_if<O>(&v))return *p;fail("Expected JSON object");}
    const A& arr()const{if(auto p=std::get_if<A>(&v))return *p;fail("Expected JSON array");}
    const std::string& str()const{if(auto p=std::get_if<std::string>(&v))return *p;fail("Expected JSON string");}
    const J& at(const std::string& k)const{auto& o=obj();auto i=o.find(k);if(i==o.end())fail("Missing JSON key: "+k);return i->second;}
    const J* get(const std::string& k)const{auto& o=obj();auto i=o.find(k);return i==o.end()?nullptr:&i->second;}
    std::uint64_t num()const{
        auto p=std::get_if<N>(&v);if(!p)fail("Expected JSON integer");
        std::uint64_t n{};auto s=p->value;auto r=std::from_chars(s.data(),s.data()+s.size(),n);
        if(r.ec!=std::errc{}||r.ptr!=s.data()+s.size())fail("Invalid unsigned JSON integer");return n;
    }
    bool boolean()const{if(auto p=std::get_if<bool>(&v))return *p;fail("Expected JSON boolean");}
};
struct Parser{
    std::string_view s;std::size_t p{},nodes{};
    void ws(){while(p<s.size()&&(s[p]==' '||s[p]=='\r'||s[p]=='\n'||s[p]=='\t'))++p;}
    bool eat(char c){ws();if(p<s.size()&&s[p]==c){++p;return true;}return false;}
    void expect(char c){if(!eat(c))fail("Malformed JSON at "+std::to_string(p));}
    unsigned hex4(){unsigned u=0;for(int i=0;i<4;++i){
        if(p==s.size())fail("Truncated JSON escape");char c=s[p++];unsigned x;
        if(c>='0'&&c<='9')x=c-'0';else if(c>='a'&&c<='f')x=c-'a'+10;else if(c>='A'&&c<='F')x=c-'A'+10;else fail("Invalid JSON escape");u=(u<<4)|x;
    }return u;}
    std::string string(){
        expect('"');std::string o;
        while(p<s.size()){
            unsigned char c=s[p++];if(c=='"')return o;
            if(c<32)fail("Control byte in JSON string");
            if(c!='\\'){o+=char(c);continue;}
            if(p==s.size())fail("Truncated JSON string");c=s[p++];
            switch(c){
            case '"':case '\\':case '/':o+=char(c);break;
            case 'b':o+='\b';break;case 'f':o+='\f';break;case 'n':o+='\n';break;case 'r':o+='\r';break;case 't':o+='\t';break;
            case 'u':{
                unsigned u=hex4();
                if(u>=0xd800&&u<=0xdbff){if(p+2>s.size()||s.substr(p,2)!="\\u")fail("Missing JSON low surrogate");p+=2;auto l=hex4();if(l<0xdc00||l>0xdfff)fail("Invalid JSON surrogate");u=0x10000+((u-0xd800)<<10)+l-0xdc00;}
                else if(u>=0xdc00&&u<=0xdfff)fail("Unexpected JSON low surrogate");
                if(u<128)o+=char(u);else if(u<2048){o+=char(0xc0|(u>>6));o+=char(0x80|(u&63));}
                else if(u<65536){o+=char(0xe0|(u>>12));o+=char(0x80|((u>>6)&63));o+=char(0x80|(u&63));}
                else{o+=char(0xf0|(u>>18));o+=char(0x80|((u>>12)&63));o+=char(0x80|((u>>6)&63));o+=char(0x80|(u&63));}break;}
            default:fail("Invalid JSON escape");
            }
        }fail("Unterminated JSON string");
    }
    J value(unsigned depth=0){
        if(depth>32||++nodes>3000000)fail("JSON complexity limit");ws();if(p==s.size())fail("Missing JSON value");
        if(s[p]=='"')return J{string()};
        if(eat('{')){J::O o;if(!eat('}'))for(;;){auto k=string();expect(':');if(!o.emplace(k,value(depth+1)).second)fail("Duplicate JSON key: "+k);if(eat('}'))break;expect(',');}return J{std::move(o)};}
        if(eat('[')){J::A a;if(!eat(']'))for(;;){a.push_back(value(depth+1));if(eat(']'))break;expect(',');}return J{std::move(a)};}
        for(auto literal:{"true","false","null"}){std::string_view l=literal;if(s.substr(p,l.size())==l){p+=l.size();if(l=="null")return J{nullptr};return J{l=="true"};}}
        auto start=p;if(s[p]=='-')++p;
        auto digits=[&](){auto begin=p;while(p<s.size()&&s[p]>='0'&&s[p]<='9')++p;return p>begin;};
        if(p<s.size()&&s[p]=='0')++p;else if(!digits())fail("Invalid JSON number");
        if(p<s.size()&&s[p]=='.'){++p;if(!digits())fail("Invalid JSON fraction");}
        if(p<s.size()&&(s[p]=='e'||s[p]=='E')){++p;if(p<s.size()&&(s[p]=='+'||s[p]=='-'))++p;if(!digits())fail("Invalid JSON exponent");}
        return J{J::N{std::string(s.substr(start,p-start))}};
    }
};
J parse(std::string_view s){if(s.size()>64u*1024u*1024u)fail("JSON exceeds limit");Parser p{s};auto j=p.value();p.ws();if(p.p!=s.size())fail("Trailing JSON data");return j;}
void schema(const J& j,const std::string& name){if(j.at("schema").str()!=name||j.at("version").num()!=1)fail("Unsupported schema: "+name);}
const Entry& entry(const Entries& e,const std::string& name){
    if(!safe_entry(name))fail("Unsafe entry reference: "+name);
    auto it=e.find(name);if(it==e.end())fail("Missing PAK entry: "+name);return it->second;
}
std::string relative(const std::string& path,const std::string& name){auto p=path.find_last_of('/');return (p==std::string::npos?"":path.substr(0,p+1))+name;}
void size_is(const Entry& e,std::uint64_t n){if(e.size!=n)fail("Unexpected payload size");}
const std::set<std::string> statuses{"converted","decoded","raw","failed","container"};
std::string key(const std::string& fmt,const std::string& status){return fmt+"/"+status;}
std::uint64_t count(const std::map<std::string,std::uint64_t>& m,const std::string& k){auto i=m.find(k);return i==m.end()?0:i->second;}
}
bool safe_entry(std::string_view s){
    if(s.empty()||s.size()>1024||s.front()=='/'||s.back()=='/')return false;
    for(unsigned char c:s)if(c<32||c>=127||c=='\\'||c==':')return false;
    while(!s.empty()){auto p=s.find('/');auto part=s.substr(0,p);if(part.empty()||part=="."||part=="..")return false;if(p==s.npos)break;s.remove_prefix(p+1);}
    return true;
}
std::vector<DatasetPackage> dataset_packages(std::string_view text){
    auto j=parse(text);schema(j,"spellcross-dataset");j.at("partial").boolean();j.at("runtime_ready").boolean();
    std::vector<DatasetPackage> result;std::set<std::string> files,categories;
    for(auto& p:j.at("packages").arr()){
        auto file=p.at("file").str(),cat=p.at("category").str();
        if(!safe_entry(file)||file.find('/')!=file.npos||file!=cat+".pak"||!files.insert(file).second||!categories.insert(cat).second)fail("Invalid or duplicate package filename");
        result.push_back({file,cat});
    }
    if(result.empty()||result.size()>100)fail("Invalid dataset package count");return result;
}
PackageSummary validate_package(const Entries& entries,std::string_view expected){
    auto j=parse(entry(entries,"manifest.json").json);schema(j,"spellcross-pak");
    PackageSummary out;out.category=j.at("category").str();if(out.category!=expected)fail("Package category mismatch");
    j.at("partial").boolean();
    std::map<std::string,std::string> payloads;
    std::set<std::string> ids,graphics,used_pages;
    for(auto& r:j.at("records").arr()){
        auto id=r.at("id").str(),status=r.at("status").str(),fmt=r.at("format").str();
        if(id.empty()||!ids.insert(id).second||!statuses.contains(status))fail("Duplicate ID or invalid record status: "+id);
        r.at("name").str();++out.records;++out.counts[status];++out.formats[key(fmt,status)];
        auto& orig=r.at("original");auto& source=entry(entries,orig.at("entry").str());auto off=orig.at("offset").num(),n=orig.at("size").num();
        if(off>source.size||n>source.size-off)fail("Original range outside entry: "+id);
        if(auto fb=r.get("fallback"))size_is(entry(entries,fb->str()),n);
        if(auto p=r.get("payload")){entry(entries,p->str());payloads.emplace(id,p->str());}
        else if(status=="converted"||status=="decoded")fail("Missing converted payload: "+id);
    }
    auto& palettes=j.at("palettes").arr();
    for(auto& p:palettes){size_is(entry(entries,p.at("rgb").str()),768);size_is(entry(entries,p.at("known").str()),256);}
    std::map<std::string,std::set<std::string>> owners;
    std::map<std::string,std::uint64_t> frame_counts;
    for(auto& g:j.at("graphics").arr()){
        auto name=g.str();if(!graphics.insert(name).second)fail("Duplicate atlas manifest");
        auto a=parse(entry(entries,name).json);schema(a,"spellcross-atlas");
        std::vector<std::pair<int,int>> dimensions;
        for(auto& p:a.at("pages").arr()){
            auto i=p.at("indices").str(),m=p.at("opacity").str();
            if(i==m||!used_pages.insert(i).second||!used_pages.insert(m).second)fail("Reused atlas page");
            auto& x=entry(entries,i);auto& y=entry(entries,m);
            if(x.png_width<=0||x.png_height<=0||x.png_width>2048||x.png_height>2048||
               x.png_width!=y.png_width||x.png_height!=y.png_height)fail("Atlas plane dimensions mismatch");
            dimensions.emplace_back(x.png_width,x.png_height);++out.pages;out.png_bytes+=x.size+y.size;
        }
        std::set<std::pair<std::string,std::uint64_t>> seen;
        for(auto& f:a.at("frames").arr()){
            auto id=f.at("id").str();auto ordinal=f.at("frame").num(),page=f.at("page").num();
            if(!ids.contains(id)||!seen.emplace(id,ordinal).second||page>=dimensions.size()||f.at("palette").num()>=palettes.size())fail("Invalid atlas frame reference: "+id);
            auto& rect=f.at("rect").arr();if(rect.size()!=4||f.at("origin").arr().size()!=2)fail("Invalid atlas geometry");
            auto x=rect[0].num(),y=rect[1].num(),w=rect[2].num(),h=rect[3].num();
            auto [pw,ph]=dimensions[page];
            if(!w||!h||x>unsigned(pw)||y>unsigned(ph)||w>unsigned(pw)-x||h>unsigned(ph)-y)fail("Atlas frame outside page: "+id);
            if(ordinal!=frame_counts[id]++)fail("Non-contiguous atlas frame ordinal: "+id);
            owners[id].insert(name);++out.frames;out.pixels+=w*h;
        }
        if(auto layout=a.get("fsu_layout")){
            schema(*layout,"spellcross-fsu-layout");
            auto az=layout->at("static_azimuths").num(),slopes=layout->at("static_slopes").num();
            auto anim_az=layout->at("animation_azimuths").num(),anim_slopes=layout->at("animation_slopes").num(),frames=layout->at("frames_per_sequence").num();
            if(anim_az&&anim_slopes)fail("FSU layout mixes azimuth and slope animation");
            std::set<std::string> used;
            auto check_rows=[&](const J& value,std::uint64_t rows,std::uint64_t columns){
                if(value.arr().size()!=rows)fail("FSU layout row count mismatch");
                for(auto& row:value.arr()){
                    if(row.arr().size()!=columns)fail("FSU layout column count mismatch");
                    for(auto& item:row.arr()){
                        auto id=item.str();
                        if(!seen.contains({id,0})||!used.insert(id).second)fail("Invalid or duplicate FSU layout frame");
                    }
                }
            };
            check_rows(layout->at("static_by_slope"),slopes,az);
            check_rows(layout->at("animation_by_azimuth_or_slope"),anim_az?anim_az:anim_slopes,frames);
            if(used.size()!=seen.size())fail("FSU layout does not cover all atlas frames");
        }
    }
    for(auto& [id,p]:payloads){
        auto& e=entry(entries,p);
        if(graphics.contains(p)){if(!owners[id].contains(p))fail("Missing atlas frames: "+id);continue;}
        if(p.ends_with("/font.json")){
            auto f=parse(e.json);schema(f,"spellcross-bitmap-font");
            if(f.at("id").str()!=id||!owners[id].contains(f.at("atlas").str())||f.at("glyph_count").num()!=frame_counts[id])fail("Font atlas mismatch");
        }else if(p.ends_with("/map.json")){
            auto m=parse(e.json);schema(m,"spellcross-tactical-map");
            auto w=m.at("width").num(),h=m.at("height").num();if(!w||!h||w>1000000||h>1000000/w)fail("Invalid map dimensions");
            for(auto layer:{"ground","objects"})size_is(entry(entries,relative(p,m.at(layer).at("entry").str())),w*h*2);
        }else if(p.ends_with("/movie.json")){
            auto m=parse(e.json);schema(m,"spellcross-movie");
            if(!m.at("rgb_roundtrip_verified").boolean()||!entry(entries,relative(p,m.at("entry").str())).size)fail("Unverified or empty movie");
        }else if(p.ends_with(".json")){parse(e.json);}
    }
    for(auto& [id,groups]:owners)if(!payloads.contains(id)||groups.size()!=1)fail("Orphan atlas frame: "+id);
    return out;
}
void validate_dataset(std::string_view text,const std::map<std::string,PackageSummary>& summaries){
    auto j=parse(text);schema(j,"spellcross-dataset");auto packages=dataset_packages(text);
    if(packages.size()!=summaries.size())fail("Dataset package count mismatch");
    PackageSummary total;std::map<std::string,std::uint64_t> formats;
    for(auto& p:j.at("packages").arr()){
        auto file=p.at("file").str();auto it=summaries.find(file);if(it==summaries.end())fail("Missing package: "+file);
        auto& s=it->second;if(s.category!=p.at("category").str()||s.records!=p.at("records").num())fail("Package record count mismatch");
        for(auto& status:statuses)if(p.at(status=="container"?"containers":status).num()!=count(s.counts,status))fail("Package coverage mismatch: "+file);
        for(auto& [k,n]:s.formats)formats[s.category+"/"+k]+=n;
        total.frames+=s.frames;total.pages+=s.pages;total.pixels+=s.pixels;total.png_bytes+=s.png_bytes;
    }
    std::map<std::string,std::uint64_t> actual;
    for(auto& [fmt,counts]:j.at("coverage_by_format").obj())for(auto& [status,n]:counts.obj())actual[fmt+"/"+status]=n.num();
    if(actual!=formats)fail("Dataset format coverage mismatch");
    auto& m=j.at("atlas_metrics");
    if(m.at("frames").num()!=total.frames||m.at("pages").num()!=total.pages||m.at("source_pixels").num()!=total.pixels||m.at("png_bytes").num()!=total.png_bytes)fail("Dataset atlas metrics mismatch");
}
std::uint32_t crc32_update(std::uint32_t state,const unsigned char* d,std::size_t n){
    static const auto table=[](){std::vector<std::uint32_t> t(256);for(unsigned i=0;i<256;++i){auto c=std::uint32_t(i);for(int j=0;j<8;++j)c=(c>>1)^((c&1)?0xedb88320u:0);t[i]=c;}return t;}();
    for(std::size_t i=0;i<n;++i)state=(state>>8)^table[(state^d[i])&255];return state;
}
}
