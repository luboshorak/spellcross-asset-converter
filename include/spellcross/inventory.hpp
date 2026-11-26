#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace sc {
struct Asset {
    std::filesystem::path backing;
    std::string name; // logical path, with archive members separated by ::
    std::string format;
    std::uint64_t offset{}, size{};
    bool member{};
};
struct Inventory {
    std::vector<Asset> assets;
    std::vector<std::string> warnings;
    std::size_t fs_archives{}, fsu_archives{}, fsu_resources{}, fsu_sprites{};
    std::string source_kind;
};
std::string utf8(const std::filesystem::path&);
Inventory scan(const std::filesystem::path& source);
std::vector<std::uint8_t> preview(const Asset&, std::size_t limit = 4096);
// Exports exact stored bytes to a newly created child directory, never
// interprets archive filenames as filesystem paths or overwrites old exports.
std::filesystem::path extract(const std::vector<Asset>&, const std::filesystem::path& output);
std::string manifest(const Inventory&);
}
