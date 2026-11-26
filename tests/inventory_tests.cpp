#include "spellcross/inventory.hpp"
#include "spellcross/graphics.hpp"
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
        // Minimal custom dictionary (A,B), codes A,B,AB,clear,clear.
        // Widths: 2,3,3,3,2 (the first insertion reserves clear slot).
        auto lz=[](const B& raw){
            // Literal-only encoder for synthetic fixture, 256 implicit symbols.
            B packed{0,1,9};unsigned width=9,count=256,bit=0;
            auto put=[&](unsigned code){
                for(int i=static_cast<int>(width)-1;i>=0;--i){
                    if(!bit)packed.push_back(0);
                    packed.back()|=((code>>i)&1)<<(7-bit);
                    bit=(bit+1)%8;
                }
            };
            for(auto c:raw){put(c);++count;if(count+1>=1u<<width)++width;}
            put(256);width=9;put(256);
            return packed;
        };
        B sprite{1,0,128,0, 0xfe,0xff,1,1, 0,128,253,255, 4,5,6,7, 0,255,0,255};
        auto compressed=lz(sprite);
        require(sc::decode_lz(compressed)==sprite);
        auto decoded=sc::decode_sprite(sprite);
        require(decoded.x==-2&&decoded.y==128&&decoded.width==7&&decoded.height==1);
        require(decoded.indices[0]==0&&decoded.opaque[0]==255&&decoded.opaque[5]==0&&decoded.indices[2]==253);
        bool failed=false;try{sc::decode_lz(compressed,2);}catch(const std::exception&){failed=true;}require(failed);
        failed=false;try{sc::decode_sprite(B{1,0,0,0});}catch(const std::exception&){failed=true;}require(failed);
        B fsu(28+compressed.size());le(fsu,4,1);str(fsu,8,"TEST_");le(fsu,13,17);
        fsu[17]=1;str(fsu,19,"000");le(fsu,22,28);
        fsu[26]=compressed.size()&255;fsu[27]=compressed.size()>>8;
        std::copy(compressed.begin(),compressed.end(),fsu.begin()+28);write(t.p/"UNITS.FSU",fsu);
        auto units=sc::scan(t.p/"UNITS.FSU");
        require(units.fsu_archives==1&&units.fsu_resources==1&&units.fsu_sprites==1&&units.assets.size()==2);
        auto spr=std::find_if(units.assets.begin(),units.assets.end(),[](auto& a){return a.format=="FSU-SPRITE";});
        require(spr!=units.assets.end()&&spr->name=="UNITS.FSU::TEST/000"&&sc::decode_asset(*spr)==sprite);
        le(fsu,22,0xffffffff);write(t.p/"BROKEN.FSU",fsu);
        auto broken=sc::scan(t.p/"BROKEN.FSU");require(broken.warnings.size()==1&&broken.assets.size()==1);
        auto group=sc::sprite_group_members(units,*spr);
        require(group.size()==1&&units.assets[group[0]].name==spr->name);
        auto meta=sc::sprite_metadata(*spr,decoded,sc::tactical_palette(units));
        require(meta.find("\"x_offset\": -2")!=std::string::npos);
        require(meta.find("\"shadow_index\": 253")!=std::string::npos);
        require(meta.find("\"stored_bytes\": \"original.bin\"")!=std::string::npos);
        sc::Asset icon;icon.format="ICO";icon.name="COMMON.FS::ICON.ICO";
        B icon_data{0,0,2,0,3,0,1, 2,2,0,33,255};
        auto ico=sc::decode_image(icon,icon_data);
        require(ico&&ico->width==3&&ico->indices[0]==0&&ico->opaque[0]==255&&ico->opaque[2]==0&&!ico->shadow_index);
        sc::Asset raw_icon;raw_icon.format="LZ";raw_icon.name="COMMON.FS::I_TEST.LZ";
        auto raw_img=sc::decode_image(raw_icon,B(120,12));require(raw_img&&raw_img->width==60&&raw_img->height==2);
        sc::Asset txt;txt.name="COMMON.FS::LEVEL1.DEF";txt.format="DEF";txt.member=true;
        require(sc::is_text_asset(txt,B{'A','\n','B'})&&!sc::is_text_asset(txt,B{'A',0,'B'}));
        require(sc::block_name(txt)=="COMMON.FS"&&sc::block_name(*spr)=="UNITS.FSU");
        txt.name="DATA/TEXTS.FS::E02_0001";txt.format="UNKNOWN";
        require(sc::is_text_asset(txt,B{'H','Q'}));
        txt.name="RESEARCH.FS::R001.INF";txt.format="INF";
        require(sc::is_text_asset(txt,B{'R','1'}));
        sc::Asset portrait;portrait.name="INFO.FS::AMX13_1.LZ";portrait.format="LZ";portrait.member=true;
        auto art=sc::decode_image(portrait,B(437*390,0));
        require(art&&art->width==437&&art->height==390&&art->opaque[0]==255);
        // Lazy nested LZ -> FS -> TXT, and a PNM frame table.
        auto inner=lz(fsdata);auto outer=lz(inner);write(t.p/"NESTED.LZ",outer);
        sc::Asset nested{t.p/"NESTED.LZ","NESTED.LZ","LZ",0,outer.size(),false};
        auto first=sc::layer_children(nested,{});require(first.size()==1&&first[0].format=="LZ");
        sc::LayerPath recipe{first[0]};
        auto second=sc::layer_children(nested,recipe);require(second.size()==1&&second[0].format=="DECODED");
        // Test a valid nested FS without the deliberately unsafe filename fixture.
        B clean=fsdata;str(clean,25,"EMPTY.LZ");auto packed=lz(clean);write(t.p/"NESTED.LZ",packed);nested.size=packed.size();
        auto archive=sc::layer_children(nested,{});require(archive.size()==1&&archive[0].format=="FS");
        recipe={archive[0]};auto children=sc::layer_children(nested,recipe);require(children.size()==2);
        recipe.push_back(children[0]);require(sc::layer_bytes(nested,recipe)==B({'a','b','c'}));
        B pnm(5+icon_data.size());pnm[0]=1;le(pnm,1,5);
        std::copy(icon_data.begin(),icon_data.end(),pnm.begin()+5);write(t.p/"ANIM.PNM",pnm);
        sc::Asset anim{t.p/"ANIM.PNM","ANIM.PNM","PNM",0,pnm.size(),false};
        auto frames=sc::layer_children(anim,{});require(frames.size()==1&&frames[0].format=="PNM-FRAME");
        auto frame=sc::layer_image(anim,{frames[0]},sc::layer_bytes(anim,{frames[0]}));
        require(frame&&frame->width==3&&frame->opaque[0]==0&&frame->opaque[1]==255);
        std::cout<<"Inventory, graphics and recursive browser tests passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
