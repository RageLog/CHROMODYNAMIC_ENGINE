// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_scene_navigator/src/SceneNavigator.cpp
//
// phase687 — cd::editor::panel::scene_navigator  implementation
// =============================================================================
#include <cd/editor/panel_scene_navigator/SceneNavigator.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ranges>
#include <string>

namespace cd::editor::panel::scene_navigator
{

// ---------------------------------------------------------------------------
// Internal helper: convert a string to lower-case in-place.
// ---------------------------------------------------------------------------
namespace
{

[[nodiscard]] std::string to_lower(std::string_view sv)
{
    std::string result;
    result.reserve(sv.size());
    for (const char c : sv)
        result.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    return result;
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// State API
// ---------------------------------------------------------------------------

void SceneNavigator::set_entities(std::span<const cd::ecs::Entity> entities,
                                  std::span<const std::string>     names)
{
    // Defensive: silently truncate to the shorter span if lengths differ.
    const std::size_t count = std::min(entities.size(), names.size());

    entities_.assign(entities.begin(), entities.begin() + static_cast<std::ptrdiff_t>(count));
    names_.assign(names.begin(),       names.begin()    + static_cast<std::ptrdiff_t>(count));

    // Invalidate selection if the previously selected entity is no longer present.
    if (selected_.has_value())
    {
        const cd::ecs::Entity sel = *selected_;
        const bool still_present = std::ranges::any_of(entities_,
            [sel](const cd::ecs::Entity& e) { return e == sel; });
        if (!still_present)
            selected_.reset();
    }

    rebuild_filter();
}

void SceneNavigator::set_filter(std::string_view substring)
{
    filter_ = to_lower(substring);
    rebuild_filter();
}

std::optional<cd::ecs::Entity> SceneNavigator::selected_entity() const noexcept
{
    return selected_;
}

std::span<const std::size_t> SceneNavigator::filtered_indices() const noexcept
{
    return { filtered_.data(), filtered_.size() };
}

void SceneNavigator::simulate_click(
    float x, float y, const cd::ui::widgets::Rect& bounds) noexcept
{
    // Outside panel?
    if (x < bounds.x || x > bounds.x + bounds.w ||
        y < bounds.y || y > bounds.y + bounds.h)
    {
        return;
    }

    const float rel_y = y - bounds.y;

    // Above the list area (search box region)?
    if (rel_y < kListOffsetY)
        return;

    // Hit-test each filtered row.
    for (std::size_t fi = 0; fi < filtered_.size(); ++fi)
    {
        const float top = row_top(fi);
        if (rel_y >= top && rel_y < top + kRowH)
        {
            // fi is the index into filtered_; map back to entity.
            selected_ = entities_[filtered_[fi]];
            return;
        }
    }
    // Click landed in a gap or below the list — keep existing selection.
}

void SceneNavigator::simulate_text_input(std::string_view typed)
{
    if (typed.empty())
        return;
    // Append to the raw filter text (preserve user's casing in the stored
    // lower-case representation).
    std::string appended = filter_;
    for (const char c : typed)
        appended.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    filter_ = std::move(appended);
    rebuild_filter();
}

// ---------------------------------------------------------------------------
// DrawBatcher path
// ---------------------------------------------------------------------------

void SceneNavigator::draw(
    cd::ui::renderer::DrawBatcher& batcher,
    const cd::ui::widgets::Theme&  theme,
    const cd::ui::widgets::Rect&   bounds) const
{
    // ---- 1. Panel background ------------------------------------------------
    batcher.quad(bounds.x, bounds.y, bounds.w, bounds.h,
                 cd::ui::renderer::Color {
                     theme.surface.r,
                     theme.surface.g,
                     theme.surface.b,
                     theme.surface.a });

    if (!bounds.is_valid())
        return;

    const float inner_w = bounds.w - 2.0F * kPad;

    // ---- 2. Search box background ------------------------------------------
    // Slightly lighter than panel surface so the search box reads as an
    // interactive input area distinct from the list below it.
    batcher.quad(bounds.x + kPad, bounds.y + kPad,
                 inner_w, kSearchH,
                 cd::ui::renderer::Color {
                     theme.surface_hover.r,
                     theme.surface_hover.g,
                     theme.surface_hover.b,
                     theme.surface_hover.a });

    // ---- 3. Accent separator bar -------------------------------------------
    const float bar_y = bounds.y + kPad + kSearchH + kPad;
    batcher.quad(bounds.x + kPad, bar_y,
                 inner_w, kBarH,
                 cd::ui::renderer::Color {
                     theme.accent.r,
                     theme.accent.g,
                     theme.accent.b,
                     theme.accent.a });

    // ---- 4. Filtered entity rows -------------------------------------------
    for (std::size_t fi = 0; fi < filtered_.size(); ++fi)
    {
        const std::size_t idx = filtered_[fi];
        const float       ry  = bounds.y + row_top(fi);

        // Clip rows that overflow the panel bottom.
        if (ry + kRowH > bounds.y + bounds.h - kPad)
            break;

        const bool is_selected =
            selected_.has_value() && entities_[idx] == *selected_;

        if (is_selected)
        {
            // Accent highlight strip for the selected row.
            batcher.quad(bounds.x + kPad, ry,
                         inner_w, kRowH,
                         cd::ui::renderer::Color {
                             theme.accent.r,
                             theme.accent.g,
                             theme.accent.b,
                             60U });
        }
        else
        {
            // Subtle alternating shade for unselected rows (even rows are
            // slightly darker; odd rows match the surface_hover level).
            const bool even = (fi % 2U == 0U);
            const auto bg = even ? theme.surface : theme.surface_hover;
            batcher.quad(bounds.x + kPad, ry,
                         inner_w, kRowH,
                         cd::ui::renderer::Color {
                             bg.r, bg.g, bg.b, bg.a });
        }

        // Entity-ID indicator swatch: a narrow colour bar on the left edge.
        // Hue cycles through 6 accent colours based on entity slot id % 6.
        constexpr float kSwatchW = 3.0F;
        const auto slot_colour = [&]() -> cd::ui::renderer::Color
        {
            switch (entities_[idx].id % 6U)
            {
            case 0U: return { 96U, 160U, 255U, 220U };   // blue  (default accent)
            case 1U: return { 80U, 200U, 100U, 220U };   // green
            case 2U: return {220U, 200U,  80U, 220U };   // yellow
            case 3U: return {200U,  80U,  80U, 220U };   // red
            case 4U: return {180U,  80U, 220U, 220U };   // purple
            default: return {120U, 210U, 200U, 220U };   // teal
            }
        }();
        batcher.quad(bounds.x + kPad, ry + 2.0F,
                     kSwatchW, kRowH - 4.0F,
                     slot_colour);
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void SceneNavigator::rebuild_filter() noexcept
{
    filtered_.clear();

    if (filter_.empty())
    {
        // No filter — all indices pass through.
        filtered_.reserve(entities_.size());
        for (std::size_t i = 0; i < entities_.size(); ++i)
            filtered_.push_back(i);
        return;
    }

    for (std::size_t i = 0; i < names_.size(); ++i)
    {
        // Case-insensitive substring search: lower-case the name on the fly.
        const std::string lower_name = to_lower(names_[i]);
        if (lower_name.find(filter_) != std::string::npos)
            filtered_.push_back(i);
    }
}

}  // namespace cd::editor::panel::scene_navigator
