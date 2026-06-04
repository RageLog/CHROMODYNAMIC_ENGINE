// =============================================================================
// CHROMODYNAMIC — cd/editor/panel_settings/SettingsPanel.hpp
//
// phase698 — cd::editor::panel::settings  (panel_settings library)
//
// Settings panel: 5-category tab strip at the top + a key/value entry list
// below. A user opens the panel, switches to the Graphics tab, edits
// 'vsync=on' to 'vsync=off' — the engine reads the change next frame.
//
// Category enum
// ─────────────
//   kGraphics — rendering, vsync, resolution, AA, shadow quality
//   kInput    — key bindings, mouse sensitivity, deadzone
//   kAudio    — volume, output device, spatial audio
//   kEditor   — theme, grid snap, auto-save interval
//   kAdvanced — developer flags, shader cache, logging verbosity
//
// Entry struct
// ────────────
//   key      — unique identifier for programmatic access, e.g. "vsync"
//   label    — display label, e.g. "V-Sync"
//   value    — current value string, e.g. "on"
//   tooltip  — shown when the entry has focus
//   category — which tab this entry belongs to
//
// Class API
// ─────────
//   register_entry(const Entry&)           — add a setting entry
//   set_value(key, value)                  — replace the value for a key
//   get_value(key) const                   — retrieve value or nullopt
//   set_active_category(Category)          — switch visible tab
//   entry_count() const                    — total registered entries
//   entries_in_category(Category) const    — entries for a specific category
//   draw(DrawBatcher&, Theme&, Rect&) const — emit draw commands
//
// Lifetime contract
// ─────────────────
//   SettingsPanel is default-constructible and owns its entry storage.
//   Thread-safe for read, single-thread for write.
//
// MOMENT: A user opens Settings, switches to Graphics, edits vsync=on to
// vsync=off — the engine respects it the next frame.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cd::editor::panel::settings
{

// ---------------------------------------------------------------------------
// Category — the five settings tabs.
// ---------------------------------------------------------------------------
enum class Category : std::uint8_t
{
    kGraphics,
    kInput,
    kAudio,
    kEditor,
    kAdvanced,
};

// ---------------------------------------------------------------------------
// Entry — one settings item (key/value pair with metadata).
// ---------------------------------------------------------------------------
struct Entry
{
    std::string key;      ///< Unique programmatic identifier, e.g. "vsync".
    std::string label;    ///< Human-readable label shown in the UI.
    std::string value;    ///< Current value string.
    std::string tooltip;  ///< Shown when the entry row has focus.
    Category    category { Category::kGraphics };
};

// ---------------------------------------------------------------------------
// SettingsPanel
// ---------------------------------------------------------------------------
class SettingsPanel
{
public:
    /// Default-constructible. Active category starts at kGraphics.
    SettingsPanel() noexcept = default;

    // ---- Entry registry -----------------------------------------------------

    /// Add a settings entry. Duplicate keys are allowed (last write wins
    /// in set_value; first match wins in get_value).
    void register_entry(const Entry& entry);

    /// Update the value string for an entry identified by `key`.
    /// No-op if the key is not registered.
    void set_value(std::string_view key, std::string value);

    /// Retrieve the current value for `key`, or std::nullopt if not found.
    [[nodiscard]] std::optional<std::string> get_value(std::string_view key) const;

    // ---- Tab / category API -------------------------------------------------

    /// Switch the active (visible) category tab.
    void set_active_category(Category category) noexcept;

    /// Returns the currently active category.
    [[nodiscard]] Category active_category() const noexcept;

    // ---- Counts -------------------------------------------------------------

    /// Total number of registered entries across all categories.
    [[nodiscard]] std::size_t entry_count() const noexcept;

    /// Number of entries belonging to `category`.
    [[nodiscard]] std::size_t entries_in_category(Category category) const noexcept;

    // ---- DrawBatcher path (DockSpace / apps/editor) -------------------------

    /// Emit draw commands into `batcher` within `bounds`.
    ///
    /// Renders (in order):
    ///   1. Panel background fill.
    ///   2. Tab strip — 5 category tabs; active tab gets accent highlight.
    ///   3. Accent separator bar below the tab strip.
    ///   4. Entry rows for the active category:
    ///        * Row background quad (alternating shade).
    ///        * Narrow accent-colour swatch on the left edge.
    ///        * Wider value-field quad on the right side of each row.
    ///      When an entry has a tooltip the row shows a subtle focus ring.
    ///
    /// Thread-safety: call from the render thread only.
    void draw(cd::ui::renderer::DrawBatcher& batcher,
              const cd::ui::widgets::Theme&  theme,
              const cd::ui::widgets::Rect&   bounds) const;

private:
    // ---- Stored data --------------------------------------------------------
    std::vector<Entry> entries_          {};      ///< All registered entries.
    Category           active_category_  { Category::kGraphics };

    // ---- Layout constants ---------------------------------------------------
    static constexpr float kPad        =  6.0F;  ///< Outer horizontal/vertical padding.
    static constexpr float kTabH       = 24.0F;  ///< Tab strip height.
    static constexpr float kBarH       =  3.0F;  ///< Accent separator bar height.
    static constexpr float kRowH       = 22.0F;  ///< Height of one entry row.
    static constexpr float kRowGap     =  2.0F;  ///< Gap between rows.
    static constexpr float kSwatchW    =  3.0F;  ///< Left-edge category-colour swatch width.
    static constexpr float kValueRatio = 0.45F;  ///< Right portion of row reserved for value field.
    static constexpr std::size_t kCategoryCount = 5U;

    /// Y offset (relative to bounds.y) where the first entry row begins.
    static constexpr float kListOffsetY =
        kPad + kTabH + kPad + kBarH + kPad;

    /// Y coordinate of entry row `i` relative to bounds.y.
    [[nodiscard]] static float row_top(std::size_t i) noexcept
    {
        return kListOffsetY + static_cast<float>(i) * (kRowH + kRowGap);
    }

    // ---- Helpers ------------------------------------------------------------

    /// Returns the accent colour swatch for a given category index (0-4).
    [[nodiscard]] static cd::ui::renderer::Color category_swatch_color(Category cat) noexcept;
};

}  // namespace cd::editor::panel::settings
