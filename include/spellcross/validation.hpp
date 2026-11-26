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
    // Bounded tactical tile arrays retained for read-back index checks.
    std::vector<std::uint8_t> tile_bytes;
};
using Entries=std::map<std::string,Entry>;
// Physical entries are counted once; logical original ranges can overlap.
struct AuditRecord {
    std::string id,name,format,status,detail,payload,kind,assessment,notes;
    std::uint64_t source_bytes{},payload_bytes{};
    // Package-local locators, not new public resource IDs.
    std::string original_entry,fallback_entry,atlas_entry;
    std::uint64_t original_offset{},atlas_frames{};
};
struct AtlasGroup {std::string group,entry;std::uint64_t frames{};bool fsu_layout{};std::uint64_t static_slopes{},static_azimuths{},animation_rows{},sequence_frames{};};
// A reference points to an asset, atlas group, or numeric ID in a
// particular definition table. Source locations are stable record IDs
// plus a field/line selector; unknown syntax is reported separately.
struct GameReference {
    std::string source,field,kind,target,scope;
    std::uint64_t frame_limit{};
};
struct DefinitionSet {
    std::string source,kind;
    std::vector<std::string> ids;
};
struct GameCommand {std::uint64_t line{};std::string keyword;std::vector<std::string> args;};
struct GameBlock {
    std::string source,domain,keyword,key;
    std::uint64_t line{};
    std::vector<std::string> args;
    std::vector<GameCommand> commands;
};
struct FsuContextRecord {std::string source,group;std::vector<std::uint64_t> static_azimuths;std::uint64_t animation_origins{},origin_count{},zero_origins{},center_count{},zero_centers{};};
struct PackageSummary {
    std::string category;
    std::map<std::string,std::uint64_t> counts;
    std::map<std::string,std::uint64_t> formats;
    std::uint64_t records{},frames{},pages{},pixels{},png_bytes{};
    std::uint64_t original_bytes{},derived_bytes{},metadata_bytes{},unattributed_bytes{};
    std::uint64_t zip_entries{},archive_bytes{};
    std::vector<AuditRecord> audit_records;
    std::vector<AtlasGroup> atlas_groups;
    std::vector<GameReference> game_references;
    std::vector<DefinitionSet> definitions;
    std::vector<GameBlock> game_blocks;
    std::vector<FsuContextRecord> fsu_contexts;
    std::map<std::string,std::uint64_t> metadata_coverage;
    std::map<std::string,std::uint64_t> unreviewed_commands;
};
struct DatasetPackage {std::string file,category;};
bool safe_entry(std::string_view);
std::vector<DatasetPackage> dataset_packages(std::string_view);
PackageSummary validate_package(const Entries&,std::string_view expected_category);
void validate_dataset(std::string_view,const std::map<std::string,PackageSummary>&);
struct AuditReport {std::string summary,records_tsv,catalog_json,game_metadata_json,references_tsv;};
// Optional v1 extension: deterministic dataset-wide lookup index derived
// exclusively from validated package manifests.
bool has_resource_catalog(std::string_view root);
bool has_game_metadata(std::string_view root);
void validate_game_metadata(std::string_view stored,std::string_view expected);
std::string resource_catalog(std::string_view root,const std::map<std::string,PackageSummary>&);
void validate_resource_catalog(std::string_view catalog,std::string_view root,
                               const std::map<std::string,PackageSummary>&);
struct SourceRecord {std::string id,name,format,category;std::uint64_t size{};};
// SourceRecord id/name are original byte strings, matching pack::stable_id.
AuditReport audit_report(std::string_view root,const std::map<std::string,PackageSummary>&,
                         const std::vector<SourceRecord>* expected_source=nullptr);
std::uint32_t crc32_update(std::uint32_t state,const unsigned char* data,std::size_t n);
}
