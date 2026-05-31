// =============================================================================
// CHROMODYNAMIC — cd/editor/panel_asset_drop_target/AssetDropTarget.hpp
//
// phase594 — cd::editor::panel::asset_drop_target  (panel_asset_drop_target library)
//
// Asset Drop Target panel: renders a dashed-border rectangle within `bounds`
// labelled "Drop assets here". Accepted file extensions can be filtered via
// set_accepted_extensions. When a drop occurs, consume_dropped_path() returns
// true and transfers the path to the caller for routing into the asset pipeline
// (e.g. cd::asset::scene_streamer or the material pipeline).
//
// Provides a DrawBatcher-based draw path for the DockSpace shell (apps/editor).
// Renders:
//
//   * A coloured panel background.
//   * A dashed-border rectangle (four edge strips with gap segments).
//   * A centre-strip placeholder indicating the drop zone label.
//
// State API:
//   set_accepted_extensions(span<const string>) — filter; empty = accept all.
//   consume_dropped_path(string& out)           — true if a drop occurred this
//                                                  frame; clears internal buffer.
//   simulate_drop(string path)                  — push a path for tests.
//
// Lifetime contract:
//   AssetDropTarget is default-constructible and owns no heap storage beyond
//   an internal std::vector<std::string> for accepted extensions and a
//   std::optional<std::string> for the pending drop.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace cd::editor::panel::asset_drop_target
{

// ---------------------------------------------------------------------------
// AssetDropTarget
// ---------------------------------------------------------------------------
class AssetDropTarget
{
public:
    // Default-constructible; starts with no accepted-extension filter and no
    // pending drop.
    AssetDropTarget() noexcept = default;

    // ---- Filter API ---------------------------------------------------------

    /// Replace the accepted-extension list.
    /// Entries should be lower-case including the leading dot: {".gltf", ".png"}.
    /// An empty span clears the filter — all extensions are accepted.
    void set_accepted_extensions(std::span<const std::string> exts);

    /// Returns the number of accepted extensions currently stored.
    [[nodiscard]] std::size_t extension_count() const noexcept;

    // ---- Drop API -----------------------------------------------------------

    /// Returns true and writes the dropped path to `out_path` if a drop
    /// occurred this frame. Clears the internal buffer after transfer.
    /// Returns false (and leaves `out_path` unchanged) when no drop is pending.
    [[nodiscard]] bool consume_dropped_path(std::string& out_path);

    /// Simulate a drop by pushing `path` as if the user had dragged it onto
    /// the panel. If accepted_extensions is non-empty, the path extension must
    /// match one of them (case-sensitive); otherwise the push is a no-op.
    /// Designed for unit tests; does not interact with the OS.
    void simulate_drop(std::string path);

    // ---- DrawBatcher path (DockSpace / apps/editor) -------------------------

    /// Emit draw commands into `batcher` within `bounds`.
    /// Renders a background fill, dashed-border strips, and a centre label
    /// placeholder strip.
    ///
    /// Thread-safety: must be called from the render thread only.
    void draw(cd::ui::renderer::DrawBatcher& batcher,
              const cd::ui::widgets::Theme&  theme,
              const cd::ui::widgets::Rect&   bounds) const;

private:
    /// Returns true if `path` passes the extension filter (or if the filter is
    /// empty).
    [[nodiscard]] bool accepts(const std::string& path) const;

    std::vector<std::string>  accepted_exts_;  ///< Allowed extensions (may be empty).
    std::optional<std::string> pending_drop_;  ///< Queued drop path (one at most).
};

}  // namespace cd::editor::panel::asset_drop_target
