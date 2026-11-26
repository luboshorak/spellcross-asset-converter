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
static void inspect_fs(Inventory& out, const Asset& a) {
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
            members.push_back({a.backing,a.name+"::"+n,format(n),a.offset+off,len,true});
        }
        for (auto& m:members) add(out,std::move(m));
        ++out.fs_archives;
    } catch (const std::exception& e) {
        out.warnings.push_back(a.name+": "+e.what());
    }
}
static void source_asset(Inventory& out, Asset a) {
    add(out,a);
    if (a.format=="FS") inspect_fs(out,a);
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
        } else if (format(n)=="FS") {
            out.source_kind="FS archive";
            source_asset(out,{root,n,"FS",0,fs::file_size(root),false});
        } else throw std::runtime_error("Choose a directory, .iso or .fs file");
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
