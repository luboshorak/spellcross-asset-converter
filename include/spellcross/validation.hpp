#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>
namespace sc::validation {
struct Entry {
    std::uint64_t size{};
    int png_width{},png_height{};
    // Only JSON metadata is retained; bulk payloads are streamed by caller.
    std::string json;
};
using Entries=std::map<std::string,Entry>;
struct PackageSummary {
    std::string category;
    std::map<std::string,std::uint64_t> counts;
    std::map<std::string,std::uint64_t> formats;
    std::uint64_t records{},frames{},pages{},pixels{},png_bytes{};
};
struct DatasetPackage {std::string file,category;};
bool safe_entry(std::string_view);
std::vector<DatasetPackage> dataset_packages(std::string_view);
PackageSummary validate_package(const Entries&,std::string_view expected_category);
void validate_dataset(std::string_view,const std::map<std::string,PackageSummary>&);
std::uint32_t crc32_update(std::uint32_t state,const unsigned char* data,std::size_t n);
}
