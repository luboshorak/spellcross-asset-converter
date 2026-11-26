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
        B anm(5+tile.size());anm[0]=1;le(anm,1,5);std::copy(tile.begin(),tile.end(),anm.begin()+5);write(t.p/"TILE.ANM",anm);
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
        require(safe_entry("graphics/1/page.png")&&!safe_entry("../bad")&&!safe_entry("C:/bad"));
        const unsigned char crc_test[]="123456789";require((crc32_update(0xffffffffu,crc_test,9)^0xffffffffu)==0xcbf43926u);
        Entries entries;
        entries["original.bin"].size=4;
        entries["text.txt"].size=2;
        entries["manifest.json"].json=R"({"schema":"spellcross-pak","version":1,"category":"common","partial":false,"records":[{"id":"a#1","name":"a","format":"TXT","status":"converted","original":{"entry":"original.bin","offset":1,"size":2},"payload":"text.txt"}],"graphics":[],"palettes":[]})";
        auto summary=validate_package(entries,"common");require(summary.records==1&&summary.counts.at("converted")==1);
        auto dataset=R"({"schema":"spellcross-dataset","version":1,"partial":false,"runtime_ready":false,"packages":[{"file":"common.pak","category":"common","records":1,"converted":1,"decoded":0,"raw":0,"failed":0,"containers":0}],"coverage_by_format":{"common/TXT":{"converted":1}},"atlas_metrics":{"frames":0,"pages":0,"source_pixels":0,"png_bytes":0}})";
        validate_dataset(dataset,{{"common.pak",summary}});
        entries["original.bin"].size=2;bool bad_range=false;try{validate_package(entries,"common");}catch(const std::exception&){bad_range=true;}require(bad_range);
        entries["original.bin"].size=4;
        entries["palette.rgb"].size=768;entries["palette.known"].size=256;
        entries["i.png"]={12,4,4,{}};entries["m.png"]={12,4,4,{}};
        entries["manifest.json"].json=R"({"schema":"spellcross-pak","version":1,"category":"units","partial":false,"records":[{"id":"a#1","name":"a","format":"FSU-SPRITE","status":"converted","original":{"entry":"original.bin","offset":0,"size":4},"payload":"frames.json"}],"graphics":["frames.json"],"palettes":[{"rgb":"palette.rgb","known":"palette.known"}]})";
        entries["frames.json"].json=R"({"schema":"spellcross-atlas","version":1,"group":"a","pages":[{"indices":"i.png","opacity":"m.png"}],"frames":[{"id":"a#1","frame":0,"page":0,"rect":[1,1,3,3],"origin":[-3,4],"palette":0,"shadow_index":253,"trailing_bytes":0}]})";
        auto graphics_summary=validate_package(entries,"units");require(graphics_summary.frames==1&&graphics_summary.pixels==9&&graphics_summary.png_bytes==24);
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
        std::cout<<"Inventory, graphics, video, atlas, semantic, campaign, music and presentation tests passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
