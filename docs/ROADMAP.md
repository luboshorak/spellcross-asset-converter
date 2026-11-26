# Asset Converter development roadmap

## Scope and non-negotiable project boundary

Spellcross Asset Converter is a standalone project. All implementation work for this roadmap is confined to this repository. Do not edit Spellcross Reloaded (SR), its loader, branches, dependencies or packaging, and do not begin integration unless the owner explicitly instructs it. Reading the existing game and restoration sources as format references is permitted. Preserve original bytes and report damaged or unsupported data honestly. Do not equate successful decoding, ZIP structural validation, or a `converted` status with complete gameplay semantics.

## Milestones (provisional versions, not release dates)

| Version | Scope | Acceptance criteria |
|---|---|---|
| 0.9.11 | Full PAK export audit | Real read-back inventory; per-record conversion reason and payload kind; original/derived storage accounting; source inventory matching when exporting; run on actual data before declaring coverage complete. |
| 0.9.12 | Finalize PAK structure and categorization | Explicit logical ownership, stable IDs, clear schema contracts, duplication/fallback accounting, documented archive layout. |
| 0.9.13 | Complete game metadata | Review missing animation timing/origins, palette contexts, fonts, unit fields, map layers, campaign/research/formation data and audiovisual metadata; record every unresolved field. |
| 0.9.14 | Cross-resource validation | Validate references between maps, units, graphics, animations, sounds, scripts, campaigns and language resources; distinguish missing, ambiguous and optional links. |
| 0.9.15 | Complete CZ and EN conversion | Repeatable full retail export and read-back; compare with source inventory and payloads, verify visuals and media; document remaining unavoidable source defects. |
| 1.0.0 | Standalone converter release | All previous gates satisfied, documented modern data contract and reproducible export; owner approves scope. |

## Current gate

0.9.10 merged; its build passed. Full retail PAK export is not yet demonstrated. 0.9.11 adds the audit machinery to obtain the missing evidence, not the evidence itself. The first actual full dataset audit is the input for the next milestone; do not jump ahead to SR integration. Keep `runtime_ready: false` until the data contract and semantic completeness have been demonstrated; runtime integration remains explicitly out of scope until separately authorized.
