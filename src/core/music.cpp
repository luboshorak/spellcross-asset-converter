#include "spellcross/music.hpp"
#include <algorithm>
#include <stdexcept>
#include <string_view>
namespace sc {
namespace {
using Span=std::span<const std::uint8_t>;
bool tag(Span d,std::size_t p,std::string_view s){return p<=d.size()&&s.size()<=d.size()-p&&std::equal(s.begin(),s.end(),d.begin()+p);}
void need(Span d,std::size_t p,std::size_t n){if(p>d.size()||n>d.size()-p)throw std::runtime_error("Truncated music data");}
unsigned be16(Span d,std::size_t p){need(d,p,2);return unsigned(d[p])*256+d[p+1];}
std::uint32_t be32(Span d,std::size_t p){need(d,p,4);return std::uint32_t(d[p])<<24|std::uint32_t(d[p+1])<<16|std::uint32_t(d[p+2])<<8|std::uint32_t(d[p+3]);}
unsigned vlq(Span d,std::size_t& p){
    unsigned v=0;for(int i=0;i<4;++i){need(d,p,1);auto b=d[p++];v=(v<<7)|(b&127);if(!(b&128))return v;}
    throw std::runtime_error("Invalid MIDI variable length");
}
void track(Span d){
    std::size_t p=0;unsigned running=0;bool end=false;
    while(p<d.size()){
        vlq(d,p);need(d,p,1);unsigned status=d[p];
        if(status&128){++p;if(status<240)running=status;else running=0;}
        else{if(!running)throw std::runtime_error("Missing MIDI running status");status=running;}
        if(status<240){
            unsigned type=status>>4,n=(type==12||type==13)?1:2;need(d,p,n);
            for(unsigned i=0;i<n;++i)if(d[p++]&128)throw std::runtime_error("Invalid MIDI data byte");
        }else if(status==255){
            need(d,p,1);auto type=d[p++];auto n=vlq(d,p);need(d,p,n);
            if(type==47){if(n||p!=d.size())throw std::runtime_error("Invalid MIDI end-of-track");end=true;break;}
            p+=n;
        }else if(status==240||status==247){auto n=vlq(d,p);need(d,p,n);p+=n;}
        else throw std::runtime_error("Unsupported MIDI system event");
    }
    if(!end)throw std::runtime_error("MIDI track has no end marker");
}
void iff(Span d,unsigned depth,unsigned& events){
    if(depth>8)throw std::runtime_error("XMIDI nesting exceeds limit");
    for(std::size_t p=0;p<d.size();){
        need(d,p,8);auto len=std::size_t(be32(d,p+4));need(d,p+8,len);
        auto body=d.subspan(p+8,len);
        if(tag(d,p,"FORM")||tag(d,p,"CAT ")||tag(d,p,"LIST")){
            need(body,0,4);iff(body.subspan(4),depth+1,events);
        }else if(tag(d,p,"EVNT")){
            if(body.empty()||++events>128)throw std::runtime_error("Invalid XMIDI event stream");
        }
        p+=8+len;
        if(len&1){need(d,p,1);++p;}
    }
}
}
MusicKind music_kind(Span d){
    if(tag(d,0,"MThd"))return MusicKind::midi;
    if(tag(d,0,"FORM")&&(tag(d,8,"XDIR")||tag(d,8,"XMID")))return MusicKind::xmidi;
    return MusicKind::none;
}
void validate_midi(Span d){
    if(music_kind(d)!=MusicKind::midi||be32(d,4)!=6)throw std::runtime_error("Invalid SMF header");
    auto type=be16(d,8),n=be16(d,10),division=be16(d,12);
    if(type>2||!n||n>128||(type==0&&n!=1)||!division)throw std::runtime_error("Invalid SMF track count or time division");
    std::size_t p=14;
    for(unsigned i=0;i<n;++i){
        if(!tag(d,p,"MTrk"))throw std::runtime_error("Missing SMF track");
        auto len=std::size_t(be32(d,p+4));need(d,p+8,len);track(d.subspan(p+8,len));p+=8+len;
    }
    if(p!=d.size())throw std::runtime_error("Trailing SMF bytes");
}
void validate_xmidi(Span d){
    if(music_kind(d)!=MusicKind::xmidi||d.size()>16u*1024u*1024u)throw std::runtime_error("Invalid XMIDI input");
    unsigned events=0;iff(d,0,events);if(!events)throw std::runtime_error("No XMIDI events");
}
}
