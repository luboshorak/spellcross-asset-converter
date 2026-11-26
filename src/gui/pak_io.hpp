#pragma once
// Shared by the actual exporter read-back and the cross-platform ZIP tests.
#include "spellcross/validation.hpp"
#include "spellcross/graphics.hpp"
#include <wx/wfstream.h>
#include <wx/zipstrm.h>
#include <wx/mstream.h>
#include <wx/image.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <stdexcept>
namespace sc::pak_io {
inline sc::validation::Entries read_pak(const std::filesystem::path& filename){
    using namespace sc::validation;
    if(!std::filesystem::is_regular_file(filename)||std::filesystem::is_symlink(filename))
        throw std::runtime_error("Missing or nonregular PAK file");
    wxFFileInputStream file(wxString(filename.wstring()));
    if(!file.IsOk())throw std::runtime_error("Cannot open PAK");
    wxZipInputStream zip(file);
    Entries result;
    std::uint64_t total=0,json_total=0;
    std::array<unsigned char,65536> buffer{};
    while(auto item=std::unique_ptr<wxZipEntry>(zip.GetNextEntry())){
        if(result.size()>=300000)throw std::runtime_error("PAK entry count limit");
        // On UTF-8 wxWidgets builds the conversion buffer may borrow the
        // wxString's storage. Keep the name alive until its bytes are copied.
        const wxString zip_name=item->GetName(wxPATH_UNIX);
        auto utf=zip_name.utf8_str();
        std::string name(utf.data(),utf.length());
        if(item->IsDir()||!safe_entry(name)||result.contains(name))
            throw std::runtime_error("Unsafe or duplicate PAK entry: "+name);
        const bool json=name.ends_with(".json");
        const bool png=name.ends_with(".png")&&name.starts_with("graphics/");
        const std::uint64_t cap=json||png?64ull*1024*1024:8ull*1024*1024*1024;
        sc::Bytes image;
        Entry e;std::uint32_t crc=0xffffffffu;
        for(;;){
            zip.Read(buffer.data(),buffer.size());auto n=zip.LastRead();
            if(n>cap-e.size||n>32ull*1024*1024*1024-total)
                throw std::runtime_error("PAK read limit exceeded: "+name);
            crc=crc32_update(crc,buffer.data(),n);
            e.size+=n;total+=n;
            if(json){
                if(n>256ull*1024*1024-json_total)throw std::runtime_error("PAK metadata limit exceeded");
                json_total+=n;e.json.append(reinterpret_cast<const char*>(buffer.data()),n);
            }else if(png)image.insert(image.end(),buffer.begin(),buffer.begin()+n);
            if(zip.GetLastError()==wxSTREAM_EOF)break;
            if(zip.GetLastError()!=wxSTREAM_NO_ERROR||!n)
                throw std::runtime_error("ZIP read error: "+name);
        }
        if(item->GetSize()<0||static_cast<std::uint64_t>(item->GetSize())!=e.size||
            item->GetCrc()!=(crc^0xffffffffu))
            throw std::runtime_error("ZIP length or CRC mismatch: "+name);
        if(png){
            // Validate dimensions before wxImage allocates decoded pixels.
            const unsigned char signature[]={137,80,78,71,13,10,26,10};
            if(image.size()<33||!std::equal(std::begin(signature),std::end(signature),image.begin())||
               !std::equal(image.begin()+12,image.begin()+16,"IHDR"))
                throw std::runtime_error("Invalid atlas PNG header: "+name);
            auto be=[&](int p){return std::uint32_t(image[p])<<24|std::uint32_t(image[p+1])<<16|std::uint32_t(image[p+2])<<8|image[p+3];};
            auto width=be(16),height=be(20);
            if(!width||!height||width>2048||height>2048||image[24]!=8)
                throw std::runtime_error("Invalid atlas PNG dimensions or bit depth: "+name);
            wxMemoryInputStream input(image.data(),image.size());wxImage decoded;
            if(!decoded.LoadFile(input,wxBITMAP_TYPE_PNG)||decoded.GetWidth()!=int(width)||decoded.GetHeight()!=int(height)||decoded.HasAlpha())
                throw std::runtime_error("Invalid atlas data plane: "+name);
            auto rgb=decoded.GetData();
            for(std::size_t p=0;p<std::size_t(width)*height;++p)
                if(rgb[p*3]!=rgb[p*3+1]||rgb[p*3]!=rgb[p*3+2])
                    throw std::runtime_error("Non-grayscale atlas plane: "+name);
            e.png_width=width;e.png_height=height;
        }
        result.emplace(std::move(name),std::move(e));
    }
    if(zip.GetLastError()!=wxSTREAM_EOF)throw std::runtime_error("Invalid ZIP end");
    return result;
}

}
