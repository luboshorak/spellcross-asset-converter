# Asset Converter development roadmap

## Scope and non-negotiable project boundary

Spellcross Asset Converter is a standalone project. All implementation work for this roadmap is confined to this repository. Do not edit Spellcross Reloaded (SR), its loader, branches, dependencies or packaging, and do not begin integration unless the owner explicitly instructs it. Reading the existing game and restoration sources as format references is permitted. Preserve original bytes and report damaged or unsupported data honestly. Do not equate successful decoding, ZIP structural validation, or a `converted` status with complete gameplay semantics.

## Milestones (provisional versions, not release dates)

| Version | Scope | Acceptance criteria |
|---|---|---|
| 0.9.11 | Full PAK export audit | Real read-back inventory; per-record conversion reason and payload kind; original/derived storage accounting; source inventory matching when exporting; run on actual data before declaring coverage complete. |
| 0.9.11.2 | Performance optimization | Preserve complete inventory and decoded payloads while reducing export time; measure package and validation time; mark damaged retail tile fallback explicitly. |
| 0.9.12 | Finalize PAK structure and categorization | Explicit logical ownership, stable IDs, clear schema contracts, duplication/fallback accounting, documented archive layout. |
| 0.9.13 | Complete game metadata | Review missing animation timing/origins, palette contexts, fonts, unit fields, map layers, campaign/research/formation data and audiovisual metadata; record every unresolved field. |
| 0.9.14 | Cross-resource validation | Validate references between maps, units, graphics, animations, sounds, scripts, campaigns and language resources; distinguish missing, ambiguous and optional links. |
| 0.9.15 | Complete CZ and EN conversion | Repeatable full retail export and read-back; compare with source inventory and payloads, verify visuals and media; document remaining unavoidable source defects. |
| 1.0.0 | Standalone converter release | All previous gates satisfied, documented modern data contract and reproducible export; owner approves scope. |

## Current gate

0.9.11.1 completed a full Czech ISO export with successful structural validation and source inventory match: 32,643 records, 32,555 converted, 72 raw, 2 failed, 14 containers, 31,331 frames. User reported excessive export time. 0.9.11.2 is the performance milestone before PAK structure changes. Preserve the baseline coverage and exact modern payload content, except explicitly documented synthetic fallback and lossless media encoding/container differences. Full English export and semantic validation remain pending. Keep `runtime_ready: false` until the data contract and semantic completeness have been demonstrated; runtime integration remains explicitly out of scope until separately authorized.

0.9.11.2 Czech performance export passed: 104.146 s total, 68.080 s movies, unchanged coverage and 31,332 frames (one explicit transparent sentinel added). 0.9.12 begins with the remaining small retail tables and legacy-only role classification. The unresolved evidence ZIP is the input for investigating BOXER.LST, TEMPFILE.MAP and TEMP.DPK; do not assume they are unnecessary. Cross-package ownership and semantic reference checks still require work.

0.9.12.1 resolves the three identified residual file types by owner-approved policy: convert TEMPFILE.MAP using the existing validated map schema; preserve BOXER.LST and 525-byte TEMP.DPK as explicit archival-only originals. The supplied reference COMMON.FS map passes all eight layer bounds checks, but the latest CZ/EN ISO exports remain to be verified. No claim of zero unresolved until real audit. Next contract gate: explicit package/resource ownership and cross-package lookup design, followed by 0.9.13 metadata review. Preserve all IDs, original archive provenance and current PAK v1 compatibility.

0.9.12.2 introduces the dataset-wide resource catalog as a backwards-compatible PAK v1 extension: explicit package owners, original ranges, payload and atlas locators, exact IDs, collision-aware names and whole graphic groups. Old PAKs can be indexed from read-back. Validate the catalog against actual manifests, not pre-export assumptions. This is the ownership/lookup contract; physical package layout stays unchanged to avoid duplicating assets or changing verified IDs. The next 0.9.13 gate is metadata completeness, starting with unit/FSU groups, their animation semantics and strategic map definitions. 0.9.14 will use the catalog to resolve and validate actual cross-resource links. Full CZ/EN export and runtime certification remain pending.

Combined 0.9.13 / 0.9.14 implementation (version 0.9.14): explicit unit flags and original-byte consistency checks, optional FSA context decode and layout checks, actual tactical tile index verification, typed ordered domain blocks and reference extraction/resolution from validated package payloads. game-metadata.json and game-references.tsv expose all known links, candidate owners and unresolved outcomes; never hide unknown commands or absent origins. Real full CZ/EN audits are the next acceptance gate. Completion of implementation is not semantic certification; close the remaining issues revealed by the audits, review timing/coordinates/unknown commands and original-source parity before declaring 1.0.0. SR remains untouched.
