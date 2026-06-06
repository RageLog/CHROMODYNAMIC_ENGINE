// =============================================================================
// CHROMODYNAMIC — cd/editor/cdproj/CdprojFile.hpp
//
// phase547 — .cdproj project-file read/write for cd::editor.
//
// A .cdproj file is a UTF-8 JSON document that persists just enough editor
// state to let the editor restore its visual session on the next launch:
//   * The last opened scene path.
//   * The DockSpace serialized layout (opaque string; caller converts
//     DockSpace::serialize()'s std::vector<std::byte> to/from string).
//   * The main window geometry (position + size + maximized flag).
//   * A capped FIFO list of recently opened files (max 10 entries).
//
// Design constraints (per CLAUDE.md §1 + task brief):
//   * No third-party JSON library — uses the same hand-rolled approach as
//     cd::game_save (cd::core-only transitive dependency).
//   * std::optional return semantics: read_cdproj() returns nullopt on any
//     error (missing file, malformed JSON, unknown schema version).
//   * write_cdproj() is atomic: it writes to a .tmp sibling then renames,
//     so a crash mid-write never leaves a half-written file behind.
//   * push_recent_file() deduplicates (move-to-front) and evicts from the
//     tail once the list exceeds 10 entries (FIFO policy).
//
// Format (schema_version 1):
//   {
//     "schema_version": 1,
//     "last_opened_scene_path": "assets/scenes/main.cdscene",
//     "dock_layout": "<opaque base64 or raw string from caller>",
//     "window": { "x": 100, "y": 50, "w": 1280, "h": 720, "maximized": false },
//     "recent_files": ["a.cdscene", "b.cdscene"]
//   }
//
// Schema evolution notes:
//   * schema_version == 1 is the only version accepted.  read_cdproj()
//     returns nullopt for any other value so a newer binary never silently
//     interprets a newer file incorrectly; the caller can offer the user a
//     "create new project" fallback.
//   * Unknown top-level keys are silently ignored (forward compat).
// =============================================================================
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cd::editor::cdproj
{

// ---------------------------------------------------------------------------
// Data structures
// ---------------------------------------------------------------------------

/// Window geometry captured on exit and restored on startup.
struct CdprojWindow
{
    int  x         { 0 };
    int  y         { 0 };
    int  w         { 1280 };
    int  h         { 720 };
    bool maximized { false };
};

/// Root data object for a .cdproj file.
struct CdprojData
{
    int                      schema_version        { 1 };
    std::string              last_opened_scene_path;
    std::string              dock_layout;
    CdprojWindow             window;
    std::vector<std::string> recent_files;
    /// phase695 / M14 W6A — active theme name; one of "dark" / "light" /
    /// "high_contrast". Defaults to "dark" when the field is absent (forward
    /// compatibility with pre-695 .cdproj files).
    std::string              theme_name            { "dark" };
    /// phase788 / H5 — Custom layout panel selection.
    /// Non-empty when the user chose "Custom" in the first-launch welcome
    /// dialog.  Contains the ordered list of panel IDs (matching those passed
    /// to DockSpace::register_panel) that the user enabled.  An empty list
    /// means the field is absent in the file (pre-788 .cdproj or a non-Custom
    /// preset was chosen).  Forward-compatible: older readers skip the key.
    std::vector<std::string> user_custom_layout;
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/// Read and parse a .cdproj file.
///
/// Returns std::nullopt if:
///   * The file does not exist.
///   * Any I/O error occurs.
///   * The content is not valid JSON.
///   * The schema_version field is missing or != 1.
///
/// Unknown JSON keys are silently ignored (forward compatibility).
[[nodiscard]] std::optional<CdprojData>
read_cdproj(const std::filesystem::path& path);

/// Serialise `data` and write it to `path` atomically.
///
/// Atomic contract: a temporary file (`path` + ".tmp") is written first,
/// then renamed over `path`.  On crash the previous version (or no file at
/// all on first write) is left intact.
///
/// Returns true on success, false on any I/O error.
[[nodiscard]] bool
write_cdproj(const CdprojData& data, const std::filesystem::path& path);

/// Push `path` onto the front of `data.recent_files`.
///
/// Deduplication: if `path` already exists anywhere in the list it is
/// removed from its current position (move-to-front).  After insertion the
/// list is trimmed to at most 10 entries (tail eviction / FIFO policy).
void push_recent_file(CdprojData& data, std::string path);

/// Platform-appropriate default location for the editor project file.
///
/// Windows : %APPDATA%\cd_editor\last.cdproj
/// macOS   : $HOME/Library/Application Support/cd_editor/last.cdproj
/// Linux   : $XDG_CONFIG_HOME/cd_editor/last.cdproj
///           or $HOME/.config/cd_editor/last.cdproj
[[nodiscard]] std::filesystem::path default_cdproj_path();

}  // namespace cd::editor::cdproj
