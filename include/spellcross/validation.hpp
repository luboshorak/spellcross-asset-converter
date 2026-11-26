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
// Physical entries are counted once; logical original ranges can overlap.
struct AuditRecord {
    std::string id,name,format,status,detail,payload,kind,assessment,notes;
    std::uint64_t source_bytes{},payload_bytes{};
    // Package-local locators, not new public resource IDs.
    std::string original_entry,fallback_entry,atlas_entry;
    std::uint64_t original_offset{},atlas_frames{};
};
struct AtlasGroup {std::string group,entry;std::uint64_t frames{};bool fsu_layout{};};
struct PackageSummary {
    std::string category;
    std::map<std::string,std::uint64_t> counts;
    std::map<std::string,std::uint64_t> formats;
    std::uint64_t records{},frames{},pages{},pixels{},png_bytes{};
    std::uint64_t original_bytes{},derived_bytes{},metadata_bytes{},unattributed_bytes{};
    std::uint64_t zip_entries{},archive_bytes{};
    std::vector<AuditRecord> audit_records;
    std::vector<AtlasGroup> atlas_groups;
};
struct DatasetPackage {std::string file,category;};
bool safe_entry(std::string_view);
std::vector<DatasetPackage> dataset_packages(std::string_view);
PackageSummary validate_package(const Entries&,std::string_view expected_category);
void validate_dataset(std::string_view,const std::map<std::string,PackageSummary>&);
struct AuditReport {std::string summary,records_tsv,catalog_json;};
// Optional v1 extension: deterministic dataset-wide lookup index derived
// exclusively from validated package manifests.
bool has_resource_catalog(std::string_view root);
std::string resource_catalog(std::string_view root,const std::map<std::string,PackageSummary>&);
void validate_resource_catalog(std::string_view catalog,std::string_view root,
                               const std::map<std::string,PackageSummary>&);
struct SourceRecord {std::string id,name,format,category;std::uint64_t size{};};
// SourceRecord id/name are original byte strings, matching pack::stable_id.
AuditReport audit_report(std::string_view root,const std::map<std::string,PackageSummary>&,
                         const std::vector<SourceRecord>* expected_source=nullptr);
std::uint32_t crc32_update(std::uint32_t state,const unsigned char* data,std::size_t n);
}
