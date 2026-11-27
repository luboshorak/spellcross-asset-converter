#include <tuple>
#include "spellcross/inventory.hpp"
#include "spellcross/graphics.hpp"
#include "spellcross/video.hpp"
#include "spellcross/pack.hpp"
#include "spellcross/semantic.hpp"
#include "spellcross/campaign.hpp"
#include "spellcross/music.hpp"
#include "spellcross/validation.hpp"
#include "spellcross/presentation_data.hpp"
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
        // Original DOS installer layout: directory block (including count),
        // then 25-byte file entries and raw payloads. Include both FS/FSU
        // plus a nested directory and an ISO-side file outside INSTALL.DTA.
        std::string dirs("DATA\0SOUND\\VOICE\0",17);
        const std::size_t count_pos=8+dirs.size()+3; // padding in directory block
        const std::size_t records=count_pos+4,end=records+3*25;
        B install(end+fsdata.size()+fsu.size()+3);
        le(install,0,static_cast<std::uint32_t>(count_pos-4));le(install,4,2);str(install,8,dirs);
        le(install,count_pos,3);
        auto install_record=[&](unsigned i,const std::string& name,unsigned offset,unsigned len,unsigned dir){
            auto pos=records+25*i;str(install,pos,name);le(install,pos+13,offset);le(install,pos+17,len);le(install,pos+21,dir);
        };
        install_record(0,"COMMON.FS",end,fsdata.size(),0);
        install_record(1,"UNITS.FSU",end+fsdata.size(),fsu.size(),0);
        install_record(2,"VOICE.WAV",end+fsdata.size()+fsu.size(),3,1);
        std::copy(fsdata.begin(),fsdata.end(),install.begin()+end);
        std::copy(fsu.begin(),fsu.end(),install.begin()+end+fsdata.size());
        str(install,end+fsdata.size()+fsu.size(),"wav");
        auto installed_dir=t.p/"installer";require(fs::create_directory(installed_dir));write(installed_dir/"INSTALL.DTA",install);
        auto verify_install=[&](const sc::Inventory& result,std::uint64_t base){
            require(result.warnings.empty()&&result.install_archives==1&&result.install_files==3&&result.fs_archives==1&&result.fsu_archives==1&&result.fsu_sprites==1);
            auto find=[&](std::string_view suffix)->const sc::Asset&{
                auto it=std::find_if(result.assets.begin(),result.assets.end(),[&](const auto& a){return a.name.ends_with(suffix);});
                require(it!=result.assets.end());return *it;
            };
            auto& common=find("INSTALL.DTA::DATA/COMMON.FS");
            auto& hello=find("COMMON.FS::HELLO.TXT");
            auto& unit=find("UNITS.FSU::TEST/000");
            auto& voice=find("SOUND/VOICE/VOICE.WAV");
            require(common.indexed_container&&!sc::can_expand(common,{}));
            require(hello.offset==base+end+46&&sc::preview(hello)==B({'a','b','c'}));
            require(sc::block_name(hello)==common.name&&sc::pack::category(hello)=="common"&&sc::pack::category(common)=="common");
            require(sc::decode_asset(unit)==sprite&&sc::pack::category(unit)=="units");
            require(sc::block_name(voice).empty()&&sc::pack::category(voice)=="audio"&&sc::preview(voice)==B({'w','a','v'}));
        };
        auto installer=sc::scan(installed_dir/"INSTALL.DTA");require(installer.assets.size()==7);verify_install(installer,0);
        verify_install(sc::scan(installed_dir),0);
        // Re-use the minimal ISO fixture, replacing its directory entries.
        std::fill(iso.begin()+20*2048,iso.end(),0);
        auto install_dir=20*2048;
        install_dir+=record(install_dir,20,2048,2,std::string(1,'\0'));
        install_dir+=record(install_dir,20,2048,2,std::string(1,'\1'));
        install_dir+=record(install_dir,21,install.size(),0,"INSTALL.DTA;1");
        record(install_dir,22,3,0,"CDSIDE.TXT;1");
        require(install.size()<2048);
        std::copy(install.begin(),install.end(),iso.begin()+21*2048);str(iso,22*2048,"cd!");
        write(t.p/"installer.iso",iso);auto disc=sc::scan(t.p/"installer.iso");require(disc.assets.size()==8);verify_install(disc,21*2048);
        auto cdside=std::find_if(disc.assets.begin(),disc.assets.end(),[](auto& a){return a.name=="CDSIDE.TXT";});
        require(cdside!=disc.assets.end()&&sc::preview(*cdside)==B({'c','d','!'}));
        // Malformed installer tables must never index partial or out-of-range data.
        auto malformed=[&](B data){write(installed_dir/"INSTALL.DTA",data);auto bad=sc::scan(installed_dir/"INSTALL.DTA");require(bad.assets.size()==1&&bad.warnings.size()==1&&bad.install_archives==0);};
        auto bad_install=install;le(bad_install,records+13,1);malformed(bad_install);
        bad_install=install;le(bad_install,records+21,99);malformed(bad_install);
        bad_install=install;le(bad_install,records+13,0xffffffffu);malformed(bad_install);
        bad_install=install;le(bad_install,0,4);malformed(bad_install);
        bad_install=install;str(bad_install,8,"../X");malformed(bad_install);
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
        require(ico&&ico->width==3&&ico->indices[0]==0&&ico->opaque[0]==255&&ico->opaque[2]==0&&!ico->shadow_index&&ico->opaque_zero_uses_254);
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
        B pnm(5+icon_data.size());pnm[0]=1;le(pnm,1,4);
        std::copy(icon_data.begin(),icon_data.end(),pnm.begin()+5);write(t.p/"ANIM.PNM",pnm);
        sc::Asset anim{t.p/"ANIM.PNM","ANIM.PNM","PNM",0,pnm.size(),false};
        auto frames=sc::layer_children(anim,{});require(frames.size()==1&&frames[0].format=="PNM-FRAME");
        auto frame=sc::layer_image(anim,{frames[0]},sc::layer_bytes(anim,{frames[0]}));
        require(frame&&frame->width==3&&frame->opaque[0]==0&&frame->opaque[1]==255);
        std::vector<std::int16_t> audio{-32768,0,32767};
        auto wave=sc::pcm_wave(audio,11025,1);
        require(wave.size()==50&&wave[44]==0&&wave[45]==128&&wave[48]==255&&wave[49]==127);
        B dp2(784);le(dp2,0,1);le(dp2,772,2);le(dp2,776,2);write(t.p/"clip.DP2",dp2);
        sc::Asset clip{t.p/"clip.DP2","clip.DP2","DP2",0,dp2.size(),false};
        auto video=sc::decode_video({},clip);require(video.width==2&&video.height==2&&video.frames.size()==1&&video.frames[0]==B(4,0));
        B tile{1,0,2,0,0xff,0xff,0xff,0xff, 2,0,1,0, 0,1,2,3};
        sc::Asset terrain;terrain.name="T11.FS::SLOPE.DTA";terrain.member=true;terrain.format="DTA";
        auto tile_image=sc::decode_image(terrain,tile);
        require(tile_image&&tile_image->width==80&&tile_image->y==-1&&tile_image->opaque[2]==0&&tile_image->indices[3]==1);
        // Real terrain scanlines are aligned to four-pixel blocks.
        // x=79, one visible pixel and three transparent pad pixels.
        B padded{2,0,2,0,0xff,0xff,0xff,0xff,
                 79,0,1,0,9,0,0,0,
                 79,0,0,1,7,0,0,0,0,255,255,255};
        auto padded_tile=sc::decode_image(terrain,padded);
        require(padded_tile&&padded_tile->width==80&&padded_tile->height==2&&
                padded_tile->indices[79]==9&&padded_tile->indices[159]==7&&
                padded_tile->opaque[78]==0&&padded_tile->opaque[158]==0);
        auto invalid_pad=padded;invalid_pad[13]=5;
        bool rejected_pad=false;try{sc::decode_image(terrain,invalid_pad);}catch(const std::exception&){rejected_pad=true;}require(rejected_pad);
        invalid_pad=padded;invalid_pad.back()=0;
        rejected_pad=false;try{sc::decode_image(terrain,invalid_pad);}catch(const std::exception&){rejected_pad=true;}require(rejected_pad);
        invalid_pad=padded;invalid_pad.pop_back();
        rejected_pad=false;try{sc::decode_image(terrain,invalid_pad);}catch(const std::exception&){rejected_pad=true;}require(rejected_pad);
        // Same decode path for nested ANM frame.
        B padded_anm(5+padded.size());padded_anm[0]=1;le(padded_anm,1,4);
        std::copy(padded.begin(),padded.end(),padded_anm.begin()+5);
        write(t.p/"PADDED.ANM",padded_anm);
        sc::Asset pad_anim{t.p/"PADDED.ANM","PADDED.ANM","ANM",0,padded_anm.size(),false};
        auto pad_steps=sc::layer_children(pad_anim,{});
        auto pad_frame=sc::layer_image(pad_anim,{pad_steps.at(0)},sc::layer_bytes(pad_anim,{pad_steps.at(0)}));
        require(pad_frame&&pad_frame->indices==padded_tile->indices&&pad_frame->opaque==padded_tile->opaque);
        B anm(5+tile.size());anm[0]=1;le(anm,1,4);std::copy(tile.begin(),tile.end(),anm.begin()+5);write(t.p/"TILE.ANM",anm);
        sc::Asset anim2{t.p/"TILE.ANM","TILE.ANM","ANM",0,anm.size(),false};
        auto layers=sc::layer_children(anim2,{});require(layers.size()==1&&layers[0].format=="ANM-FRAME");
        auto tile_frame=sc::layer_image(anim2,{layers[0]},sc::layer_bytes(anim2,{layers[0]}));require(tile_frame&&tile_frame->width==80);
        sc::pack::Atlas atlas(8);
        sc::Sprite a1;a1.width=3;a1.height=2;a1.indices={0,253,17,255,1,2};a1.opaque={255,128,0,255,0,255};
        auto r1=atlas.add(a1);auto r2=atlas.add(a1);auto r3=atlas.add(a1);
        require(r1&&r2&&r3&&r1->x==0&&r2->x==3&&r3->x==0&&r3->y==2);
        auto idx=atlas.indices(),mask=atlas.opacity();
        for(auto r:{*r1,*r2,*r3})for(int y=0;y<r.height;++y)for(int x=0;x<r.width;++x){
            auto at=(r.y+y)*atlas.width()+r.x+x,source=y*r.width+x;
            require(idx[at]==a1.indices[source]&&mask[at]==a1.opaque[source]);
        }
        sc::Sprite large;large.width=8;large.height=8;large.indices.resize(64);large.opaque.resize(64);
        require(!atlas.add(large)&&atlas.width()==6&&atlas.height()==4);
        sc::pack::PaletteTable table;sc::Palette pal{};
        require(table.intern(pal)==0&&table.intern(pal)==0);pal.known[0]=true;require(table.intern(pal)==1);
        require(sc::pack::category(terrain)=="terrain"&&sc::pack::category(*spr)=="units");
        require(sc::pack::stable_id(*spr,1)!=sc::pack::stable_id(*spr,2));
        require(sc::pack::json("a\"b\n")=="\"a\\\"b\\u000a\"");
        sc::Asset sound;sound.name="SAMPLES.FS::8TEST";sound.member=true;
        auto sample=sc::decode_sample(sound,B{0,128,255});
        require(sample&&sample->samples==std::vector<std::int16_t>({-32768,0,32512})&&sample->rate==11025);
        auto round=sc::decode_sample(sound,sc::pcm_wave(sample->samples,sample->rate,sample->channels));require(round&&round->samples==sample->samples);
        // Retail RIFF with an odd number of 8-bit PCM samples and no
        // final pad byte: all sample bytes are present and recoverable.
        B odd_wave{'R','I','F','F',39,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,
                   1,0,1,0,17,43,0,0,17,43,0,0,1,0,8,0,'d','a','t','a',3,0,0,0,0,128,255};
        sound.name="SAMPLES.FS::U0614_00";
        auto odd_pcm=sc::decode_sample(sound,odd_wave);
        require(odd_pcm&&odd_pcm->rate==11025&&odd_pcm->samples==sample->samples);
        auto odd_round=sc::decode_sample(sound,sc::pcm_wave(odd_pcm->samples,odd_pcm->rate,odd_pcm->channels));
        require(odd_round&&odd_round->samples==odd_pcm->samples);
        auto padded_wave=odd_wave;padded_wave.push_back(0);le(padded_wave,4,40);
        require(sc::decode_sample(sound,padded_wave)->samples==odd_pcm->samples);
        auto truncated_wave=odd_wave;truncated_wave.pop_back();
        bool truncated_pcm=false;try{sc::decode_sample(sound,truncated_wave);}catch(const std::exception&){truncated_pcm=true;}require(truncated_pcm);
        auto intermediate=odd_wave;intermediate.insert(intermediate.begin()+12,{'J','U','N','K',1,0,0,0,7});
        le(intermediate,4,static_cast<std::uint32_t>(intermediate.size()-8));
        bool bad_intermediate=false;try{sc::decode_sample(sound,intermediate);}catch(const std::exception&){bad_intermediate=true;}require(bad_intermediate);
        sound.name="SAMPLES.FS::MTEST";auto stereo=sc::decode_sample(sound,B{0,0,255,255,0,128,1,128});
        require(stereo&&stereo->channels==2&&stereo->samples==std::vector<std::int16_t>({-32768,32767,0,1}));
        sc::Asset defs;defs.name="COMMON.FS::JEDNOTKY.DEF";B units_data(207);units_data[0]='A';units_data[0x47]=1;units_data[0x48]=2;units_data[0x49]=7;units_data[0xa3]=13;
        auto units_json=sc::unit_definitions(defs,units_data);require(units_json&&units_json->find("\"fire_flags\":513")!=std::string::npos&&units_json->find("\"shot_ap\":7")!=std::string::npos&&units_json->find("\"action_id\":13")!=std::string::npos);
        sc::Asset font;font.name="COMMON.FS::FONT_001.FNT";auto glyphs=sc::decode_font(font,B{1,9,1,0x80,0x80});
        require(glyphs&&glyphs->glyphs.size()==33&&glyphs->glyphs[32].indices[0]==1&&glyphs->glyphs[32].indices[8]==1&&glyphs->glyphs[32].indices[1]==0);
        sc::Asset script;script.format="DEF";script.name="RESEARCH.DEF";
        std::string source="Item(6) {\r\n ORconnections(77)\r\n ORconnections(124)\r\n FutureCall(001,100%,a name,-)\r\n}\x1a";
        auto script_json=sc::script_definition(script,B(source.begin(),source.end()));
        require(script_json&&script_json->find("\"command_count\":3")!=std::string::npos&&script_json->find("\"args\":[\"001\",\"100%\",\"a name\",\"-\"]")!=std::string::npos);
        require(script_json->find("\"args\":[\"77\"]")<script_json->find("\"args\":[\"124\"]"));
        // 1x1 map: one L1 sprite, no L2 objects, no animations, posts or sounds.
        B map_data(26);le(map_data,0,34);le(map_data,4,1);map_data[8]=0x12;map_data[9]=1;map_data[11]=1;
        str(map_data,13,"T11");
        map_data.resize(34);str(map_data,26,"TILE");
        map_data.insert(map_data.end(),{0,0x20}); // elevation 2
        map_data.insert(map_data.end(),4,0); // L2 count
        map_data.insert(map_data.end(),{0,0x81}); // original flags
        map_data.insert(map_data.end(),8*4,0); // L3, L4, L5, L6, sound tables/counts
        map_data.insert(map_data.end(),{0xab,0xcd});
        sc::Asset map_asset;map_asset.format="DTA";map_asset.name="COMMON.FS::M01_01.DTA";
        auto modern_map=sc::tactical_map(map_asset,map_data);
        require(modern_map&&modern_map->ground==B({0,0x20})&&modern_map->objects==B({0,0x81})&&modern_map->metadata.find("\"trailing_hex\":\"abcd\"")!=std::string::npos);
        {
            auto temp_map_asset=map_asset;temp_map_asset.name="COMMON.FS::TEMPFILE.MAP";temp_map_asset.format="MAP";
            auto temp_map=sc::tactical_map(temp_map_asset,map_data);
            require(temp_map&&temp_map->ground==modern_map->ground&&temp_map->objects==modern_map->objects&&temp_map->metadata==modern_map->metadata);
            auto invalid_map=map_data;invalid_map[8]=0;
            require(!sc::tactical_map(temp_map_asset,invalid_map));
        }
        map_data[34]=1;bool rejected=false;try{sc::tactical_map(map_asset,map_data);}catch(const std::exception&){rejected=true;}require(rejected);
        script.name="A_SOUNDS.DEF";source="; preamble\n; [02] attack\nFIRE\n#\nHIT\nMISS\n";
        auto classes=sc::class_definition(script,B(source.begin(),source.end()));
        require(classes&&classes->find("\"id\":2")!=std::string::npos&&classes->find("\"values\":[\"FIRE\",\"#\",\"HIT\",\"MISS\"]")!=std::string::npos);
        script.name="MURY.DEF";source="; typ 0: wall\n4\n100\n5\n";
        classes=sc::class_definition(script,B(source.begin(),source.end()));require(classes&&classes->find("\"max_health\":100")!=std::string::npos);
        script.name="SAMPLES.DEF";source="; [000] first\n#\n; next\nSOUND\n; random\nA\nB\n";
        classes=sc::class_definition(script,B(source.begin(),source.end()));require(classes&&classes->find("\"id\":2")!=std::string::npos&&classes->find("\"values\":[\"A\",\"B\"]")!=std::string::npos);
        // DP2 stored as loose files: its companion audio has a different backing file.
        auto companion=sc::pcm_wave(std::vector<std::int16_t>{0,1,-1},11025,1);
        write(t.p/"clip.wav",companion);
        sc::Asset companion_asset{t.p/"clip.wav","clip.wav","WAV",0,companion.size(),false};
        sc::Inventory clip_inv;clip_inv.assets={clip,companion_asset};
        auto with_audio=sc::decode_video(clip_inv,clip);require(with_audio.wave==companion);
        B midi{'M','T','h','d',0,0,0,6,0,0,0,1,0,96,'M','T','r','k',0,0,0,4,0,0xff,0x2f,0};
        require(sc::music_kind(midi)==sc::MusicKind::midi);sc::validate_midi(midi);
        midi.pop_back();bool bad_midi=false;try{sc::validate_midi(midi);}catch(const std::exception&){bad_midi=true;}require(bad_midi);
        B xmi{'F','O','R','M',0,0,0,14,'X','M','I','D','E','V','N','T',0,0,0,2,0xff,0x2f};
        require(sc::music_kind(xmi)==sc::MusicKind::xmidi);sc::validate_xmidi(xmi);
        xmi[19]=99;bool bad_xmi=false;try{sc::validate_xmidi(xmi);}catch(const std::exception&){bad_xmi=true;}require(bad_xmi);
        using namespace sc::validation;
        {
            // Validate actual embedded unit bytes, not just JSON field names.
            Entries unit_entries;
            unit_entries["original.bin"].size=units_data.size();
            unit_entries["records/1/units.json"].json=*units_json;
            unit_entries["records/1/units.json"].size=units_json->size();
            unit_entries["manifest.json"].json=R"({"schema":"spellcross-pak","version":1,"category":"common","partial":false,"records":[{"id":"units#1","name":"COMMON.FS::JEDNOTKY.DEF","format":"DEF","status":"converted","original":{"entry":"original.bin","offset":0,"size":207},"payload":"records/1/units.json"}],"graphics":[],"palettes":[]})";
            auto unit_summary=validate_package(unit_entries,"common");
            require(unit_summary.metadata_coverage.at("unit-types")==1&&unit_summary.metadata_coverage.at("unit-types-with-explicit-flags")==1);
            require(unit_summary.definitions.size()==1&&unit_summary.definitions[0].ids==std::vector<std::string>{"0"});
            auto& bad_unit=unit_entries["records/1/units.json"].json;
            auto at=bad_unit.find("\"shot_ap\":7");require(at!=std::string::npos);
            bad_unit.replace(at,std::string("\"shot_ap\":7").size(),"\"shot_ap\":8");
            bool bad_unit_rejected=false;try{validate_package(unit_entries,"common");}catch(const std::exception&){bad_unit_rejected=true;}require(bad_unit_rejected);

            // FSA auxiliary metadata: exact signed coordinates and bounds.
            sc::Asset fsa_asset;fsa_asset.name="units.fsa";fsa_asset.format="FSA";
            const std::string magic="SpellFSUcontextV1.0";
            B fsa(magic.begin(),magic.end());fsa.push_back(0);
            auto add32=[&](std::uint32_t n){auto p=fsa.size();fsa.resize(p+4);le(fsa,p,n);};
            add32(1);fsa.push_back(5);fsa.push_back(0);fsa.insert(fsa.end(),{'T','E','S','T',0});
            add32(1);add32(1);add32(0xfffffffeu);add32(3);add32(4);add32(5);
            add32(0);add32(6);add32(7);
            auto context_json=sc::fsu_context(fsa_asset,fsa);
            require(context_json&&context_json->find("[-2,3]")!=std::string::npos);
            require(context_json->find("\"group\":\"TEST\"")!=std::string::npos);
            const auto name_start=magic.size()+1+4+2;
            auto old_fsa=fsa;old_fsa[name_start-2]=4;
            old_fsa.erase(old_fsa.begin()+name_start+4);
            require(sc::fsu_context(fsa_asset,old_fsa)==context_json);
            auto reject_fsa=[&](const B& bytes){
                bool rejected=false;try{sc::fsu_context(fsa_asset,bytes);}catch(const std::exception&){rejected=true;}require(rejected);
            };
            auto bad_name=fsa;bad_name[name_start+1]=0;reject_fsa(bad_name);
            bad_name=fsa;bad_name[name_start+3]=0;reject_fsa(bad_name);
            bad_name=fsa;bad_name[name_start-2]=1;
            bad_name[name_start]=0;bad_name.erase(bad_name.begin()+name_start+1,bad_name.begin()+name_start+5);reject_fsa(bad_name);
            auto duplicate=fsa;duplicate[magic.size()+1]=2;
            B duplicate_name{5,0,'t','e','s','t',0};
            duplicate.insert(duplicate.begin()+name_start+5,duplicate_name.begin(),duplicate_name.end());
            reject_fsa(duplicate);
            Entries fsa_entries;
            fsa_entries["original.bin"].size=fsa.size();
            fsa_entries["records/1/fsu-context.json"].json=*context_json;
            fsa_entries["records/1/fsu-context.json"].size=context_json->size();
            fsa_entries["manifest.json"].json="{\"schema\":\"spellcross-pak\",\"version\":1,\"category\":\"other\",\"partial\":false,\"records\":[{\"id\":\"fsa#1\",\"name\":\"units.fsa\",\"format\":\"FSA\",\"status\":\"converted\",\"original\":{\"entry\":\"original.bin\",\"offset\":0,\"size\":"+std::to_string(fsa.size())+"},\"payload\":\"records/1/fsu-context.json\"}],\"graphics\":[],\"palettes\":[]}";
            auto fsa_summary=validate_package(fsa_entries,"other");
            require(fsa_summary.fsu_contexts.size()==1&&fsa_summary.fsu_contexts[0].static_azimuths==std::vector<std::uint64_t>{1});
            require(fsa_summary.fsu_contexts[0].origin_count==1&&fsa_summary.fsu_contexts[0].zero_origins==0);
            require(fsa_summary.fsu_contexts[0].center_count==2&&fsa_summary.fsu_contexts[0].zero_centers==0);
            auto zero_context=*context_json;
            const auto origin_pos=zero_context.find("[-2,3]");require(origin_pos!=std::string::npos);
            zero_context.replace(origin_pos,6,"[0,0]");
            fsa_entries["records/1/fsu-context.json"].json=zero_context;
            fsa_entries["records/1/fsu-context.json"].size=zero_context.size();
            auto zero_summary=validate_package(fsa_entries,"other");
            require(zero_summary.fsu_contexts[0].zero_origins==1&&zero_summary.metadata_coverage.at("fsu-context-groups-without-nonzero-origins")==1);
            fsa.pop_back();bool truncated_fsa=false;try{sc::fsu_context(fsa_asset,fsa);}catch(const std::exception&){truncated_fsa=true;}require(truncated_fsa);

            // Map tile plane indices are actually validated on read-back.
            Entries map_entries;
            map_entries["original.bin"].size=map_data.size();
            map_entries["records/1/map.json"].json=modern_map->metadata;
            map_entries["records/1/map.json"].size=modern_map->metadata.size();
            map_entries["records/1/ground.u16le"].size=modern_map->ground.size();
            map_entries["records/1/ground.u16le"].tile_bytes=modern_map->ground;
            map_entries["records/1/objects.u8x2"].size=modern_map->objects.size();
            map_entries["records/1/objects.u8x2"].tile_bytes=modern_map->objects;
            map_entries["manifest.json"].json="{\"schema\":\"spellcross-pak\",\"version\":1,\"category\":\"common\",\"partial\":false,\"records\":[{\"id\":\"map#1\",\"name\":\"COMMON.FS::M01_01.DTA\",\"format\":\"DTA\",\"status\":\"converted\",\"original\":{\"entry\":\"original.bin\",\"offset\":0,\"size\":"+std::to_string(map_data.size())+"},\"payload\":\"records/1/map.json\"}],\"graphics\":[],\"palettes\":[]}";
            auto map_summary=validate_package(map_entries,"common");
            require(map_summary.game_references.size()==1&&map_summary.game_references[0].target=="TILE"&&map_summary.game_references[0].scope=="T11.FS");
            map_entries["records/1/ground.u16le"].tile_bytes[0]=1;
            bool invalid_tile=false;try{validate_package(map_entries,"common");}catch(const std::exception&){invalid_tile=true;}require(invalid_tile);

            // Genuine cross-package resolution, not just a synthetic name list.
            Entries terrain_entries;
            terrain_entries["original.bin"].size=4;
            terrain_entries["palette.rgb"].size=768;terrain_entries["palette.known"].size=256;
            terrain_entries["i.png"]={12,1,1,{}};terrain_entries["m.png"]={12,1,1,{}};
            terrain_entries["manifest.json"].json=R"({"schema":"spellcross-pak","version":1,"category":"terrain","partial":false,"records":[{"id":"tile#1","name":"T11.FS::TILE.DTA","format":"DTA","status":"converted","original":{"entry":"original.bin","offset":0,"size":4},"payload":"frames.json"}],"graphics":["frames.json"],"palettes":[{"rgb":"palette.rgb","known":"palette.known"}]})";
            terrain_entries["frames.json"].json=R"({"schema":"spellcross-atlas","version":1,"group":"T11.FS","pages":[{"indices":"i.png","opacity":"m.png"}],"frames":[{"id":"tile#1","frame":0,"page":0,"rect":[0,0,1,1],"origin":[0,0],"palette":0}]})";
            auto terrain_summary=validate_package(terrain_entries,"terrain");
            const auto map_root=R"({"schema":"spellcross-dataset","version":1,"partial":false,"runtime_ready":false,"game_metadata":"game-metadata.json","packages":[{"file":"common.pak","category":"common","records":1,"converted":1,"decoded":0,"raw":0,"failed":0,"containers":0},{"file":"terrain.pak","category":"terrain","records":1,"converted":1,"decoded":0,"raw":0,"failed":0,"containers":0}],"coverage_by_format":{"common/DTA":{"converted":1},"terrain/DTA":{"converted":1}},"atlas_metrics":{"frames":1,"pages":1,"source_pixels":1,"png_bytes":24}})";
            std::map<std::string,PackageSummary> linked{{"common.pak",map_summary},{"terrain.pak",terrain_summary}};
            require(has_game_metadata(map_root));
            auto linked_report=audit_report(map_root,linked);
            require(linked_report.references_tsv.find("TILE\tresolved\t1\tterrain.pak::tile#1::frames.json")!=std::string::npos);
            validate_game_metadata(" \n"+linked_report.game_metadata_json,linked_report.game_metadata_json);
            auto changed_metadata=linked_report.game_metadata_json;
            auto found=changed_metadata.find("\"result\":\"resolved\"");require(found!=std::string::npos);
            changed_metadata.replace(found,std::string("\"result\":\"resolved\"").size(),"\"result\":\"missing\"");
            bool stale_metadata=false;try{validate_game_metadata(changed_metadata,linked_report.game_metadata_json);}catch(const std::exception&){stale_metadata=true;}require(stale_metadata);
            linked["common.pak"].game_references.push_back({"map#1","objects.sprites[0]","terrain-sprite","TILE","PUST.FS"});
            linked["common.pak"].game_references.push_back({"map#1","optional","pnm","#","T11.FS"});
            auto missing_report=audit_report(map_root,linked);
            require(missing_report.references_tsv.find("TILE\tmissing\t0")!=std::string::npos&&missing_report.references_tsv.find("#\toptional-empty\t0")!=std::string::npos);
            auto partial_root=std::string(map_root);partial_root.replace(partial_root.find("\"partial\":false"),15,"\"partial\":true");
            auto partial_report=audit_report(partial_root,linked);
            require(partial_report.references_tsv.find("TILE\tnot-in-selection\t0")!=std::string::npos);
            linked["terrain.pak"].audit_records[0].assessment="placeholder";
            auto placeholder_report=audit_report(map_root,linked);
            require(placeholder_report.references_tsv.find("TILE\tplaceholder\t1")!=std::string::npos);
            linked["common.pak"].definitions.push_back({"map#1","unit",{"1","1"}});
            linked["common.pak"].game_references.push_back({"map#1","ambiguous unit","definition","1","unit"});
            auto ambiguous_report=audit_report(map_root,linked);
            require(ambiguous_report.references_tsv.find("1\tambiguous\t2")!=std::string::npos);

            // Ordered research metadata keeps repeated OR prerequisites.
            Entries research_entries;
            research_entries["original.bin"].size=100;
            research_entries["records/1/script.json"].json=*script_json;
            research_entries["records/1/script.json"].size=script_json->size();
            research_entries["manifest.json"].json=R"({"schema":"spellcross-pak","version":1,"category":"common","partial":false,"records":[{"id":"research#1","name":"COMMON.FS::RESEARCH.DEF","format":"DEF","status":"converted","original":{"entry":"original.bin","offset":0,"size":100},"payload":"records/1/script.json"}],"graphics":[],"palettes":[]})";
            auto research_summary=validate_package(research_entries,"common");
            require(research_summary.game_blocks.size()==1&&research_summary.game_blocks[0].commands.size()==3);
            require(research_summary.game_references.size()==2&&research_summary.game_references[0].target=="77"&&research_summary.game_references[1].target=="124");
            require(research_summary.unreviewed_commands.at("research:FUTURECALL")==1);
            require(research_summary.definitions[0].ids==std::vector<std::string>{"6"});
            // Real Czech tactical directives, including Unicode labels and
            // percent syntax. Event IDs are local, not global unit types.
            sc::Asset mission;mission.format="DEF";mission.name="COMMON.FS::M01_01A.DEF";
            const std::string cz="MissionData {\n AddMissionObjective(DestroyAllUnits,Zni\xc4\x8d" "en\xc3\xad v\xc5\xa1" "ech jednotek)\n AddMissionObjective(SeePlace,648,Goal)\n AddSpecialEvent(SeePlace,648,01,100%)\n AddSpecialEvent(MissionStart,01,50%)\n NightMission()\n}\nEventData(1) {\n EventText(MSG0001)\n}\n";
            auto mission_json=sc::script_definition(mission,B(cz.begin(),cz.end()),true);
            require(mission_json&&mission_json->find("Zni\xc4\x8d" "en\xc3\xad v\xc5\xa1" "ech jednotek")!=std::string::npos);
            require(mission_json->find("\"source_encoding\":\"UTF-8\"")!=std::string::npos);
            Entries mission_entries;
            mission_entries["original.bin"].size=cz.size();
            mission_entries["records/1/script.json"].json=*mission_json;
            mission_entries["records/1/script.json"].size=mission_json->size();
            mission_entries["manifest.json"].json="{\"schema\":\"spellcross-pak\",\"version\":1,\"category\":\"common\",\"partial\":false,\"records\":[{\"id\":\"mission#1\",\"name\":\"COMMON.FS::M01_01A.DEF\",\"format\":\"DEF\",\"status\":\"converted\",\"original\":{\"entry\":\"original.bin\",\"offset\":0,\"size\":"+std::to_string(cz.size())+"},\"payload\":\"records/1/script.json\"}],\"graphics\":[],\"palettes\":[]}";
            auto mission_summary=validate_package(mission_entries,"common");
            require(mission_summary.unreviewed_commands.empty());
            require(mission_summary.metadata_coverage.at("objective:DESTROYALLUNITS")==1);
            require(mission_summary.metadata_coverage.at("objective:SEEPLACE")==1);
            require(mission_summary.metadata_coverage.at("trigger:SEEPLACE")==1);
            require(mission_summary.metadata_coverage.at("trigger:MISSIONSTART")==1);
            require(mission_summary.metadata_coverage.at("night-mission")==1);
            require(mission_summary.game_references.size()==3);
            require(mission_summary.game_references[0].scope=="tactical-event:mission#1"&&mission_summary.game_references[0].target=="1");
            require(mission_summary.definitions[0].ids==std::vector<std::string>{"1"});
            linked["common.pak"].game_references.push_back({"map#1","line 4 MISSIONENDBADTEXT[0]","text","T01_01A.BAD","TEXTS.FS"});
            require(audit_report(map_root,linked).references_tsv.find("T01_01A.BAD\tmissing-end-text\t0")!=std::string::npos);
            require(audit_report(partial_root,linked).references_tsv.find("T01_01A.BAD\tnot-in-selection\t0")!=std::string::npos);
            // Static-only one-frame attack versus actual excessive frame count.
            linked["terrain.pak"].atlas_groups.push_back({"UNITS.FSU::SMOB1","frames.json",16,true,1,16,0,0});
            linked["common.pak"].game_references.push_back({"map#1","unit[7].light_animation","atlas-group","SMOB1","UNITS.FSU",1});
            linked["common.pak"].game_references.push_back({"map#1","unit[7].armor_animation","atlas-group","SMOB1","UNITS.FSU",2});
            const auto static_report=audit_report(map_root,linked);
            require(static_report.references_tsv.find("SMOB1\tstatic-frame-reference\t1")!=std::string::npos);
            require(static_report.references_tsv.find("SMOB1\tframe-limit-review\t1")!=std::string::npos);
            auto en_root=std::string(map_root);en_root.insert(1,"\"edition_language\":\"en\",");
            linked["common.pak"].game_references.push_back({"map#1","music example","music","ABC","MUSIC.FS"});
            require(audit_report(en_root,linked).references_tsv.find("ABC\tedition-absent\t0")!=std::string::npos);
            require(audit_report(map_root,linked).references_tsv.find("ABC\tmissing\t0")!=std::string::npos);
            linked["terrain.pak"].audit_records[0].name="MUSIC.FS";
            require(audit_report(en_root,linked).references_tsv.find("ABC\tmissing\t0")!=std::string::npos);
            // Tutorial mission DEFs use the tactical format, unlike TUTORIAL.DEF.
            mission_entries["manifest.json"].json.replace(mission_entries["manifest.json"].json.find("M01_01A.DEF"),11,"TUT01.DEF");
            auto tutorial_mission=validate_package(mission_entries,"common");
            require(tutorial_mission.unreviewed_commands.empty()&&tutorial_mission.metadata_coverage.at("trigger:SEEPLACE")==1);
        }
        require(safe_entry("graphics/1/page.png")&&!safe_entry("../bad")&&!safe_entry("C:/bad"));
        const unsigned char crc_test[]="123456789";require((crc32_update(0xffffffffu,crc_test,9)^0xffffffffu)==0xcbf43926u);
        Entries entries;
        entries["original.bin"].size=4;
        entries["text.txt"].size=2;
        entries["manifest.json"].json=R"({"schema":"spellcross-pak","version":1,"category":"common","partial":false,"records":[{"id":"a#1","name":"a","format":"TXT","status":"converted","original":{"entry":"original.bin","offset":1,"size":2},"payload":"text.txt"}],"graphics":[],"palettes":[]})";
        auto summary=validate_package(entries,"common");require(summary.records==1&&summary.counts.at("converted")==1);
        auto dataset=R"({"schema":"spellcross-dataset","version":1,"partial":false,"runtime_ready":false,"packages":[{"file":"common.pak","category":"common","records":1,"converted":1,"decoded":0,"raw":0,"failed":0,"containers":0}],"coverage_by_format":{"common/TXT":{"converted":1}},"atlas_metrics":{"frames":0,"pages":0,"source_pixels":0,"png_bytes":0}})";
        validate_dataset(dataset,{{"common.pak",summary}});
        require(summary.audit_records.size()==1&&summary.audit_records[0].kind=="utf8-text"&&
                summary.audit_records[0].assessment=="modern-payload");
        require(summary.original_bytes==4&&summary.derived_bytes==2&&summary.zip_entries==3);
        auto coverage_report=audit_report(dataset,{{"common.pak",summary}});
        require(coverage_report.summary.find("modern-payload: 1")!=std::string::npos&&
                coverage_report.summary.find("not certified")!=std::string::npos&&
                coverage_report.records_tsv.find("utf8-text")!=std::string::npos);
        {
            require(!has_resource_catalog(dataset));
            auto root_with_catalog=std::string(dataset);
            root_with_catalog.insert(1,"\"resource_catalog\":\"resources.json\",");
            require(has_resource_catalog(root_with_catalog));
            const auto one_catalog=resource_catalog(root_with_catalog,{{"common.pak",summary}});
            require(one_catalog==coverage_report.catalog_json);
            validate_resource_catalog(one_catalog,root_with_catalog,{{"common.pak",summary}});
            validate_resource_catalog(" \n"+one_catalog,root_with_catalog,{{"common.pak",summary}});
            require(one_catalog.find("\"original\":{\"entry\":\"original.bin\",\"offset\":1,\"size\":2}")!=std::string::npos);
            require(one_catalog.find("\"by_leaf\":{\"A\":[\"a#1\"]}")!=std::string::npos);
            auto altered_catalog=one_catalog;
            auto owner_pos=altered_catalog.find("\"package\":\"common.pak\"");
            require(owner_pos!=std::string::npos);
            altered_catalog.replace(owner_pos,std::string("\"package\":\"common.pak\"").size(),"\"package\":\"other.pak\"");
            bool bad_catalog=false;try{validate_resource_catalog(altered_catalog,root_with_catalog,{{"common.pak",summary}});}catch(const std::exception&){bad_catalog=true;}require(bad_catalog);
            auto unsafe_root=root_with_catalog;
            auto catalog_pos=unsafe_root.find("resources.json");
            unsafe_root.replace(catalog_pos,14,"../evil.json");
            bool unsafe_catalog=false;try{has_resource_catalog(unsafe_root);}catch(const std::exception&){unsafe_catalog=true;}require(unsafe_catalog);

            // Two packages, same leaf name, distinct stable IDs. Include a
            // byte-preserving non-ASCII source name and duplicate case aliases.
            Entries collision_entries;
            collision_entries["original.bin"].size=4;
            collision_entries["text.txt"].size=2;
            collision_entries["manifest.json"].json=R"({"schema":"spellcross-pak","version":1,"category":"common","partial":false,"records":[{"id":"COMMON.FS::\u00e9/A.TXT#1","name":"COMMON.FS::\u00e9/A.TXT","format":"TXT","status":"converted","original":{"entry":"original.bin","offset":0,"size":2},"payload":"text.txt"}],"graphics":[],"palettes":[]})";
            auto common_collision=validate_package(collision_entries,"common");
            collision_entries["manifest.json"].json=R"({"schema":"spellcross-pak","version":1,"category":"info","partial":false,"records":[{"id":"INFO.FS::a.txt#1","name":"INFO.FS::a.txt","format":"TXT","status":"converted","original":{"entry":"original.bin","offset":2,"size":2},"payload":"text.txt"}],"graphics":[],"palettes":[]})";
            auto info_collision=validate_package(collision_entries,"info");
            const auto collision_root=R"({"schema":"spellcross-dataset","version":1,"partial":false,"runtime_ready":false,"resource_catalog":"resources.json","packages":[{"file":"common.pak","category":"common","records":1,"converted":1,"decoded":0,"raw":0,"failed":0,"containers":0},{"file":"info.pak","category":"info","records":1,"converted":1,"decoded":0,"raw":0,"failed":0,"containers":0}],"coverage_by_format":{"common/TXT":{"converted":1},"info/TXT":{"converted":1}},"atlas_metrics":{"frames":0,"pages":0,"source_pixels":0,"png_bytes":0}})";
            std::map<std::string,PackageSummary> collision_summaries{{"common.pak",common_collision},{"info.pak",info_collision}};
            auto collision_catalog=resource_catalog(collision_root,collision_summaries);
            const std::string first_id="COMMON.FS::\xc3\xa9/A.TXT#1";
            require(collision_catalog.find("\"A.TXT\":[\""+first_id+"\",\"INFO.FS::a.txt#1\"]")!=std::string::npos);
            validate_resource_catalog(collision_catalog,collision_root,collision_summaries);
            auto missing_alias=collision_catalog;
            auto alias_pos=missing_alias.find("\"A.TXT\":[");
            require(alias_pos!=std::string::npos);
            missing_alias.erase(alias_pos+9,first_id.size()+3);
            bool bad_alias=false;try{validate_resource_catalog(missing_alias,collision_root,collision_summaries);}catch(const std::exception&){bad_alias=true;}require(bad_alias);
            collision_summaries.at("info.pak").audit_records[0].id=first_id;
            bool duplicate_global=false;try{resource_catalog(collision_root,collision_summaries);}catch(const std::exception&){duplicate_global=true;}require(duplicate_global);
        }
        std::vector<SourceRecord> source_records{{"a#1","a","TXT","common",2}};
        auto matched_report=audit_report(dataset,{{"common.pak",summary}},&source_records);
        require(matched_report.summary.find("checked against selected scan")!=std::string::npos);
        source_records[0].size=3;
        bool mismatched_source=false;
        try{audit_report(dataset,{{"common.pak",summary}},&source_records);}catch(const std::exception&){mismatched_source=true;}
        require(mismatched_source);
        source_records[0].size=2;
        source_records.push_back({"missing#1","missing","TXT","common",1});
        bool missing_source=false;
        try{audit_report(dataset,{{"common.pak",summary}},&source_records);}catch(const std::exception&){missing_source=true;}
        require(missing_source);
        // Distinguish semantic conversion from decoding and raw fallback.
        Entries audit_entries;
        audit_entries["original/container.bin"].size=100;
        audit_entries["records/1/decoded.bin"].size=20;
        audit_entries["records/1/original.bin"].size=5;
        audit_entries["records/2/script.json"].json="{}";
        audit_entries["records/2/script.json"].size=2;
        audit_entries["records/2/source.txt"].size=12;
        audit_entries["unexpected.bin"].size=7;
        audit_entries["manifest.json"].json=R"({"schema":"spellcross-pak","version":1,"category":"common","partial":false,"records":[
          {"id":"container#1","name":"COMMON.FS","format":"FS","status":"container","original":{"entry":"original/container.bin","offset":0,"size":100}},
          {"id":"decoded#1","name":"DECODED.LZ","format":"LZ","status":"decoded","detail":"unknown syntax","original":{"entry":"original/container.bin","offset":10,"size":5},"fallback":"records/1/original.bin","payload":"records/1/decoded.bin"},
          {"id":"script#1","name":"MISSION.DEF","format":"DEF","status":"converted","original":{"entry":"original/container.bin","offset":20,"size":12},"payload":"records/2/script.json"},
          {"id":"failed#1","name":"BROKEN.DTA","format":"DTA","status":"failed","detail":"invalid header","original":{"entry":"original/container.bin","offset":40,"size":4}}
        ],"graphics":[],"palettes":[]})";
        auto audited=validate_package(audit_entries,"common");
        require(audited.audit_records.size()==4&&audited.original_bytes==105&&audited.derived_bytes==34&&audited.unattributed_bytes==7);
        require(audited.audit_records[0].assessment=="preserved-container"&&
                audited.audit_records[1].assessment=="legacy-required"&&audited.audit_records[1].kind=="decoded-binary"&&
                audited.audit_records[2].assessment=="semantic-review"&&audited.audit_records[2].kind=="ordered-script"&&
                audited.audit_records[3].assessment=="legacy-required"&&audited.audit_records[3].detail=="invalid header");
        entries["original.bin"].size=2;bool bad_range=false;try{validate_package(entries,"common");}catch(const std::exception&){bad_range=true;}require(bad_range);
        entries["original.bin"].size=4;
        entries["palette.rgb"].size=768;entries["palette.known"].size=256;
        entries["i.png"]={12,4,4,{}};entries["m.png"]={12,4,4,{}};
        entries["manifest.json"].json=R"({"schema":"spellcross-pak","version":1,"category":"units","partial":false,"records":[{"id":"a#1","name":"a","format":"FSU-SPRITE","status":"converted","original":{"entry":"original.bin","offset":0,"size":4},"payload":"frames.json"}],"graphics":["frames.json"],"palettes":[{"rgb":"palette.rgb","known":"palette.known"}]})";
        entries["frames.json"].json=R"({"schema":"spellcross-atlas","version":1,"group":"a","pages":[{"indices":"i.png","opacity":"m.png"}],"frames":[{"id":"a#1","frame":0,"page":0,"rect":[1,1,3,3],"origin":[-3,4],"palette":0,"shadow_index":253,"trailing_bytes":0}]})";
        auto graphics_summary=validate_package(entries,"units");require(graphics_summary.frames==1&&graphics_summary.pixels==9&&graphics_summary.png_bytes==24);
        require(graphics_summary.audit_records.at(0).assessment=="semantic-review"&&
                graphics_summary.audit_records.at(0).notes=="Missing FSU layout");
        {
            require(graphics_summary.atlas_groups.size()==1&&graphics_summary.atlas_groups[0].group=="a"&&
                    graphics_summary.atlas_groups[0].entry=="frames.json"&&graphics_summary.atlas_groups[0].frames==1&&
                    !graphics_summary.atlas_groups[0].fsu_layout);
            require(graphics_summary.audit_records[0].atlas_entry=="frames.json"&&graphics_summary.audit_records[0].atlas_frames==1);
            const auto graphics_root=R"({"schema":"spellcross-dataset","version":1,"partial":false,"runtime_ready":false,"resource_catalog":"resources.json","packages":[{"file":"units.pak","category":"units","records":1,"converted":1,"decoded":0,"raw":0,"failed":0,"containers":0}],"coverage_by_format":{"units/FSU-SPRITE":{"converted":1}},"atlas_metrics":{"frames":1,"pages":1,"source_pixels":9,"png_bytes":24}})";
            const auto graphic_catalog=resource_catalog(graphics_root,{{"units.pak",graphics_summary}});
            require(graphic_catalog.find("\"by_group\":{\"A\":[0]}")!=std::string::npos);
            require(graphic_catalog.find("\"atlas\":{\"entry\":\"frames.json\",\"frames\":1}")!=std::string::npos);
            validate_resource_catalog(graphic_catalog,graphics_root,{{"units.pak",graphics_summary}});
        }
        {
            // Small retail table fixtures: preserve exact bit positions and values.
            sc::Asset table_asset;table_asset.member=true;table_asset.name="COMMON.FS::LEVEL_02.SSD";table_asset.format="SSD";
            B adjacency(128);le(adjacency,4,0x8000001cu);
            auto adjacency_table=sc::retail_table(table_asset,adjacency);
            require(adjacency_table&&adjacency_table->kind=="territory-adjacency"&&adjacency_table->json.find("[2,3,4,31]")!=std::string::npos);
            table_asset.name="COMMON.FS::GAMMA.TBL";table_asset.format="TBL";
            B gamma(704);for(std::size_t k=0;k<gamma.size();++k)gamma[k]=k%64;
            require(sc::retail_table(table_asset,gamma)->kind=="gamma-table");
            table_asset.name="COMMON.FS::_BUY.PAL";table_asset.format="PAL";
            require(sc::retail_table(table_asset,B{13,10})->kind=="empty-palette");
            require(!sc::retail_table(table_asset,B{1,2}));
            table_asset.name="T11.FS::MAPCOLOR.COL";table_asset.format="COL";
            require(sc::retail_table(table_asset,B(11))->kind=="terrain-color-indices");
            table_asset.name="INFO.FS::PALETA.DRK";table_asset.format="DRK";
            require(sc::retail_table(table_asset,B(256))->kind=="info-index-filter");
            auto info_filter=sc::retail_table(table_asset,B(256));
            Entries retail_entries;
            retail_entries["original.bin"].size=256;
            retail_entries["records/1/retail-table.json"].json=info_filter->json;
            retail_entries["records/1/retail-table.json"].size=info_filter->json.size();
            retail_entries["manifest.json"].json=R"({"schema":"spellcross-pak","version":1,"category":"info","partial":false,"records":[{"id":"a#1","name":"INFO.FS::PALETA.DRK","format":"DRK","status":"converted","original":{"entry":"original.bin","offset":0,"size":256},"payload":"records/1/retail-table.json"}],"graphics":[],"palettes":[]})";
            require(validate_package(retail_entries,"info").audit_records.at(0).kind=="info-index-filter");
            auto bad_filter=info_filter->json;auto role_pos=bad_filter.find("\"dark\"");bad_filter.replace(role_pos,6,"\"light\"");
            retail_entries["records/1/retail-table.json"].json=bad_filter;
            bool bad_role=false;try{validate_package(retail_entries,"info");}catch(const std::exception&){bad_role=true;}require(bad_role);
            sc::Asset dos;dos.name="DATA/INSTALL.DTA::DRIVERS/ADLIB.MDI";dos.format="MDI";dos.member=true;
            require(sc::legacy_support_asset(dos));
            dos.name="DATA/INSTALL.FS::MSG0000";dos.format="UNKNOWN";require(sc::legacy_support_asset(dos));
            dos.name="COMMON.FS::MSG0000";require(!sc::legacy_support_asset(dos));
            dos.name="MOVIE.FS::TEMP.DPK";dos.format="DPK";dos.size=525;require(sc::legacy_support_asset(dos));
            dos.size=526;require(!sc::legacy_support_asset(dos));
            dos.size=525;dos.name="MOVIE.FS::OTHER.DPK";require(!sc::legacy_support_asset(dos));
            dos.name="COMMON.FS::TEMP.DPK";require(!sc::legacy_support_asset(dos));
            dos.name="COMMON.FS::BOXER.LST";dos.format="LST";dos.size=225;require(sc::legacy_support_asset(dos));
            dos.size=224;require(!sc::legacy_support_asset(dos));
            Entries legacy_entries;
            legacy_entries["original.bin"].size=42;
            legacy_entries["manifest.json"].json=R"({"schema":"spellcross-pak","version":1,"category":"other","partial":false,"records":[{"id":"a#1","name":"DATA/INSTALL.FS::MSG0000","format":"UNKNOWN","status":"raw","role":"legacy-only","original":{"entry":"original.bin","offset":0,"size":42}}],"graphics":[],"palettes":[]})";
            require(validate_package(legacy_entries,"other").audit_records.at(0).assessment=="legacy-only");
            legacy_entries["original.bin"].size=525;
            legacy_entries["manifest.json"].json=R"({"schema":"spellcross-pak","version":1,"category":"movies","partial":false,"records":[{"id":"temp#1","name":"DATA/MOVIE.FS::TEMP.DPK","format":"DPK","status":"raw","role":"legacy-only","original":{"entry":"original.bin","offset":0,"size":525}}],"graphics":[],"palettes":[]})";
            require(validate_package(legacy_entries,"movies").audit_records.at(0).assessment=="legacy-only");
            legacy_entries["original.bin"].size=526;
            auto& manifest=legacy_entries["manifest.json"].json;
            manifest.replace(manifest.find("\"size\":525"),10,"\"size\":526");
            bool different_temp=false;try{validate_package(legacy_entries,"movies");}catch(const std::exception&){different_temp=true;}require(different_temp);
        }
        // Failed retail terrain remains failed, but has a renderable sentinel.
        sc::Asset damaged;damaged.name="DATA/DEVAST.FS::DMA0_270.DTA";damaged.format="DTA";damaged.member=true;
        B damaged_bytes(2248);damaged_bytes[186]=42;
        auto sentinel=sc::damaged_terrain_placeholder(damaged,damaged_bytes);
        require(sentinel&&sentinel->width==80&&sentinel->height==1&&sentinel->opaque==B(80));
        bool original_bad=false;try{sc::decode_image(damaged,damaged_bytes);}catch(const std::exception&){original_bad=true;}require(original_bad);
        damaged_bytes[0]=1;require(!sc::damaged_terrain_placeholder(damaged,damaged_bytes));
        damaged_bytes[0]=0;damaged.name="DEVAST.FS::OTHER.DTA";require(!sc::damaged_terrain_placeholder(damaged,damaged_bytes));
        Entries placeholder_entries;
        placeholder_entries["original.bin"].size=2248;
        placeholder_entries["palette.rgb"].size=768;placeholder_entries["palette.known"].size=256;
        placeholder_entries["i.png"]={12,80,1,{}};placeholder_entries["m.png"]={12,80,1,{}};
        placeholder_entries["manifest.json"].json=R"({"schema":"spellcross-pak","version":1,"category":"terrain","partial":false,"records":[{"id":"bad#1","name":"DEVAST.FS::DMA0_270.DTA","format":"DTA","status":"failed","original":{"entry":"original.bin","offset":0,"size":2248},"payload":"frames.json"}],"graphics":["frames.json"],"palettes":[{"rgb":"palette.rgb","known":"palette.known"}]})";
        placeholder_entries["frames.json"].json=R"({"schema":"spellcross-atlas","version":1,"group":"terrain","pages":[{"indices":"i.png","opacity":"m.png"}],"frames":[{"id":"bad#1","frame":0,"page":0,"rect":[0,0,80,1],"origin":[0,0],"palette":0,"synthetic_placeholder":true}]})";
        auto placeholder_summary=validate_package(placeholder_entries,"terrain");
        require(placeholder_summary.counts.at("failed")==1&&placeholder_summary.audit_records.at(0).kind=="synthetic-placeholder"&&placeholder_summary.audit_records.at(0).assessment=="placeholder");
        auto& placeholder_meta=placeholder_entries["frames.json"].json;
        auto marker_pos=placeholder_meta.find(",\"synthetic_placeholder\":true");
        placeholder_meta.erase(marker_pos,std::string(",\"synthetic_placeholder\":true").size());
        bool unmarked=false;try{validate_package(placeholder_entries,"terrain");}catch(const std::exception&){unmarked=true;}require(unmarked);
        auto& atlas_json=entries["frames.json"].json;
        atlas_json.pop_back();atlas_json+=R"(,"fsu_layout":{"schema":"spellcross-fsu-layout","version":1,"static_azimuths":1,"static_slopes":1,"animation_azimuths":0,"animation_slopes":0,"frames_per_sequence":0,"static_by_slope":[["a#1"]],"animation_by_azimuth_or_slope":[]}})";
        validate_package(entries,"units");
        auto good_layout=atlas_json;auto bad_id=atlas_json.rfind("a#1");atlas_json.replace(bad_id,3,"x#1");
        bool bad_layout=false;try{validate_package(entries,"units");}catch(const std::exception&){bad_layout=true;}require(bad_layout);atlas_json=good_layout;
        entries["m.png"].png_width=3;bool bad_atlas=false;try{validate_package(entries,"units");}catch(const std::exception&){bad_atlas=true;}require(bad_atlas);
        bool duplicate=false;try{dataset_packages(R"({"schema":"spellcross-dataset","version":1,"version":1})");}catch(const std::exception&){duplicate=true;}require(duplicate);
        auto layout=sc::fsu_layout("ABRMS",{"A00","A01","B00","B01"});require(layout.static_slopes==2&&layout.static_azimuths==2&&layout.stat[1][1]==3);
        layout=sc::fsu_layout("TEST",{"A00","B00","A10","B10","F00","F01"});require(layout.frames==2&&layout.animation_azimuths==2&&layout.anim[1][1]==3&&layout.stat[0][0]==4);
        sc::Asset ui;ui.name="COMMON.FS::MAINMENU.LZ";ui.format="LZ";ui.member=true;
        auto menu=sc::decode_image(ui,B(640*480));require(menu&&menu->width==640&&menu->height==480&&menu->opaque[0]==255);
        ui.name="COMMON.FS::LEVEL_02.CLK";ui.format="CLK";auto clk=sc::decode_image(ui,B{1,0,3,0,6,0,2,7,1,0});require(clk&&clk->indices==B({7,7,0}));
        ui.name="COMMON.FS::DISPLAY.PL";ui.format="PL";auto lines=sc::language_table(ui,B{';','x','\r','\n','A','\r','B',26});require(lines&&lines->size()==2&&sc::language_for(ui,"cs")=="pl");
        require(sc::narrative_display(B{'A','~','\r','\n','B','.',26})==B({'A',' ','B','.'}));
        sc::Asset select_mask;select_mask.name="COMMON.FS::SELECT_A.LZ0";select_mask.format="LZ0";select_mask.member=true;
        auto selection=sc::decode_image(select_mask,sprite);require(selection&&selection->indices==decoded.indices&&selection->opaque==decoded.opaque&&!selection->shadow_index);
        sc::Asset overlay;overlay.name="COMMON.FS::PLK_ACT.LZ0";overlay.format="LZ0";overlay.member=true;
        auto outline=sc::decode_image(overlay,B(9247));require(outline&&outline->width==138&&outline->height==67&&outline->trailing_bytes==1);
        overlay.name="COMMON.FS::RTA_SEL.LZ0";outline=sc::decode_image(overlay,B(2113));require(outline&&outline->width==132&&outline->height==16);
        overlay.name="COMMON.FS::LISTA_0B.LZ0";outline=sc::decode_image(overlay,B(10881));require(outline&&outline->width==160&&outline->height==68);
        auto pnm_image=sc::layer_image(anim,{sc::layer_children(anim,{}).at(0)},icon_data);
        require(pnm_image&&pnm_image->opaque[0]==0&&!pnm_image->opaque_zero_uses_254);
        // Two frames at distinct relative offsets, then an alias to the first.
        B sequence_fixture(13+2*icon_data.size());sequence_fixture[0]=3;le(sequence_fixture,1,12);le(sequence_fixture,5,12+icon_data.size());le(sequence_fixture,9,12);
        std::copy(icon_data.begin(),icon_data.end(),sequence_fixture.begin()+13);
        std::copy(icon_data.begin(),icon_data.end(),sequence_fixture.begin()+13+icon_data.size());
        write(t.p/"SEQUENCE.PNM",sequence_fixture);
        sc::Asset sequence_asset{t.p/"SEQUENCE.PNM","SEQUENCE.PNM","PNM",0,sequence_fixture.size(),false};
        auto steps=sc::layer_children(sequence_asset,{});require(steps.size()==3&&steps[0].offset==13&&steps[1].offset==13+icon_data.size()&&steps[2].offset==13);
        auto direct_steps=sc::animation_frames(sequence_asset,sequence_fixture);
        require(direct_steps.size()==steps.size());
        for(std::size_t k=0;k<steps.size();++k)require(direct_steps[k].offset==steps[k].offset&&direct_steps[k].size==steps[k].size);
        {
            sc::PreviewSession session;
            require(sc::preview(sequence_asset,sequence_fixture.size())==sequence_fixture);
            require(sc::preview(sequence_asset,4)==B(sequence_fixture.begin(),sequence_fixture.begin()+4));
        }
        require(sc::layer_bytes(sequence_asset,{steps[2]})==icon_data);
        sc::Inventory palette_fixture;
        auto fixture_bank=[&](const std::string& archive,const std::string& name,unsigned count,unsigned value){
            auto file=t.p/(archive+"-"+name);B bytes(count*3,static_cast<std::uint8_t>(value));write(file,bytes);
            palette_fixture.assets.push_back({file,archive+"::"+name,"PAL",0,bytes.size(),true});
        };
        fixture_bank("COMMON.FS","STRATEGY.PAL",256,10);
        fixture_bank("COMMON.FS","_SHARED1.PAL",128,20);
        fixture_bank("COMMON.FS","BIG_MAP.PAL",64,30);
        fixture_bank("COMMON.FS","_HIERAR.PAL",64,40);
        fixture_bank("COMMON.FS","_UNITS.PAL",64,50);
        fixture_bank("COMMON.FS","_OPTIONS.PAL",64,60);
        fixture_bank("INFO.FS","PALETA.NRM",256,70);
        fixture_bank("COMMON.FS","MAINMENU.PAL",256,80);
        sc::Palette tactical{};tactical.rgb.fill(90);tactical.known.fill(true);
        auto extra=palette_fixture.assets.front();extra.format="FSU-SPRITE";extra.name="UNITS.FSU::FAKE/A00";extra.size=123;
        palette_fixture.assets.push_back(extra);
        auto indexed_palette_fixture=sc::palette_inventory(palette_fixture);
        require(indexed_palette_fixture.assets.size()+1==palette_fixture.assets.size());
        sc::Asset context_asset;context_asset.member=true;context_asset.format="BTN";
        for(auto n:{"BIGMB__N.BTN","BIGM2__N.BTN","BMPAN__N.BTN","VMMIN__N.BTN"}){
            context_asset.name="COMMON.FS::"+std::string(n);
            auto actual=sc::asset_palette(palette_fixture,context_asset,tactical);
            auto indexed=sc::asset_palette(indexed_palette_fixture,context_asset,tactical);
            require(actual.rgb==indexed.rgb&&actual.known==indexed.known);
            require(actual.rgb[0]==20&&actual.rgb[128*3]==10&&actual.rgb[192*3]==30);
        }
        context_asset.name="COMMON.FS::IINFO__N.BTN";require(sc::asset_palette(palette_fixture,context_asset,tactical).rgb[0]==70);
        context_asset.name="COMMON.FS::IEXIT__N.BTN";require(sc::asset_palette(palette_fixture,context_asset,tactical).rgb[0]==70);
        context_asset.name="COMMON.FS::MAINB__N.BTN";require(sc::asset_palette(palette_fixture,context_asset,tactical).rgb[0]==90);
        context_asset.format="LZ";
        for(auto [name,mid]:std::vector<std::pair<std::string,int>>{{"HIERARCH.LZ",40},{"UNITS.LZ",50},{"VMU_SLCT.LZ",50},{"OPTIONS.LZ",60}}){
            context_asset.name="COMMON.FS::"+name;require(sc::asset_palette(palette_fixture,context_asset,tactical).rgb[128*3]==mid);
        }
        context_asset.name="COMMON.FS::BM_LSTA.LZ";auto bm=sc::decode_image(context_asset,B(5785,196));require(bm&&bm->width==65&&bm->height==89&&bm->opaque[0]==255);
        context_asset.name="COMMON.FS::T01.LZ";auto tut_button=sc::decode_image(context_asset,B(6200));require(tut_button&&tut_button->width==200&&tut_button->height==31&&sc::asset_palette(palette_fixture,context_asset,tactical).rgb[0]==80);
        // Remaining retail UI assets: dimensions reconstructed from EN pixel arrays.
        sc::Asset retail;retail.member=true;retail.format="LZ";
        for(auto [name,w,h]:std::vector<std::tuple<std::string,int,int>>{
            {"INFO.FS::IBG_600.LZ",800,600},{"INFO.FS::NAMELSTA.LZ",200,80},
            {"COMMON.FS::DIFFIC.LZ",249,152},{"COMMON.FS::GOTOLSTA.LZ",176,91},
            {"COMMON.FS::LED_OFF.LZ",9,8},{"COMMON.FS::LED_ON.LZ",9,8}}){
            retail.name=name;auto image=sc::decode_image(retail,B(w*h,7));
            require(image&&image->width==w&&image->height==h);
        }
        retail.name="INFO.FS::PATTERN.DTA";retail.format="DTA";
        auto pattern=sc::decode_image(retail,B(1024,0));require(pattern&&pattern->width==32&&pattern->height==32&&pattern->opaque[0]==255);
        retail.name="DATA/INSTALL.DTA::DATA/COMMON.FS::C02.LZ";retail.format="LZ";
        require(sc::is_text_asset(retail,B{'I','N','T','R','O','.','S','M','K'}));
        require(!sc::is_text_asset(retail,B{'I',0,'N'}));
        retail.name="MUSIC.FS::C02.LZ";require(!sc::is_text_asset(retail,B{'I','N','T','R','O'}));
        // Single-entry Windows ICO with 2x2 bottom-up 8-bit DIB, BGRA
        // palette, 4-byte aligned XOR rows and separate 1-bit AND mask.
        B win(22+40+8+8+8);
        win[2]=1;win[4]=1;win[6]=2;win[7]=2;win[8]=2;
        le(win,14,64);le(win,18,22);
        le(win,22,40);le(win,26,2);le(win,30,4);win[34]=1;win[36]=8;le(win,54,2);
        win[62]=10;win[63]=20;win[64]=30;
        win[66]=40;win[67]=50;win[68]=60;
        win[70]=1;win[71]=0;win[74]=0;win[75]=1;
        win[82]=0x40; // top right pixel is transparent
        write(t.p/"SPELCROS.ICO",win);
        sc::Asset win_asset{t.p/"SPELCROS.ICO","DATA/INSTALL.DTA::SPELCROS.ICO","ICO",0,win.size(),true};
        auto win_image=sc::decode_image(win_asset,win);
        require(win_image&&win_image->width==2&&win_image->height==2&&win_image->indices==B({0,1,1,0})&&
                win_image->opaque==B({255,0,255,255})&&!win_image->opaque_zero_uses_254);
        auto win_palette=sc::asset_palette({},win_asset,tactical);
        require(win_palette.known[0]&&win_palette.known[1]&&!win_palette.known[2]&&win_palette.rgb[0]==30&&win_palette.rgb[3]==60);
        win.pop_back();bool truncated_icon=false;
        try{sc::decode_image(win_asset,win);}catch(const std::exception&){truncated_icon=true;}
        require(truncated_icon);
        // Shared geometry across archives and edition-independent frame names.
        for(auto archive:{"COMMON.FS","INSTALL.FS"})for(auto variant:{"RAM1","RAM2"}){
            sc::Asset frame_asset;frame_asset.member=true;frame_asset.format="DTA";
            for(auto [suffix,w,h]:std::vector<std::tuple<std::string,int,int>>{
                    {"HORZ.DTA",76,10},{"VERT.DTA",10,69},{"ROH.DTA",10,10}}){
                frame_asset.name=std::string(archive)+"::"+variant+suffix;
                auto image=sc::decode_image(frame_asset,B(w*h,0));
                require(image&&image->width==w&&image->height==h&&image->opaque[0]==0);
                require(!sc::decode_image(frame_asset,B(w*h+3)));
            }
        }
        sc::Asset pattern_asset;pattern_asset.member=true;pattern_asset.format="DTA";
        pattern_asset.name="COMMON.FS::RAM1PAT.DTA";
        // Exact decoded lengths from the Czech ISO evidence bundle.
        for(auto archive:{"COMMON.FS","INSTALL.FS"}){
            pattern_asset.name=std::string(archive)+"::RAM1PAT.DTA";
            auto pattern_img=sc::decode_image(pattern_asset,B(6715,106));
            require(pattern_img&&pattern_img->width==85&&pattern_img->height==79&&pattern_img->trailing_bytes==0);
            require(!sc::decode_image(pattern_asset,B(6714)));
        }
        // Verify both previously supported English and newly observed Czech
        // layouts, and preserve the one trailing retail padding byte.
        for(auto [name,w,h,pad]:std::vector<std::tuple<std::string,int,int,int>>{
            {"DATA/INFO.FS::INFOLSTA.LZ",433,63,0},
            {"DATA/INFO.FS::INFOLSTA.LZ",433,64,0},
            {"DATA/INSTALL.DTA::DATA/COMMON.FS::DIFFIC.LZ",249,152,0},
            {"DATA/INSTALL.DTA::DATA/COMMON.FS::DIFFIC.LZ",249,180,0},
            {"DATA/INSTALL.DTA::DATA/COMMON.FS::IL_CISLA.LZ",40,7,1},
            {"DATA/INSTALL.DTA::DATA/COMMON.FS::JRC_LOGO.LZ",640,480,0},
            {"DATA/INSTALL.DTA::DATA/COMMON.FS::RES_BAR.LZ",260,16,0},
            {"DATA/INSTALL.FS::BAR.LZ",320,38,0},
            {"DATA/INSTALL.FS::BTN_DOWN.LZ",20,20,0},
            {"DATA/INSTALL.FS::BTN_UP.LZ",20,20,0}}){
            sc::Asset candidate;candidate.member=true;candidate.format="LZ";candidate.name=name;
            B payload(w*h+pad,7);payload[0]=0;
            auto actual=sc::decode_image(candidate,payload);
            require(actual&&actual->width==w&&actual->height==h&&actual->trailing_bytes==std::size_t(pad));
            require(actual->indices.size()==std::size_t(w*h)&&actual->indices[0]==0&&actual->indices.back()==7);
        }
        // Installer resources must not inherit the unrelated tactical bank.
        sc::Asset installer_bar;installer_bar.member=true;installer_bar.name="DATA/INSTALL.FS::BAR.LZ";installer_bar.format="LZ";
        auto en_bar=sc::decode_image(installer_bar,B(342*32,7));
        require(en_bar&&en_bar->width==342&&en_bar->height==32);
        sc::Asset radar;radar.name="COMMON.FS::RAD49.LZ";radar.member=true;radar.format="LZ";
        auto radar_image=sc::decode_image(radar,B(78*76,0));
        require(radar_image&&radar_image->width==78&&radar_image->height==76&&radar_image->opaque[0]==255);
        require(!sc::decode_image(radar,B(78*76+1,0)));
        sc::Asset installer_mask=installer_bar;installer_mask.name="DATA/INSTALL.FS::MASK.LZ";
        auto mask_image=sc::decode_image(installer_mask,B(604*272,255));
        require(mask_image&&mask_image->indices.size()==604*272&&mask_image->indices[0]==255);
        require(!sc::decode_image(installer_mask,B(604*272,1)));
        sc::Asset empty_text;empty_text.name="TEXTS.FS::T07_01B.BAD";empty_text.member=true;empty_text.format="BAD";
        require(sc::is_text_asset(empty_text,B{}));
        sc::Asset cheat;cheat.name="COMMON.FS::CHEATS.LZ";cheat.member=true;cheat.format="LZ";
        require(sc::is_text_asset(cheat,B{'a','\r','\n','b'}));
        // PCX with two pixels, padded row and an escaped high index.
        sc::Asset pcx;pcx.name="TEST.PCX";pcx.format="PCX";
        B pcx_data(128);pcx_data[0]=10;pcx_data[1]=5;pcx_data[2]=1;pcx_data[3]=8;
        pcx_data[8]=1;pcx_data[65]=1;pcx_data[66]=4;
        pcx_data.insert(pcx_data.end(),{0,0xc1,255,0xc2,0,12});pcx_data.resize(pcx_data.size()+768);
        auto pcx_image=sc::decode_image(pcx,pcx_data);
        require(pcx_image&&pcx_image->width==2&&pcx_image->height==1&&pcx_image->indices==B({0,255})&&pcx_image->opaque==B({255,255}));
        pcx_data[131]=0xc3;bool bad_pcx=false;
        try{sc::decode_image(pcx,pcx_data);}catch(const std::exception&){bad_pcx=true;}require(bad_pcx);
        sc::Asset qh;qh.format="QH";qh.name="COMMON.FS::BATTLES.QH";qh.member=true;
        const std::string qh_text="; comment\r\n1,403,61,50\r\nPicture of unit\r\n2,453,57,8\r\nExperience points left\r\nto next level";
        auto help=sc::quick_help(qh,B(qh_text.begin(),qh_text.end()));
        require(help&&help->find("\"region_count\":2")!=std::string::npos&&help->find("Experience points left")!=std::string::npos&&help->find("to next level")!=std::string::npos);
        const std::string bad_qh="639,0,2,1\nOutside";
        bool bad_region=false;try{sc::quick_help(qh,B(bad_qh.begin(),bad_qh.end()));}catch(const std::exception&){bad_region=true;}require(bad_region);
        auto missing_installer_palette=sc::asset_palette({},installer_bar,tactical);
        require(!missing_installer_palette.known[7]);
        sc::Asset logo;logo.member=true;logo.name="COMMON.FS::JRC_LOGO.LZ";logo.format="LZ";
        fixture_bank("COMMON.FS","JRC_LOGO.PAL",256,73);
        require(sc::asset_palette(palette_fixture,logo,tactical).rgb[0]==73);
        auto evidence=sc::graphics_evidence(B(281,0));
        require(evidence.find("decoded_bytes=281")!=std::string::npos&&
                evidence.find("40x7+1")!=std::string::npos&&
                evidence.find("not verified")!=std::string::npos);
        bool zero_height_rejected=false;
        try{sc::decode_image(terrain,B(2248,0));}catch(const std::exception&){zero_height_rejected=true;}
        require(zero_height_rejected);
        std::cout<<"Inventory, graphics, video, atlas, semantic, campaign, music and presentation tests passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
