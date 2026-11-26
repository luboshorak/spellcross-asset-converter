// FS record layout based on FSarchive in Spellcross Map Editor,
// Copyright (c) 2021 Stanislav Maslan, MIT license.
// Independent bounded, streaming reader for the converter.
#include "spellcross/inventory.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace sc {
namespace fs = std::filesystem;
using U = std::uint64_t;
constexpr U sector = 2048;
constexpr std::size_t max_assets = 200000;
std::string utf8(const fs::path& p) {
    auto u = p.generic_u8string();
    return {u.begin(), u.end()};
}
static std::string upper(std::string s) {
    for (auto& c : s) if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    return s;
}
static std::string format(const std::string& n) {
    auto leaf=n.substr(n.find_last_of("/\\:")+1);
    if(upper(leaf)=="INSTALL.DTA")return "INSTALL-DTA";
    auto i = n.find_last_of('.');
    auto j = n.find_last_of("/\\:");
    return i == std::string::npos || (j != std::string::npos && i < j)
        ? "UNKNOWN" : upper(n.substr(i+1));
}
static std::uint32_t le(const std::uint8_t* b) {
    return std::uint32_t(b[0]) | std::uint32_t(b[1]) << 8 |
           std::uint32_t(b[2]) << 16 | std::uint32_t(b[3]) << 24;
}
static void bounds(U off, U len, U total) {
    if (off > total || len > total-off) throw std::runtime_error("Range outside source");
}
class Reader {
    std::ifstream in;
public:
    U size;
    explicit Reader(const fs::path& p): in(p,std::ios::binary), size(fs::file_size(p)) {
        if (!in) throw std::runtime_error("Cannot open source");
    }
    void read(U off, std::uint8_t* b, std::size_t n) {
        bounds(off,n,size);
        if (!n) return;
        if (off > U(std::numeric_limits<std::streamoff>::max()))
            throw std::runtime_error("Offset too large");
        in.clear();
        in.seekg(static_cast<std::streamoff>(off));
        if (!in.read(reinterpret_cast<char*>(b),static_cast<std::streamsize>(n)))
            throw std::runtime_error("Unexpected end of source");
    }
};
static void add(Inventory& out, Asset a) {
    if (out.assets.size() >= max_assets) throw std::runtime_error("Inventory limit exceeded");
    out.assets.push_back(std::move(a));
}
static void source_asset(Inventory&,Asset,unsigned depth=0);
static void inspect_fs(Inventory& out, const Asset& a,unsigned depth) {
    try {
        Reader r(a.backing);
        bounds(a.offset,a.size,r.size);
        bounds(0,4,a.size);
        std::array<std::uint8_t,21> b{};
        r.read(a.offset,b.data(),4);
        U count = le(b.data());
        if (count > 100000 || count > (a.size-4)/21 ||
            count > max_assets-out.assets.size())
            throw std::runtime_error("Invalid FS entry count");
        const U table = 4+21*count;
        std::vector<Asset> members;
        members.reserve(static_cast<std::size_t>(count));
        for (U i=0;i<count;++i) {
            r.read(a.offset+4+i*21,b.data(),b.size());
            auto end = std::find(b.begin(),b.begin()+13,0);
            std::string n(b.begin(),end);
            if (n.empty()) throw std::runtime_error("Empty FS filename");
            U off=le(b.data()+13), len=le(b.data()+17);
            bounds(off,len,a.size);
            if (len && off<table) throw std::runtime_error("FS member overlaps directory");
            members.push_back({a.backing,a.name+"::"+n,format(n),a.offset+off,len,true,static_cast<std::uint32_t>(i)});
        }
        for (auto& m:members) source_asset(out,std::move(m),depth+1);
        ++out.fs_archives;
    } catch (const std::exception& e) {
        out.warnings.push_back(a.name+": "+e.what());
    }
}
// FSU layout: 4-byte prefix, u32 resource count, 5-byte name/u32 offset.
// Each resource is u16 sprite count followed by 3-byte name/u32 offset/u16 length.
// Layout reference: Stanislav Maslan's MIT-licensed FSUarchive.
static void inspect_fsu(Inventory& out,const Asset& a) {
    try {
        Reader r(a.backing);
        bounds(a.offset,a.size,r.size);
        bounds(0,8,a.size);
        std::array<std::uint8_t,9> b{};
        r.read(a.offset+4,b.data(),4);
        const U count=le(b.data());
        if(count>10000 || count>(a.size-8)/9)
            throw std::runtime_error("Invalid FSU resource count");
        std::vector<Asset> sprites;
        for(U i=0;i<count;++i) {
            r.read(a.offset+8+i*9,b.data(),9);
            auto group=std::string(b.begin(),std::find(b.begin(),b.begin()+5,0));
            while(!group.empty()&&group.back()=='_')group.pop_back();
            if(group.empty())throw std::runtime_error("Empty FSU resource name");
            const U off=le(b.data()+5);
            bounds(off,2,a.size);
            if(off<8+9*count)throw std::runtime_error("FSU resource overlaps header");
            r.read(a.offset+off,b.data(),2);
            const U n=unsigned(b[0])|(unsigned(b[1])<<8);
            bounds(off+2,n*9,a.size);
            if(n>max_assets-out.assets.size()-sprites.size())
                throw std::runtime_error("FSU sprite count exceeds inventory limit");
            for(U j=0;j<n;++j) {
                r.read(a.offset+off+2+j*9,b.data(),9);
                std::string name(b.begin(),std::find(b.begin(),b.begin()+3,0));
                U start=le(b.data()+3),len=unsigned(b[7])|(unsigned(b[8])<<8);
                bounds(start,len,a.size);
                if(name.empty()||len<3)throw std::runtime_error("Invalid FSU sprite entry");
                sprites.push_back({a.backing,a.name+"::"+group+"/"+name,"FSU-SPRITE",a.offset+start,len,true,static_cast<std::uint32_t>(j)});
            }
        }
        out.fsu_resources+=static_cast<std::size_t>(count);
        out.fsu_sprites+=sprites.size();
        for(auto& a:sprites)add(out,std::move(a));
        ++out.fsu_archives;
    } catch(const std::exception& e){out.warnings.push_back(a.name+": "+e.what());}
}
// INSTALL.DTA index layout follows Reloaded main.cpp, ReadInstallDtaIndex.
// The offsets in the file table are relative to the start of this container,
// including when the container itself is an extent inside an ISO image.
static std::string install_directory(std::string raw){
    std::replace(raw.begin(),raw.end(),'\\','/');
    std::string result;
    for(std::size_t pos=0;pos<=raw.size();){
        auto end=raw.find('/',pos);
        auto part=raw.substr(pos,end==raw.npos?raw.npos:end-pos);
        if(part==".."||part.find(':')!=part.npos)
            throw std::runtime_error("Unsafe INSTALL.DTA directory");
        for(unsigned char ch:part)if(ch<32||ch==127)throw std::runtime_error("Invalid INSTALL.DTA directory");
        // DOS root markers (empty, slash, dot) are harmless logical prefixes.
        if(!part.empty()&&part!="."){
            if(!result.empty())result+='/';result+=part;
        }
        if(end==raw.npos)break;pos=end+1;
    }
    return result;
}
static void inspect_install(Inventory& out,const Asset& a,unsigned depth){
    try{
        Reader r(a.backing);bounds(a.offset,a.size,r.size);bounds(0,8,a.size);
        std::array<std::uint8_t,25> b{};
        r.read(a.offset,b.data(),8);
        U dir_size=le(b.data()),dir_count=le(b.data()+4);
        if(dir_size<4||dir_size>1024*1024||!dir_count||dir_count>1024)
            throw std::runtime_error("Invalid INSTALL.DTA directory table");
        U count_pos=4+dir_size;bounds(count_pos,4,a.size);
        std::vector<std::uint8_t> names(static_cast<std::size_t>(dir_size-4));
        r.read(a.offset+8,names.data(),names.size());
        std::vector<std::string> dirs;std::size_t pos=0;
        for(U i=0;i<dir_count;++i){
            if(pos>=names.size())throw std::runtime_error("Truncated INSTALL.DTA directory name");
            auto end=std::find(names.begin()+pos,names.end(),0);
            auto len=static_cast<std::size_t>(end-(names.begin()+pos));
            if(end==names.end()||len>4095)throw std::runtime_error("Unterminated INSTALL.DTA directory name");
            dirs.push_back(install_directory(std::string(names.begin()+pos,end)));pos+=len+1;
        }
        r.read(a.offset+count_pos,b.data(),4);
        U count=le(b.data());
        if(!count||count>100000||count>max_assets-out.assets.size()||count>(a.size-count_pos-4)/25)
            throw std::runtime_error("Invalid INSTALL.DTA file table");
        U records_end=count_pos+4+25*count;
        std::vector<Asset> files;files.reserve(static_cast<std::size_t>(count));
        for(U i=0;i<count;++i){
            r.read(a.offset+count_pos+4+i*25,b.data(),25);
            std::string n(b.begin(),std::find(b.begin(),b.begin()+13,0));
            if(n.empty()||n=="."||n==".."||n.find_first_of("/\\:")!=n.npos)
                throw std::runtime_error("Invalid INSTALL.DTA filename");
            for(unsigned char ch:n)if(ch<32||ch==127)throw std::runtime_error("Invalid INSTALL.DTA filename");
            U off=le(b.data()+13),len=le(b.data()+17),dir=le(b.data()+21);
            if(dir>=dirs.size())throw std::runtime_error("Invalid INSTALL.DTA directory index");
            bounds(off,len,a.size);
            if(len&&off<records_end)throw std::runtime_error("INSTALL.DTA payload overlaps index");
            auto logical=a.name+"::"+(dirs[dir].empty()?"":dirs[dir]+"/")+n;
            files.push_back({a.backing,std::move(logical),format(n),a.offset+off,len,true,static_cast<std::uint32_t>(i)});
        }
        ++out.install_archives;out.install_files+=files.size();
        // Catalog all directories, not only DATA: CD-side and installed
        // resources share the same bounded reader, no extraction required.
        for(auto& child:files)source_asset(out,std::move(child),depth+1);
    }catch(const std::exception& e){out.warnings.push_back(a.name+": "+e.what());}
}
static void source_asset(Inventory& out, Asset a,unsigned depth) {
    a.indexed_container=a.format=="FS"||a.format=="FSU"||a.format=="INSTALL-DTA";
    add(out,a);
    if(!a.indexed_container)return;
    if(depth>=12){out.warnings.push_back(a.name+": archive nesting limit exceeded");return;}
    if(a.format=="FS")inspect_fs(out,a,depth);
    else if(a.format=="FSU")inspect_fsu(out,a);
    else inspect_install(out,a,depth);
}
static void scan_iso(Inventory& out, const fs::path& path) {
    Reader r(path);
    std::array<std::uint8_t,2048> b{};
    U root_off=0, root_size=0;
    bool found=false;
    for (U s=16;s<256 && (s+1)*sector<=r.size;++s) {
        r.read(s*sector,b.data(),b.size());
        if (std::string(reinterpret_cast<char*>(b.data()+1),5)!="CD001" || b[6]!=1)
            throw std::runtime_error("Not an ISO 9660 2048-byte-sector image");
        if (b[0]==255) break;
        if (b[0]!=1) continue;
        if ((unsigned(b[128]) | unsigned(b[129])<<8)!=sector)
            throw std::runtime_error("Unsupported ISO logical block size");
        if (b[156]<34 || b[157]!=0 || !(b[181]&2))
            throw std::runtime_error("Invalid ISO root record");
        root_off=U(le(b.data()+158))*sector;
        root_size=le(b.data()+166);
        found=true;
        break;
    }
    if (!found) throw std::runtime_error("ISO primary volume descriptor not found");
    std::set<std::pair<U,U>> visited;
    auto walk = [&](auto&& self,U off,U size,const std::string& prefix,unsigned depth)->void {
        bounds(off,size,r.size);
        if (depth>32 || size>64*1024*1024 || !visited.emplace(off,size).second)
            throw std::runtime_error("ISO directory cycle or limit exceeded");
        std::array<std::uint8_t,2048> block{};
        for (U pos=0;pos<size;) {
            U remaining=std::min(sector,size-pos);
            r.read(off+pos,block.data(),static_cast<std::size_t>(remaining));
            for (std::size_t i=0;i<remaining;) {
                unsigned n=block[i];
                if (!n) break;
                if (n<34 || n>remaining-i) throw std::runtime_error("Malformed ISO directory record");
                auto* d=block.data()+i;
                unsigned namesz=d[32];
                if (!namesz || 33+namesz>n) throw std::runtime_error("Malformed ISO filename");
                i+=n;
                if (namesz==1 && d[33]<=1) continue;
                std::string name(reinterpret_cast<char*>(d+33),namesz);
                auto ver=name.find(';');
                if (ver!=std::string::npos) name.resize(ver);
                if (name.empty() || name=="." || name==".." ||
                    name.find_first_of("/\\")!=std::string::npos ||
                    name.find('\0')!=std::string::npos)
                    throw std::runtime_error("Invalid ISO filename");
                auto full=prefix+name;
                if (d[1] || d[26] || d[27] || (d[25]&0x80)) {
                    out.warnings.push_back(full+": unsupported extended/interleaved/multi-extent record");
                    continue;
                }
                U start=U(le(d+2))*sector, len=le(d+10);
                bounds(start,len,r.size);
                if (d[25]&2) self(self,start,len,full+"/",depth+1);
                else source_asset(out,{path,full,format(name),start,len,false});
            }
            pos+=remaining;
        }
    };
    walk(walk,root_off,root_size,"",0);
}
Inventory scan(const fs::path& source) {
    Inventory out;
    auto root=fs::canonical(source);
    if (fs::is_directory(root)) {
        out.source_kind="Directory / mounted disc";
        std::error_code ec;
        fs::recursive_directory_iterator it(root,fs::directory_options::none,ec), end;
        if (ec) throw std::runtime_error("Cannot enumerate source directory");
        for (;it!=end;it.increment(ec)) {
            if (ec) throw std::runtime_error("Directory enumeration failed");
            auto status=it->symlink_status();
            if (!fs::is_regular_file(status)) continue;
            auto p=it->path();
            auto n=utf8(p.lexically_relative(root));
            source_asset(out,{p,n,format(n),0,fs::file_size(p),false});
        }
        if (ec) throw std::runtime_error("Directory enumeration failed");
    } else if (fs::is_regular_file(root)) {
        auto n=utf8(root.filename());
        if (format(n)=="ISO") {
            out.source_kind="ISO 9660";
            scan_iso(out,root);
        } else if (format(n)=="FS" || format(n)=="FSU" || format(n)=="INSTALL-DTA") {
            out.source_kind=format(n)+" archive";
            source_asset(out,{root,n,format(n),0,fs::file_size(root),false});
        } else throw std::runtime_error("Choose a directory, .iso, .fs, .fsu or INSTALL.DTA file");
    } else throw std::runtime_error("Unsupported source");
    std::stable_sort(out.assets.begin(),out.assets.end(),[](const Asset& a,const Asset& b){return a.name<b.name;});
    return out;
}
std::vector<std::uint8_t> preview(const Asset& a,std::size_t limit) {
    Reader r(a.backing);
    bounds(a.offset,a.size,r.size);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(std::min<U>(a.size,limit)));
    r.read(a.offset,data.data(),data.size());
    return data;
}
static std::string safe_name(const std::string& logical) {
    auto i=logical.find_last_of("/\\:");
    std::string s=logical.substr(i==std::string::npos?0:i+1);
    for(auto& c:s) if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
        (c>='0'&&c<='9')||c=='.'||c=='_'||c=='-')) c='_';
    if(s.size()>100) s.resize(100);
    while(!s.empty() && s.back()=='.') s.pop_back();
    return s.empty()?"asset":s;
}
static std::string escape(const std::string& s) {
    std::ostringstream o;
    for(unsigned char c:s) {
        if(c=='\\') o<<"\\\\";
        else if(c=='\t') o<<"\\t";
        else if(c=='\n') o<<"\\n";
        else if(c=='\r') o<<"\\r";
        else if(c<32 || c>=127) o<<"\\x"<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(c)<<std::dec;
        else o<<char(c);
    }
    return o.str();
}
std::string manifest(const Inventory& inv) {
    std::ostringstream o;
    o<<"name\tformat\tsize\tmember\toffset\n";
    for(auto& a:inv.assets) o<<escape(a.name)<<'\t'<<a.format<<'\t'<<a.size<<'\t'
        <<(a.member?"yes":"no")<<'\t'<<a.offset<<'\n';
    return o.str();
}
fs::path extract(const std::vector<Asset>& assets,const fs::path& output) {
    if(assets.empty()) throw std::runtime_error("No assets selected");
    fs::path dest;
    for(unsigned i=1;i<=10000;++i) {
        auto p=output/("Spellcross-extract-"+std::to_string(i));
        if(fs::create_directory(p)) {dest=p;break;}
    }
    if(dest.empty()) throw std::runtime_error("Cannot allocate export directory");
    try {
        std::array<std::uint8_t,65536> b{};
        std::ofstream index(dest/"manifest.tsv",std::ios::binary);
        index<<"file\tlogical_name\tsize\n";
        for(std::size_t i=0;i<assets.size();++i) {
            const auto& a=assets[i];
            auto name=std::to_string(i+1)+"_"+safe_name(a.name);
            Reader r(a.backing);
            bounds(a.offset,a.size,r.size);
            std::ofstream out(dest/name,std::ios::binary);
            if(!out) throw std::runtime_error("Cannot create output file");
            for(U pos=0;pos<a.size;) {
                auto n=static_cast<std::size_t>(std::min<U>(b.size(),a.size-pos));
                r.read(a.offset+pos,b.data(),n);
                if(!out.write(reinterpret_cast<char*>(b.data()),n))
                    throw std::runtime_error("Output write failed");
                pos+=n;
            }
            out.close();
            if(!out) throw std::runtime_error("Output close failed");
            index<<name<<'\t'<<escape(a.name)<<'\t'<<a.size<<'\n';
        }
        index.close();
        if(!index) throw std::runtime_error("Manifest write failed");
    } catch (...) {
        // Only remove the directory created by this invocation, never an old export.
        std::error_code ec;
        fs::remove_all(dest,ec);
        throw;
    }
    return dest;
}
}
