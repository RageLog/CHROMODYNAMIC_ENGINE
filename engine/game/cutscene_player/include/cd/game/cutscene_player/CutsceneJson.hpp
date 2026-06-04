// =============================================================================
// CHROMODYNAMIC — cd/game/cutscene_player/CutsceneJson.hpp
//
// Phase 703 — JSON serialization for cd::game::cutscene_player::Cutscene.
//
// Free functions in the cd::game::cutscene_player namespace:
//
//   save_to_json(const Cutscene&, const std::filesystem::path&) -> bool
//     Serializes a Cutscene to a UTF-8 JSON file at the given path.
//     Creates parent directories as needed.  Writes atomically (via .tmp
//     rename) so a crash mid-write never corrupts the previous file.
//     Returns true on success, false on any I/O error.
//
//   load_from_json(const std::filesystem::path&) -> std::optional<Cutscene>
//     Reads and parses a JSON file written by save_to_json.
//     Returns nullopt if:
//       * The file does not exist or an I/O error occurs.
//       * The content is not valid JSON.
//       * The "schema_version" field is missing or != 1.
//       * The "cutscene_id" field is missing (required for identification).
//     Unknown JSON keys are silently ignored (forward compatibility).
//
// JSON schema (schema_version 1):
//
//   {
//     "schema_version": 1,
//     "cutscene_id": "opening_cinematic",
//     "can_skip": true,
//     "phases": [
//       {
//         "phase_id": "intro",
//         "duration_ms": 3000.0,
//         "events": [
//           {
//             "offset_ms": 500.0,
//             "kind": 3,
//             "string_arg": "",
//             "vec3_arg": [0.0, 0.0, 0.0]
//           }
//         ]
//       }
//     ]
//   }
//
// Design:
//   * Uses cd::asset::json (the engine's hand-rolled JSON library) for both
//     serialization and parsing — same helper used by cd::asset::material_authoring.
//   * Atomic write: writes to <path>.tmp then renames — matches cd::editor::cdproj.
//   * EventKind is stored as its underlying uint8 integer value for forward compat.
//   * No dependency on ImGui, render, or audio headers.
//
// Threading: NOT thread-safe. Callers must synchronise externally.
// =============================================================================
#pragma once

#include <cd/game/cutscene_player/CutscenePlayer.hpp>

#include <filesystem>
#include <optional>

namespace cd::game::cutscene_player
{

/// Serialize `cutscene` and write it to `path` atomically.
///
/// Parent directories are created if they do not exist.
/// Atomic contract: writes to <path>.tmp first, then renames over <path>.
/// Returns true on success, false on any I/O error.
[[nodiscard]] bool save_to_json(const Cutscene&              cutscene,
                                const std::filesystem::path& path);

/// Read and parse a Cutscene JSON file written by save_to_json.
///
/// Returns std::nullopt if:
///   * The file does not exist or an I/O error occurs.
///   * The content is not valid JSON.
///   * schema_version != 1.
///   * cutscene_id field is absent.
[[nodiscard]] std::optional<Cutscene>
load_from_json(const std::filesystem::path& path);

}  // namespace cd::game::cutscene_player
