// =============================================================================
// CHROMODYNAMIC — cd/editor/panel_asset_browser/AssetBrowser.hpp
//
// phase545 — cd::editor::panel::asset_browser  (panel_asset_browser library)
//
// Asset Browser panel: lists file/directory entries with click selection.
// Provides a DrawBatcher-based draw path for the DockSpace shell
// (apps/editor). Renders a coloured background with one labelled row per
// visible entry; the selected row is highlighted with the accent colour.
//
// State API:
//   set_entries(span<const Entry>)  — replace the displayed entry list.
//   selected_index() const          — index of the currently selected entry
//                                     (npos if nothing is selected).
//   set_selected_index(size_t)      — programmatically change the selection.
//
// Entry:
//   struct Entry { std::string name; std::string path; bool is_dir; };
//
// Lifetime contract:
//   AssetBrowser is default-constructible and copies the entries on
//   set_entries. No external pointer ownership is required.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <cstddef>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace cd::editor::panel::asset_browser
{

// ---------------------------------------------------------------------------
// Entry — one item displayed by the asset browser.
// ---------------------------------------------------------------------------
struct Entry
{
    std::string name;    ///< Display name (file or directory name).
    std::string path;    ///< Full or relative path to the asset.
    bool        is_dir { false }; ///< True if this entry represents a directory.
};

// ---------------------------------------------------------------------------
// AssetBrowser
// ---------------------------------------------------------------------------
class AssetBrowser
{
public:
    /// Sentinel: no entry is selected.
    static constexpr std::size_t npos = std::numeric_limits<std::size_t>::max();

    // Default-constructible; starts with an empty entry list and no selection.
    AssetBrowser() noexcept = default;

    // ---- State API ----------------------------------------------------------

    /// Replace the displayed entry list.
    /// The entries are copied; the caller's storage is not retained.
    void set_entries(std::span<const Entry> entries);

    /// Returns the index of the currently selected entry, or npos if none.
    [[nodiscard]] std::size_t selected_index() const noexcept;

    /// Programmatically change the selected index.
    /// Passing npos or a value >= entry_count() clears the selection.
    void set_selected_index(std::size_t idx) noexcept;

    /// Returns the number of entries currently stored.
    [[nodiscard]] std::size_t entry_count() const noexcept;

    // ---- DrawBatcher path (DockSpace / apps/editor) -------------------------

    /// Emit draw commands into `batcher` within `bounds`.
    /// Renders the asset-browser panel background + one row per visible entry.
    /// The selected row receives an accent-colour highlight strip.
    ///
    /// Thread-safety: must be called from the render thread only.
    void draw(cd::ui::renderer::DrawBatcher& batcher,
              const cd::ui::widgets::Theme&  theme,
              const cd::ui::widgets::Rect&   bounds) const;

private:
    std::vector<Entry> entries_;             ///< Stored entries (copied on set).
    std::size_t        selected_ { npos };   ///< Currently selected index or npos.
};

}  // namespace cd::editor::panel::asset_browser
