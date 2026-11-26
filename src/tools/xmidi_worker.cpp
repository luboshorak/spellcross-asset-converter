// Private isolated helper; not a user-facing CLI. Upstream LGPL converter
// runs in a separate process because its historical parser uses assertions.
#include "spellcross/music.hpp"
#include "cvt_xmi2mid.hpp"
#include <filesystem>
#include <fstream>
#include <memory>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#endif
int main(int argc,char** argv){
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
#endif
    try{
        if(argc!=3)return 2;
        auto input=std::filesystem::u8path(argv[1]),output=std::filesystem::u8path(argv[2]);
        auto size=std::filesystem::file_size(input);
        if(!size||size>16u*1024u*1024u)return 3;
        sc::Bytes data(static_cast<std::size_t>(size));
        std::ifstream in(input,std::ios::binary);
        if(!in.read(reinterpret_cast<char*>(data.data()),data.size()))return 3;
        sc::validate_xmidi(data);
        std::uint8_t* result=nullptr;std::uint32_t len=0;
        int rc=Convert_xmi2midi(data.data(),static_cast<std::uint32_t>(data.size()),&result,&len,0);
        std::unique_ptr<std::uint8_t,decltype(&std::free)> hold(result,&std::free);
        if(rc||!result||!len||len>64u*1024u*1024u)return 4;
        sc::validate_midi(std::span<const std::uint8_t>(result,len));
        std::ofstream out(output,std::ios::binary);
        out.write(reinterpret_cast<const char*>(result),len);out.close();return out?0:5;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 6;}
}
