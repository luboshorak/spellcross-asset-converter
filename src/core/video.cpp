// DPK/DP2 delta layout adapted from spell_video.cpp, Stanislav Maslan (2022), MIT.
// CAN and Smacker use upstream Reloaded decoders with original notices.
#include "spellcross/video.hpp"
#include "can_video_decoder.h"
#include "smacker_video_decoder.h"
#include <algorithm>
#include <stdexcept>
#include <limits>
#include <cmath>
namespace sc {
namespace {
constexpr std::size_t max_pixels=512u*1024u*1024u,max_input=128u*1024u*1024u;
using Span=std::span<const std::uint8_t>;
void bounds(Span d,std::size_t p,std::size_t n){if(p>d.size()||n>d.size()-p)throw std::runtime_error("Truncated video chunk");}
unsigned u16(Span d,std::size_t p){bounds(d,p,2);return unsigned(d[p])|(unsigned(d[p+1])<<8);}
std::uint32_t u32(Span d,std::size_t p){bounds(d,p,4);return std::uint32_t(d[p])|std::uint32_t(d[p+1])<<8|std::uint32_t(d[p+2])<<16|std::uint32_t(d[p+3])<<24;}
void put16(Bytes& b,unsigned v){b.push_back(v&255);b.push_back((v>>8)&255);}
void put32(Bytes& b,std::uint32_t v){put16(b,v);put16(b,v>>16);}
void tag(Bytes& b,const char* s){b.insert(b.end(),s,s+4);}
std::string upper(std::string s){for(auto& c:s)if(c>='a'&&c<='z')c-='a'-'A';return s;}
Video delta_video(Span d,bool dp2){
    bounds(d,0,780);Video v;v.fps=15;
    auto w=u32(d,772),h=u32(d,776);
    if(!w||!h||w>4096||h>4096)throw std::runtime_error("Invalid video dimensions");
    v.width=w;v.height=h;std::size_t pixels=std::size_t(w)*h;
    Bytes frame(pixels);
    v.palettes.emplace_back();std::copy_n(d.begin()+4,768,v.palettes.back().begin());
    std::vector<std::int16_t> audio;
    std::size_t pos=780;bool first=true;
    while(pos<d.size()){
        if(v.frames.size()>=max_pixels/pixels||v.frames.size()>=100000)throw std::runtime_error("Video exceeds preview memory limit");
        if(!dp2){
            auto count=std::size_t(first?23:1)*735;bounds(d,pos,count*2);
            for(std::size_t i=0;i<count;++i)audio.push_back(static_cast<std::int16_t>(int(u16(d,pos+2*i))-32768));
            pos+=count*2;
        }
        first=false;
        auto n=u32(d,pos);pos+=4;bounds(d,pos,n);
        if(n){
            auto delta=decode_lz(d.subspan(pos,n));bounds(delta,0,8);
            std::size_t p=8,at=0;
            while(true){
                auto gap=u32(delta,p);p+=4;
                if(gap==0xffffffffu)break;
                auto count=u32(delta,p);p+=4;
                if(!count)continue;
                bounds(delta,p,count);
                if(gap>pixels-at)throw std::runtime_error("Video delta gap out of range");
                at+=gap;if(count>pixels-at)throw std::runtime_error("Video delta run out of range");
                std::copy_n(delta.begin()+p,count,frame.begin()+at);p+=count;at+=count;
            }
        }
        pos+=n;v.frames.push_back(frame);
    }
    if(v.frames.empty())throw std::runtime_error("Video has no frames");
    if(!dp2){v.wave=pcm_wave(audio,11025,1);v.audio_note="DPK PCM";}
    return v;
}
}
Bytes pcm_wave(std::span<const std::int16_t> audio,int rate,int channels){
    if(audio.empty())return {};
    if(rate<4000||rate>192000||(channels!=1&&channels!=2)||audio.size()%channels||
       audio.size()>(std::numeric_limits<std::uint32_t>::max()-36)/2)
        throw std::runtime_error("Invalid PCM audio parameters");
    Bytes out;auto n=static_cast<std::uint32_t>(audio.size()*2);out.reserve(44+n);
    tag(out,"RIFF");put32(out,36+n);tag(out,"WAVE");tag(out,"fmt ");put32(out,16);
    put16(out,1);put16(out,channels);put32(out,rate);put32(out,rate*channels*2);
    put16(out,channels*2);put16(out,16);tag(out,"data");put32(out,n);
    for(auto s:audio)put16(out,static_cast<std::uint16_t>(s));return out;
}
bool is_video(const Asset& a){return a.format=="CAN"||a.format=="DPK"||a.format=="DP2"||a.format=="SMK";}
Video decode_video(const Inventory& inv,const Asset& a){
    if(!is_video(a)||a.size>max_input)throw std::runtime_error("Unsupported video or compressed input exceeds 128 MiB");
    auto d=preview(a,static_cast<std::size_t>(a.size));Video v;
    if(a.format=="CAN"){
        if(u32(d,0)>max_pixels/64000)throw std::runtime_error("CAN exceeds preview memory limit");
        spellcross_can::Decoded decoded;
        if(!spellcross_can::Decode(d.data(),d.size(),decoded))throw std::runtime_error("Reloaded CAN decoder rejected stream");
        v.width=320;v.height=200;v.fps=15;v.frames=std::move(decoded.frames);v.palettes.push_back(decoded.palette);
        v.wave=pcm_wave(decoded.audio,11025,1);v.audio_note="CAN PCM";
    }else if(a.format=="SMK"){
        SmackerDecodedVideo decoded;std::string error;
        if(!DecodeSmackerVideo(d.data(),d.size(),decoded,error))throw std::runtime_error(error);
        v.width=decoded.width;v.height=decoded.height;v.fps=decoded.fps;
        v.frames=std::move(decoded.frames);v.palettes=std::move(decoded.palettes);
        v.wave=pcm_wave(decoded.audio_samples,decoded.audio_rate,decoded.audio_channels);v.audio_note="Smacker PCM";
    }else{
        v=delta_video(d,a.format=="DP2");
        if(a.format=="DP2"){
            auto stem=upper(a.name.substr(0,a.name.find_last_of('.')));
            v.audio_note="DP2 companion audio not found";
            for(auto& entry:inv.assets){
                if(entry.backing!=a.backing||entry.size>max_input)continue;
                auto n=upper(entry.name);if(n!=stem&&n!=stem+".WAV")continue;
                auto wave=preview(entry,static_cast<std::size_t>(entry.size));
                if(wave.size()>=12&&std::string(wave.begin(),wave.begin()+4)=="RIFF"&&std::string(wave.begin()+8,wave.begin()+12)=="WAVE"){
                    v.wave=std::move(wave);v.audio_note="DP2 companion WAVE";break;
                }
            }
        }
    }
    if(v.frames.empty()||v.palettes.empty()||!std::isfinite(v.fps)||v.fps<=0)throw std::runtime_error("Invalid decoded video");
    return v;
}
}
