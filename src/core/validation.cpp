#include "spellcross/validation.hpp"
#include "spellcross/semantic.hpp"
#include <algorithm>
#include <charconv>
#include <set>
#include <stdexcept>
#include <variant>
#include <sstream>
#include <iomanip>
#include <tuple>
namespace sc::validation {
namespace {
[[noreturn]] void fail(const std::string& msg){throw std::runtime_error(msg);}
struct J {
    using A=std::vector<J>;using O=std::map<std::string,J>;
    // JSON numbers kept as strings with a distinct type, avoiding rounding.
    struct N{std::string value;bool operator==(const N&)const=default;};
    std::variant<std::nullptr_t,bool,N,std::string,A,O> v;
    bool operator==(const J&)const=default;
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
bool has_resource_catalog(std::string_view root){
    auto j=parse(root);schema(j,"spellcross-dataset");
    auto catalog=j.get("resource_catalog");
    if(!catalog)return false;
    if(catalog->str()!="resources.json")fail("Unsupported resource catalog path");
    return true;
}
std::vector<DatasetPackage> dataset_packages(std::string_view text){
    auto j=parse(text);schema(j,"spellcross-dataset");j.at("partial").boolean();j.at("runtime_ready").boolean();
    if(auto catalog=j.get("resource_catalog");catalog&&catalog->str()!="resources.json")fail("Unsupported resource catalog path");
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
    std::set<std::string> ids,graphics,used_pages,originals,derived;
    std::map<std::string,std::size_t> audit_index;
    out.zip_entries=entries.size();
    for(auto& r:j.at("records").arr()){
        auto id=r.at("id").str(),status=r.at("status").str(),fmt=r.at("format").str();
        if(id.empty()||!ids.insert(id).second||!statuses.contains(status))fail("Duplicate ID or invalid record status: "+id);
        ++out.records;++out.counts[status];++out.formats[key(fmt,status)];
        AuditRecord row;row.id=id;row.name=r.at("name").str();row.format=fmt;row.status=status;
        if(auto detail=r.get("detail"))row.detail=detail->str();
        auto& orig=r.at("original");auto& source=entry(entries,orig.at("entry").str());auto off=orig.at("offset").num(),n=orig.at("size").num();
        if(off>source.size||n>source.size-off)fail("Original range outside entry: "+id);
        row.source_bytes=n;row.original_entry=orig.at("entry").str();row.original_offset=off;originals.insert(row.original_entry);
        if(auto fb=r.get("fallback")){size_is(entry(entries,fb->str()),n);row.fallback_entry=fb->str();originals.insert(row.fallback_entry);}
        if(auto p=r.get("payload")){
            row.payload=p->str();row.payload_bytes=entry(entries,row.payload).size;
            payloads.emplace(id,row.payload);derived.insert(row.payload);
        }else if(status=="converted"||status=="decoded")fail("Missing converted payload: "+id);
        if(status=="container"){row.kind="original-container";row.assessment="preserved-container";}
        else if(status=="raw"||status=="failed"){row.kind="original-only";row.assessment="legacy-required";}
        else if(status=="decoded"){row.kind="decoded-binary";row.assessment="legacy-required";}
        else{row.kind="unclassified";row.assessment="semantic-review";}
        if(auto role=r.get("role")){
            if(role->str()!="legacy-only"||status!="raw"||!row.payload.empty())
                fail("Invalid legacy-only record: "+id);
            sc::Asset source_asset;source_asset.name=row.name;source_asset.format=fmt;source_asset.size=n;
            source_asset.member=row.name.find("::")!=std::string::npos;
            if(!sc::legacy_support_asset(source_asset))fail("Unrecognized legacy-only resource: "+id);
            row.kind="legacy-support";row.assessment="legacy-only";
            row.notes="Preserved original archival/support resource; excluded from required modern game conversion";
        }
        audit_index.emplace(id,out.audit_records.size());
        out.audit_records.push_back(std::move(row));
    }
    auto& palettes=j.at("palettes").arr();
    for(auto& p:palettes){
        auto rgb=p.at("rgb").str(),known=p.at("known").str();
        size_is(entry(entries,rgb),768);size_is(entry(entries,known),256);
        derived.insert(rgb);derived.insert(known);
    }
    std::map<std::string,std::set<std::string>> owners;
    std::map<std::string,std::uint64_t> frame_counts;
    std::set<std::string> placeholders;
    for(auto& g:j.at("graphics").arr()){
        auto name=g.str();if(!graphics.insert(name).second)fail("Duplicate atlas manifest");
        derived.insert(name);
        auto a=parse(entry(entries,name).json);schema(a,"spellcross-atlas");
        std::vector<std::pair<int,int>> dimensions;
        for(auto& p:a.at("pages").arr()){
            auto i=p.at("indices").str(),m=p.at("opacity").str();
            if(i==m||!used_pages.insert(i).second||!used_pages.insert(m).second)fail("Reused atlas page");
            derived.insert(i);derived.insert(m);
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
            auto& row=out.audit_records.at(audit_index.at(id));
            row.atlas_entry=name;++row.atlas_frames;
            if(auto marker=f.get("synthetic_placeholder");marker&&marker->boolean()){
                if(row.status!="failed"||row.format!="DTA"||out.category!="terrain"||
                   !row.name.ends_with("DEVAST.FS::DMA0_270.DTA")||row.source_bytes!=2248||
                   ordinal!=0||w!=80||h!=1||!placeholders.insert(id).second)
                    fail("Invalid damaged terrain placeholder: "+id);
            }
            if(row.format=="FSU-SPRITE"){
                if(!a.get("fsu_layout"))row.notes="Missing FSU layout";
                if(auto error=a.get("fsu_layout_error"))row.notes="FSU layout error: "+error->str();
            }
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
        // Group is source provenance, not a filesystem path. Older external
        // manifests may lack the optional group label.
        const auto group=a.get("group");
        out.atlas_groups.push_back({group?group->str():name,name,seen.size(),a.get("fsu_layout")!=nullptr});
    }
    for(auto& [id,p]:payloads){
        auto& e=entry(entries,p);
        auto& row=out.audit_records.at(audit_index.at(id));
        if(graphics.contains(p)){
            if(!owners[id].contains(p))fail("Missing atlas frames: "+id);
            if(placeholders.contains(id)){
                if(frame_counts[id]!=1)fail("Placeholder has extra frames: "+id);
                row.kind="synthetic-placeholder";row.assessment="placeholder";
                row.notes="Known damaged source; explicit transparent sentinel, original bytes retained, not an authentic decoded graphic";
                continue;
            }
            if(row.status!="converted")fail("Non-converted record has unmarked atlas frames: "+id);
            row.kind="indexed-atlas";
            if(row.format=="FSU-SPRITE"){
                row.assessment="semantic-review";
                if(row.notes.empty())row.notes="FSU layout present; timing, fire origins and compositing need verification";
            }else if(row.format=="ANM"||row.format=="PNM"){
                row.assessment="semantic-review";row.notes="Frame timing and animation semantics need verification";
            }else{
                row.assessment="modern-payload";row.notes="Pixel geometry only; palette and rendering parity not certified";
            }
            continue;
        }
        if(p.ends_with("/font.json")){
            auto f=parse(e.json);schema(f,"spellcross-bitmap-font");
            if(f.at("id").str()!=id||!owners[id].contains(f.at("atlas").str())||f.at("glyph_count").num()!=frame_counts[id])fail("Font atlas mismatch");
            row.kind="bitmap-font";row.notes="Original code positions; Unicode mapping needs review";
        }else if(p.ends_with("/map.json")){
            auto m=parse(e.json);schema(m,"spellcross-tactical-map");
            auto w=m.at("width").num(),h=m.at("height").num();if(!w||!h||w>1000000||h>1000000/w)fail("Invalid map dimensions");
            for(auto layer:{"ground","objects"}){
                auto loc=relative(p,m.at(layer).at("entry").str());
                size_is(entry(entries,loc),w*h*2);derived.insert(loc);
            }
            row.kind="tactical-map";row.notes="Map layers present; resource links and runtime semantics not verified";
        }else if(p.ends_with("/movie.json")){
            auto m=parse(e.json);schema(m,"spellcross-movie");
            auto loc=relative(p,m.at("entry").str());
            if(!m.at("rgb_roundtrip_verified").boolean()||!entry(entries,loc).size)fail("Unverified or empty movie");
            derived.insert(loc);row.kind="lossless-movie";row.assessment="modern-payload";
            row.notes="Exporter reports RGB round-trip; independent timing/audio verification still required";
        }else if(p.ends_with("/retail-table.json")){
            auto t=parse(e.json);schema(t,"spellcross-retail-table");
            const auto kind=t.at("kind").str();
            // Reconstruct source values, and validate both the exact source
            // length and all redundant fields against the same table parser.
            sc::Bytes bytes;
            auto append=[&](const J& values,std::size_t n,unsigned max){
                if(values.arr().size()!=n)fail("Retail table length mismatch: "+id);
                for(const auto& item:values.arr()){
                    auto v=item.num();if(v>max)fail("Retail table value out of range: "+id);
                    bytes.push_back(static_cast<std::uint8_t>(v));
                }
            };
            if(kind=="territory-adjacency"){
                auto& masks=t.at("masks").arr();auto& neighbors=t.at("neighbors").arr();
                if(masks.size()!=32||neighbors.size()!=32)fail("Invalid SSD row count");
                for(unsigned i=0;i<32;++i){
                    auto mask=masks[i].num();if(mask>0xffffffffu)fail("Invalid SSD mask");
                    for(unsigned k=0;k<4;++k)bytes.push_back(static_cast<std::uint8_t>(mask>>(8*k)));
                    std::uint64_t rebuilt=0;
                    for(const auto& item:neighbors[i].arr()){
                        auto bit=item.num();
                        if(bit>=32||(rebuilt&(std::uint64_t(1)<<bit)))fail("Invalid SSD neighbor");
                        rebuilt|=std::uint64_t(1)<<bit;
                    }
                    if(rebuilt!=mask)fail("SSD mask/neighbor mismatch");
                }
            }else if(kind=="gamma-table"){
                auto& rows=t.at("rows").arr();if(rows.size()!=11)fail("Invalid gamma row count");
                for(auto& row:rows)append(row,64,63);
            }else if(kind=="terrain-color-indices"){
                append(t.at("values"),11,255);
                if(t.at("slot_semantics").str()!="unverified")fail("Unexpected terrain color semantics");
            }else if(kind=="info-rgb-palette"||kind=="info-index-filter"){
                append(t.at("values"),kind=="info-rgb-palette"?768:256,255);
                const auto role=t.at("role").str();
                if((kind=="info-rgb-palette"&&(role!="normal"||!row.name.ends_with("PALETA.NRM")))||
                   (kind=="info-index-filter"&&!((role=="dark"&&row.name.ends_with("PALETA.DRK"))||(role=="light"&&row.name.ends_with("PALETA.LGH")))))
                    fail("Invalid INFO palette role");
            }else if(kind=="empty-palette"){
                append(t.at("values"),0,255);append(t.at("original_bytes"),2,255);
            }else fail("Unknown retail table kind: "+kind);
            sc::Asset asset;asset.name=row.name;asset.format=row.format;asset.member=row.name.find("::")!=std::string::npos;
            auto checked=sc::retail_table(asset,bytes);
            if(row.status!="converted"||bytes.size()!=row.source_bytes||!checked||checked->kind!=kind)
                fail("Retail table/source metadata mismatch: "+id);
            row.kind=kind;row.assessment=kind=="terrain-color-indices"?"semantic-review":"modern-payload";
            row.notes=kind=="terrain-color-indices"?"All source indices preserved; slot meaning not verified":"Known table structure; source-byte parity not independently checked";
        }else if(p.ends_with(".json")){
            auto meta=parse(e.json);
            if(p.ends_with("/units.json")){row.kind="unit-definitions";row.notes="Unmapped fields and cross-references need review";}
            else if(p.ends_with("/script.json")){row.kind="ordered-script";row.notes="Syntax preserved; command interpretation and links need review";}
            else if(p.ends_with("/classes.json")){row.kind="class-table";row.notes="Class and resource links need review";}
            else if(p.ends_with("/language.json")){row.kind="language-table";row.notes="Entry IDs and encoding parity need review";}
            else{row.kind="other-json";row.notes="Payload schema and semantics need review";}
        }else if(row.status=="decoded"){row.kind="decoded-binary";}
        else if(p.ends_with("/audio.wav")){row.kind="pcm-wave";row.assessment="modern-payload";row.notes="PCM round-trip checked on export; not re-decoded by this audit";}
        else if(p.ends_with("/music.mid")){row.kind="standard-midi";row.assessment="modern-payload";row.notes="MIDI validation on export; playback parity not certified";}
        else if(p.ends_with("/palette.rgb")){row.kind="rgb-palette";row.assessment="modern-payload";}
        else if(p.ends_with("/filter.u8")){row.kind="index-filter";row.assessment="modern-payload";}
        else if(p.ends_with("/text.txt")||p=="text.txt"){row.kind="utf8-text";row.assessment="modern-payload";row.notes="Encoding and display parity not certified";}
        else{row.kind="other-binary";row.notes="No known modern payload contract";}
        // Companion source text is a derived payload, not a second record.
        if(p.starts_with("records/")){
            auto source_text=relative(p,"source.txt");
            if(entries.contains(source_text))derived.insert(source_text);
        }
    }
    for(auto& [id,groups]:owners)if(!payloads.contains(id)||groups.size()!=1)fail("Orphan atlas frame: "+id);
    for(const auto& [name,e]:entries){
        if(name=="manifest.json")out.metadata_bytes+=e.size;
        else if(originals.contains(name))out.original_bytes+=e.size;
        else if(derived.contains(name))out.derived_bytes+=e.size;
        else out.unattributed_bytes+=e.size;
    }
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

static std::string audit_cell(std::string_view value){
    std::string out;
    for(char c:value){
        if(c=='\\')out+="\\\\";
        else if(c=='\t')out+="\\t";
        else if(c=='\n')out+="\\n";
        else if(c=='\r')out+="\\r";
        else out+=c;
    }
    // Prevent spreadsheet formula interpretation of imported source strings.
    if(!out.empty()&&(out.front()=='='||out.front()=='+'||out.front()=='-'||out.front()=='@'))out.insert(out.begin(),'\'');
    return out;
}
// pack::json encodes every original non-ASCII byte as U+00xx.
static std::string source_name_utf8(std::string_view bytes){
    std::string result;
    for(unsigned char c:bytes){
        if(c<128)result+=char(c);
        else{result+=char(0xc0|(c>>6));result+=char(0x80|(c&63));}
    }
    return result;
}

// Catalog input strings are already parsed UTF-8. Do NOT pass them through
// pack::json (that encoder maps original source bytes to U+00xx).
static std::string catalog_quote(std::string_view value){
    std::ostringstream o;o<<'"';
    constexpr char hex[]="0123456789abcdef";
    for(unsigned char c:value){
        if(c=='"'||c=='\\')o<<'\\'<<char(c);
        else if(c<32)o<<"\\u00"<<hex[c>>4]<<hex[c&15];
        else o<<char(c);
    }
    o<<'"';return o.str();
}
static std::string lookup_key(std::string_view name){
    std::string out(name);
    for(auto& c:out){
        if(c=='\\')c='/';
        else if(c>='a'&&c<='z')c=char(c-'a'+'A');
    }
    return out;
}
static std::string leaf_key(std::string_view name){
    auto p=name.find_last_of("/\\:");
    return lookup_key(p==name.npos?name:name.substr(p+1));
}
std::string resource_catalog(std::string_view root,const std::map<std::string,PackageSummary>& packages){
    validate_dataset(root,packages);
    auto j=parse(root);
    struct Located {const std::string* file;const PackageSummary* package;const AuditRecord* row;};
    std::map<std::string,Located> records;
    std::map<std::string,std::vector<std::string>> names,leaves;
    for(const auto& [file,p]:packages){
        if(p.audit_records.size()!=p.records)fail("Resource catalog record count mismatch: "+file);
        for(const auto& row:p.audit_records){
            if(!records.emplace(row.id,Located{&file,&p,&row}).second)
                fail("Duplicate record ID across PAK packages: "+row.id);
        }
    }
    std::ostringstream o;
    o<<"{\"schema\":\"spellcross-resource-catalog\",\"version\":1,\"partial\":"
     <<(j.at("partial").boolean()?"true":"false");
    if(auto language=j.get("edition_language"))o<<",\"edition_language\":"<<catalog_quote(language->str());
    o<<",\"records\":[\n";
    bool first=true;
    for(const auto& [id,loc]:records){
        const auto& r=*loc.row;
        if(!first)o<<",\n";first=false;
        o<<"{\"id\":"<<catalog_quote(id)<<",\"name\":"<<catalog_quote(r.name)
         <<",\"package\":"<<catalog_quote(*loc.file)<<",\"category\":"<<catalog_quote(loc.package->category)
         <<",\"format\":"<<catalog_quote(r.format)<<",\"status\":"<<catalog_quote(r.status)
         <<",\"kind\":"<<catalog_quote(r.kind)<<",\"assessment\":"<<catalog_quote(r.assessment)
         <<",\"original\":{\"entry\":"<<catalog_quote(r.original_entry)<<",\"offset\":"<<r.original_offset
         <<",\"size\":"<<r.source_bytes<<"}";
        if(!r.fallback_entry.empty())o<<",\"fallback\":"<<catalog_quote(r.fallback_entry);
        if(!r.payload.empty())o<<",\"payload\":"<<catalog_quote(r.payload);
        if(!r.atlas_entry.empty())o<<",\"atlas\":{\"entry\":"<<catalog_quote(r.atlas_entry)
                                   <<",\"frames\":"<<r.atlas_frames<<"}";
        o<<'}';
        names[lookup_key(r.name)].push_back(id);
        leaves[leaf_key(r.name)].push_back(id);
    }
    auto aliases=[&](const auto& map){
        o<<'{';bool first_key=true;
        for(const auto& [key,ids]:map){
            if(!first_key)o<<',';first_key=false;
            o<<catalog_quote(key)<<":[";
            for(std::size_t i=0;i<ids.size();++i){if(i)o<<',';o<<catalog_quote(ids[i]);}
            o<<']';
        }
        o<<'}';
    };
    o<<"\n],\"by_name\":";aliases(names);o<<",\"by_leaf\":";aliases(leaves);
    struct GroupLocation {const std::string* package;const AtlasGroup* atlas;};
    std::vector<GroupLocation> groups;
    for(const auto& [file,p]:packages)for(const auto& g:p.atlas_groups)groups.push_back({&file,&g});
    std::sort(groups.begin(),groups.end(),[](const auto& a,const auto& b){
        return std::tie(*a.package,a.atlas->entry)<std::tie(*b.package,b.atlas->entry);
    });
    std::map<std::string,std::vector<std::size_t>> full_groups,leaf_groups;
    o<<",\"atlas_groups\":[\n";
    for(std::size_t i=0;i<groups.size();++i){
        auto& g=*groups[i].atlas;
        if(i)o<<",\n";
        o<<"{\"group\":"<<catalog_quote(g.group)<<",\"package\":"<<catalog_quote(*groups[i].package)
         <<",\"entry\":"<<catalog_quote(g.entry)<<",\"frames\":"<<g.frames
         <<",\"fsu_layout\":"<<(g.fsu_layout?"true":"false")<<"}";
        full_groups[lookup_key(g.group)].push_back(i);
        leaf_groups[leaf_key(g.group)].push_back(i);
    }
    auto group_aliases=[&](const auto& map){
        o<<'{';bool first_key=true;
        for(const auto& [key,indices]:map){
            if(!first_key)o<<',';first_key=false;
            o<<catalog_quote(key)<<":[";
            for(std::size_t i=0;i<indices.size();++i){if(i)o<<',';o<<indices[i];}
            o<<']';
        }
        o<<'}';
    };
    o<<"\n],\"by_group\":";group_aliases(full_groups);
    o<<",\"by_group_leaf\":";group_aliases(leaf_groups);o<<"}\n";
    auto result=o.str();
    if(result.size()>64u*1024u*1024u)fail("Resource catalog exceeds limit");
    return result;
}
void validate_resource_catalog(std::string_view catalog,std::string_view root,const std::map<std::string,PackageSummary>& packages){
    // Structural equality allows harmless whitespace/key ordering changes,
    // but rejects stale or altered owners, aliases, metadata and locators.
    auto actual=parse(catalog);schema(actual,"spellcross-resource-catalog");
    auto expected=parse(resource_catalog(root,packages));
    if(!(actual==expected))fail("Resource catalog does not match validated PAK manifests");
}

AuditReport audit_report(std::string_view root,const std::map<std::string,PackageSummary>& packages,
                         const std::vector<SourceRecord>* expected_source){
    validate_dataset(root,packages);
    auto j=parse(root);
    std::map<std::string,const SourceRecord*> expected;
    if(expected_source)for(const auto& item:*expected_source)
        if(!expected.emplace(source_name_utf8(item.id),&item).second)fail("Duplicate source inventory ID");
    std::set<std::string> seen_ids;
    std::ostringstream report,rows;
    report<<"PAK EXPORT AUDIT\n\n"
        <<"Structural validation: passed\n"
        <<"Dataset: "<<(j.at("partial").boolean()?"PARTIAL":"full selection")<<"\n"
        <<"Manifest runtime_ready: "<<(j.at("runtime_ready").boolean()?"true":"false")<<" (declared, not certified)\n";
    if(auto language=j.get("edition_language"))report<<"Edition: "<<language->str()<<"\n";
    if(auto warnings=j.get("scan_warnings"))report<<"Source scan warnings: "<<warnings->arr().size()<<"\n";
    report<<"Source inventory match: "<<(expected_source?"checked against selected scan":"not independently checked")<<"\n\n";
    rows<<"package\tcategory\tid\tname\tformat\tstatus\tpayload_kind\tassessment\tsource_bytes\tprimary_payload_bytes\tpayload\tdetail\tnotes\n";
    std::map<std::string,std::uint64_t> totals,kinds,assessments,unresolved;
    std::uint64_t original=0,derived=0,metadata=0,unattributed=0,archive=0,records=0,zip_entries=0,frames=0;
    for(const auto& [file,p]:packages){
        report<<file<<": "<<p.records<<" records; "<<p.frames<<" atlas frames; "
            <<count(p.counts,"converted")<<" converted, "<<count(p.counts,"decoded")<<" decoded, "
            <<count(p.counts,"raw")<<" raw, "<<count(p.counts,"failed")<<" failed, "
            <<count(p.counts,"container")<<" containers\n";
        records+=p.records;zip_entries+=p.zip_entries;frames+=p.frames;
        original+=p.original_bytes;derived+=p.derived_bytes;metadata+=p.metadata_bytes;
        unattributed+=p.unattributed_bytes;archive+=p.archive_bytes;
        for(const auto& [status,n]:p.counts)totals[status]+=n;
        for(const auto& row:p.audit_records){
            if(!seen_ids.insert(row.id).second)fail("Duplicate record ID across PAK packages: "+row.id);
            if(expected_source){
                auto it=expected.find(row.id);
                if(it==expected.end())fail("Unexpected PAK record: "+row.id);
                const auto& item=*it->second;
                if(source_name_utf8(item.name)!=row.name||item.format!=row.format||
                   item.category!=p.category||item.size!=row.source_bytes)
                    fail("PAK source inventory mismatch: "+row.id);
                expected.erase(it);
            }
            ++kinds[row.kind];++assessments[row.assessment];
            if(row.assessment=="legacy-required"||row.assessment=="semantic-review")
                ++unresolved[p.category+"/"+row.format+"/"+row.assessment];
            rows<<audit_cell(file)<<'\t'<<audit_cell(p.category)<<'\t'<<audit_cell(row.id)<<'\t'
                <<audit_cell(row.name)<<'\t'<<audit_cell(row.format)<<'\t'<<audit_cell(row.status)<<'\t'
                <<audit_cell(row.kind)<<'\t'<<audit_cell(row.assessment)<<'\t'<<row.source_bytes<<'\t'
                <<row.payload_bytes<<'\t'<<audit_cell(row.payload)<<'\t'<<audit_cell(row.detail)<<'\t'<<audit_cell(row.notes)<<'\n';
        }
    }
    if(!expected.empty())fail("Missing PAK record from source inventory: "+expected.begin()->first);
    report<<"\nTOTAL: "<<records<<" records, "<<zip_entries<<" ZIP entries, "<<frames<<" atlas frames\n";
    for(const auto& status:statuses)report<<status<<": "<<count(totals,status)<<"\n";
    report<<"\nASSESSMENT (not runtime certification)\n";
    for(const auto& key:{"modern-payload","semantic-review","legacy-only","placeholder","legacy-required","preserved-container"})
        report<<key<<": "<<count(assessments,key)<<"\n";
    report<<"\nUnresolved (legacy-required): "<<count(assessments,"legacy-required")<<"\n";
    report<<"\nPAYLOAD KINDS\n";
    for(const auto& [kind,n]:kinds)report<<kind<<": "<<n<<"\n";
    report<<"\nPHYSICAL PACKAGE STORAGE (bytes; each ZIP entry counted once per package)\n"
        <<"Archive bytes on disk: "<<archive<<"\nOriginal source entries: "<<original
        <<"\nDerived entries: "<<derived<<"\nPackage manifests: "<<metadata
        <<"\nUnattributed entries: "<<unattributed<<"\nTotal uncompressed: "<<original+derived+metadata+unattributed<<"\n";
    report<<"\nREMAINING WORK BY CATEGORY / FORMAT / ASSESSMENT\n";
    for(const auto& [key,n]:unresolved)report<<key<<": "<<n<<"\n";
    report<<"\nSee pak-audit.tsv for every record, conversion reason and review notes.\n"
        <<"Modern-payload means a recognized output representation exists, not gameplay parity.\n"
        <<"Shared primary payload sizes in TSV overlap; use physical totals for storage accounting.\n"
        <<"No source byte comparison, semantic cross-link validation or runtime certification performed.\n";
    return {report.str(),rows.str(),resource_catalog(root,packages)};
}

std::uint32_t crc32_update(std::uint32_t state,const unsigned char* d,std::size_t n){
    static const auto table=[](){std::vector<std::uint32_t> t(256);for(unsigned i=0;i<256;++i){auto c=std::uint32_t(i);for(int j=0;j<8;++j)c=(c>>1)^((c&1)?0xedb88320u:0);t[i]=c;}return t;}();
    for(std::size_t i=0;i<n;++i)state=(state>>8)^table[(state^d[i])&255];return state;
}
}
