#include "spellcross/inventory.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace fs=std::filesystem;
using B=std::vector<std::uint8_t>;
void require(bool b){if(!b)throw std::runtime_error("test assertion failed");}
void le(B& b,std::size_t p,std::uint32_t n){for(int i=0;i<4;i++)b[p+i]=static_cast<std::uint8_t>(n>>(i*8));}
void str(B& b,std::size_t p,const std::string& s){std::copy(s.begin(),s.end(),b.begin()+p);}
void write(const fs::path& p,const B& b){std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),b.size());require(bool(f));}
struct Temp{fs::path p;~Temp(){std::error_code ec;fs::remove_all(p,ec);}};
int main(){
    try{
        Temp t{fs::temp_directory_path()/("sc-converter-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
        require(fs::create_directory(t.p));
        B fsdata(49);le(fsdata,0,2);
        str(fsdata,4,"HELLO.TXT");le(fsdata,17,46);le(fsdata,21,3);
        str(fsdata,25,"../BAD.LZ");le(fsdata,38,49);le(fsdata,42,0);
        str(fsdata,46,"abc");write(t.p/"COMMON.FS",fsdata);
        auto inv=sc::scan(t.p);require(inv.assets.size()==3&&inv.fs_archives==1&&inv.warnings.empty());
        auto member=std::find_if(inv.assets.begin(),inv.assets.end(),[](auto& a){return a.name.ends_with("HELLO.TXT");});
        require(member!=inv.assets.end()&&sc::preview(*member)==B({'a','b','c'}));
        std::vector<sc::Asset> members;for(auto& a:inv.assets)if(a.member)members.push_back(a);
        auto dest=sc::extract(members,t.p);require(fs::exists(dest/"manifest.tsv"));
        require(fs::exists(dest/"1_BAD.LZ")&&fs::exists(dest/"2_HELLO.TXT"));
        require(sc::extract(members,t.p)!=dest);
        B bad=fsdata;le(bad,17,999);write(t.p/"BAD.FS",bad);
        auto badinv=sc::scan(t.p/"BAD.FS");require(badinv.assets.size()==1&&badinv.warnings.size()==1);
        // Minimal ISO 9660 primary volume with one FS file.
        B iso(24*2048);auto p=16*2048;iso[p]=1;str(iso,p+1,"CD001");iso[p+6]=1;iso[p+129]=8;
        auto record=[&](std::size_t off,unsigned extent,unsigned size,unsigned flags,const std::string& name){
            unsigned len=33+static_cast<unsigned>(name.size());if(len%2)++len;
            iso[off]=static_cast<std::uint8_t>(len);le(iso,off+2,extent);le(iso,off+10,size);
            iso[off+25]=static_cast<std::uint8_t>(flags);iso[off+32]=static_cast<std::uint8_t>(name.size());
            str(iso,off+33,name);return len;
        };
        record(p+156,20,2048,2,std::string(1,'\0'));
        auto d=20*2048;d+=record(d,20,2048,2,std::string(1,'\0'));
        d+=record(d,20,2048,2,std::string(1,'\1'));
        record(d,21,static_cast<unsigned>(fsdata.size()),0,"COMMON.FS;1");
        std::copy(fsdata.begin(),fsdata.end(),iso.begin()+21*2048);write(t.p/"game.iso",iso);
        auto image=sc::scan(t.p/"game.iso");require(image.assets.size()==3&&image.fs_archives==1);
        auto it=std::find_if(image.assets.begin(),image.assets.end(),[](auto& a){return a.name.ends_with("HELLO.TXT");});
        require(it!=image.assets.end()&&sc::preview(*it)==B({'a','b','c'}));
        require(sc::manifest(image).find("COMMON.FS::HELLO.TXT")!=std::string::npos);
        std::cout<<"Inventory tests passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
