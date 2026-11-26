#include "../src/gui/pak_io.hpp"
#include <chrono>
#include <iostream>

namespace fs=std::filesystem;
static void require(bool condition){if(!condition)throw std::runtime_error("PAK read-back test failed");}
static void write_entry(wxZipOutputStream& zip,const wxString& name,const std::string& value){
    require(zip.PutNextEntry(name));
    if(!value.empty()){
        zip.Write(value.data(),value.size());require(zip.LastWrite()==value.size());
    }
    require(zip.CloseEntry());
}
int main(){
    const auto temp=fs::temp_directory_path()/("sc-pak-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{
        require(fs::create_directory(temp));
        const auto archive=temp/"test.pak";
        {
            wxFFileOutputStream file(wxString(archive.wstring()));
            require(file.IsOk());wxZipOutputStream zip(file);zip.SetFormat(wxZIP_FORMAT_ZIP64);
            write_entry(zip,"original/containers/4190.bin","source");
            write_entry(zip,"records/1/text.txt","modern");
            write_entry(zip,"records/2/empty.bin","");
            write_entry(zip,"manifest.json",R"({"schema":"spellcross-pak","version":1,"category":"common","partial":false,"records":[{"id":"a#1","name":"a","format":"TXT","status":"converted","original":{"entry":"original/containers/4190.bin","offset":0,"size":6},"payload":"records/1/text.txt"}],"graphics":[],"palettes":[]})");
            require(zip.Close());require(file.Close());
        }
        auto entries=sc::pak_io::read_pak(archive);
        require(entries.size()==4&&entries.at("original/containers/4190.bin").size==6);
        require(entries.at("records/1/text.txt").size==6&&entries.at("records/2/empty.bin").size==0);
        require(sc::validation::validate_package(entries,"common").records==1);
        const auto duplicate=temp/"duplicate.pak";
        {
            wxFFileOutputStream file(wxString(duplicate.wstring()));
            require(file.IsOk());wxZipOutputStream zip(file);
            write_entry(zip,"original/containers/1.bin","first");
            write_entry(zip,"original/containers/1.bin","second");
            require(zip.Close());require(file.Close());
        }
        bool rejected=false;try{sc::pak_io::read_pak(duplicate);}catch(const std::exception&){rejected=true;}
        require(rejected);
        fs::remove_all(temp);std::cout<<"PAK ZIP64 cross-platform read-back passed\n";return 0;
    }catch(const std::exception& e){std::error_code ec;fs::remove_all(temp,ec);std::cerr<<e.what()<<'\n';return 1;}
}
