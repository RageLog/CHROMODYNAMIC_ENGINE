// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_vehicle_editor/src/VehicleEditor.cpp
//
// phase676 — cd::editor::panel::vehicle_editor  implementation
// =============================================================================
#include <cd/editor/panel_vehicle_editor/VehicleEditor.hpp>

#include <algorithm>
#include <array>
#include <cstdint>

namespace cd::editor::panel::vehicle_editor
{

// ---------------------------------------------------------------------------
// State API
// ---------------------------------------------------------------------------

void VehicleEditor::set_config(
    const cd::physics::vehicle::VehicleConfig* cfg) noexcept
{
    config_ = cfg;
}

void VehicleEditor::set_state(
    const cd::physics::vehicle::VehicleState* state) noexcept
{
    state_ = state;
}

void VehicleEditor::simulate_click_apply(
    const cd::ui::widgets::Rect& bounds) noexcept
{
    // Any click within the panel bounds registers a pending apply.
    // Sprint-2 will hit-test against a specific "Apply" button row.
    if (bounds.is_valid())
        apply_pending_ = true;
}

bool VehicleEditor::apply_pending() const noexcept
{
    return apply_pending_;
}

void VehicleEditor::consume_apply() noexcept
{
    apply_pending_ = false;
}

// ---------------------------------------------------------------------------
// Private draw helpers
// ---------------------------------------------------------------------------

void VehicleEditor::draw_strip(
    cd::ui::renderer::DrawBatcher& batcher,
    const cd::ui::widgets::Theme&  theme,
    float x, float y, float w, float h,
    float norm_fill,
    cd::ui::renderer::Color fill_color) noexcept
{
    // Background track.
    batcher.quad(x, y, w, h,
                 cd::ui::renderer::Color {
                     theme.surface_hover.r,
                     theme.surface_hover.g,
                     theme.surface_hover.b,
                     theme.surface_hover.a });

    // Filled portion.
    const float fw = std::clamp(norm_fill, 0.0F, 1.0F) * w;
    if (fw > 0.0F)
        batcher.quad(x, y, fw, h, fill_color);
}

void VehicleEditor::draw_section_header(
    cd::ui::renderer::DrawBatcher& batcher,
    const cd::ui::widgets::Theme&  theme,
    float x, float y, float w) noexcept
{
    // Thin accent line as section separator.
    batcher.quad(x, y, w, 2.0F,
                 cd::ui::renderer::Color {
                     theme.accent.r,
                     theme.accent.g,
                     theme.accent.b,
                     100U });
}

// ---------------------------------------------------------------------------
// DrawBatcher path
// ---------------------------------------------------------------------------

void VehicleEditor::draw(
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

    const float content_x = bounds.x + kPad;
    const float content_w = bounds.w - 2.0F * kPad;

    // ---- 2. Title accent separator bar --------------------------------------
    batcher.quad(content_x, bounds.y + kPad,
                 content_w, kBarH,
                 cd::ui::renderer::Color {
                     theme.accent.r,
                     theme.accent.g,
                     theme.accent.b,
                     theme.accent.a });

    float cursor_y = bounds.y + kHeaderH;

    // ---- Helpers: draw a labelled row (background strip only) ---------------
    const float strip_h = kRowH - 4.0F;  // 3px top+bottom padding inside row.

    auto advance_row = [&]() noexcept {
        cursor_y += kRowH + kRowGap;
    };

    // =========================================================================
    // Section A — ENGINE
    // =========================================================================
    draw_section_header(batcher, theme, content_x, cursor_y, content_w);
    cursor_y += 2.0F + kRowGap;

    // Default config values used when no config is bound.
    const cd::physics::vehicle::VehicleConfig default_cfg {};
    const auto& cfg = (config_ != nullptr) ? *config_ : default_cfg;

    // — Max Torque strip (0..600 N·m) ----------------------------------------
    {
        constexpr float kMaxTorque = 600.0F;
        draw_strip(batcher, theme,
                   content_x, cursor_y + 2.0F, content_w, strip_h,
                   cfg.engine.max_torque_nm / kMaxTorque,
                   cd::ui::renderer::Color { 220U, 140U, 60U, 200U });
        advance_row();
    }

    // — 6 Gear-ratio strips (each ratio mapped into [0..5] range) -------------
    {
        constexpr float kMaxRatio = 5.0F;
        const float gear_w = (content_w - 5.0F * kRowGap) / 6.0F;
        for (std::size_t g = 0; g < 6U; ++g)
        {
            const float gx = content_x + static_cast<float>(g) * (gear_w + kRowGap);
            draw_strip(batcher, theme,
                       gx, cursor_y + 2.0F, gear_w, strip_h,
                       cfg.engine.gear_ratios[g] / kMaxRatio,
                       cd::ui::renderer::Color { 120U, 190U, 240U, 200U });
        }
        advance_row();
    }

    // — Idle RPM strip (0..2000) -----------------------------------------------
    {
        constexpr float kMaxIdleRpm = 2000.0F;
        draw_strip(batcher, theme,
                   content_x, cursor_y + 2.0F, content_w * 0.5F, strip_h,
                   cfg.engine.idle_rpm / kMaxIdleRpm,
                   cd::ui::renderer::Color { 160U, 200U, 120U, 200U });
        advance_row();
    }

    // — Max RPM strip (0..10000) -----------------------------------------------
    {
        constexpr float kMaxRpm = 10000.0F;
        draw_strip(batcher, theme,
                   content_x, cursor_y + 2.0F, content_w, strip_h,
                   cfg.engine.max_rpm / kMaxRpm,
                   cd::ui::renderer::Color { 240U, 80U, 80U, 200U });
        advance_row();
    }

    cursor_y += kSectionGap;

    // =========================================================================
    // Section B — WHEELS  (4 rows: FL / FR / RL / RR)
    // =========================================================================
    draw_section_header(batcher, theme, content_x, cursor_y, content_w);
    cursor_y += 2.0F + kRowGap;

    constexpr float kMaxRadius  = 0.6F;    // m
    constexpr float kMaxMass    = 50.0F;   // kg
    constexpr float kMaxStiff   = 60000.0F;// N/m

    for (std::size_t w = 0; w < 4U; ++w)
    {
        const auto& wc = cfg.wheels[w];
        // Three sub-strips per wheel row: radius | mass | stiffness
        constexpr float kThreeGap = 2.0F;
        const float sub_w = (content_w - 2.0F * kThreeGap) / 3.0F;

        // Radius (orange)
        draw_strip(batcher, theme,
                   content_x, cursor_y + 2.0F, sub_w, strip_h,
                   wc.radius / kMaxRadius,
                   cd::ui::renderer::Color { 200U, 160U, 60U, 200U });

        // Mass (cyan)
        draw_strip(batcher, theme,
                   content_x + sub_w + kThreeGap, cursor_y + 2.0F,
                   sub_w, strip_h,
                   wc.mass / kMaxMass,
                   cd::ui::renderer::Color { 60U, 200U, 200U, 200U });

        // Stiffness (violet)
        draw_strip(batcher, theme,
                   content_x + (sub_w + kThreeGap) * 2.0F, cursor_y + 2.0F,
                   sub_w, strip_h,
                   wc.suspension_stiffness / kMaxStiff,
                   cd::ui::renderer::Color { 180U, 100U, 220U, 200U });

        advance_row();
    }

    cursor_y += kSectionGap;

    // =========================================================================
    // Section C — CHASSIS
    // =========================================================================
    draw_section_header(batcher, theme, content_x, cursor_y, content_w);
    cursor_y += 2.0F + kRowGap;

    // — Chassis mass (0..5000 kg) ---------------------------------------------
    {
        constexpr float kMaxChassisMass = 5000.0F;
        draw_strip(batcher, theme,
                   content_x, cursor_y + 2.0F, content_w, strip_h,
                   cfg.chassis_mass_kg / kMaxChassisMass,
                   cd::ui::renderer::Color { 200U, 200U, 80U, 200U });
        advance_row();
    }

    // — Dimensions trio: length / width / height (0..10 m each) ---------------
    {
        constexpr float kMaxDim = 10.0F;
        const float dim_w = (content_w - 2.0F * kRowGap) / 3.0F;

        draw_strip(batcher, theme,
                   content_x, cursor_y + 2.0F, dim_w, strip_h,
                   cfg.chassis_dimensions[0] / kMaxDim,
                   cd::ui::renderer::Color { 200U, 80U, 80U, 200U });

        draw_strip(batcher, theme,
                   content_x + dim_w + kRowGap, cursor_y + 2.0F,
                   dim_w, strip_h,
                   cfg.chassis_dimensions[1] / kMaxDim,
                   cd::ui::renderer::Color { 80U, 200U, 80U, 200U });

        draw_strip(batcher, theme,
                   content_x + (dim_w + kRowGap) * 2.0F, cursor_y + 2.0F,
                   dim_w, strip_h,
                   cfg.chassis_dimensions[2] / kMaxDim,
                   cd::ui::renderer::Color { 80U, 80U, 200U, 200U });

        advance_row();
    }

    cursor_y += kSectionGap;

    // =========================================================================
    // Live state readout (only when state is bound and space remains)
    // =========================================================================
    if (state_ == nullptr)
        return;

    const float remaining = bounds.y + bounds.h - cursor_y - kPad;
    if (remaining < (kRowH + kRowGap) * 3.0F)
        return;  // Not enough space for readout.

    draw_section_header(batcher, theme, content_x, cursor_y, content_w);
    cursor_y += 2.0F + kRowGap;

    const auto& st = *state_;

    // — Speed bar  (-250..+250 km/h → normalise to [0..1] around centre) ------
    {
        constexpr float kSpeedRange = 250.0F;
        const float norm = (st.speed_kph + kSpeedRange) / (2.0F * kSpeedRange);
        draw_strip(batcher, theme,
                   content_x, cursor_y + 2.0F, content_w, strip_h,
                   std::clamp(norm, 0.0F, 1.0F),
                   cd::ui::renderer::Color { 100U, 220U, 100U, 200U });
        advance_row();
    }

    // — RPM bar  (0..max_rpm) --------------------------------------------------
    {
        const float max_rpm = cfg.engine.max_rpm > 0.0F ? cfg.engine.max_rpm : 6500.0F;
        draw_strip(batcher, theme,
                   content_x, cursor_y + 2.0F, content_w, strip_h,
                   st.rpm / max_rpm,
                   cd::ui::renderer::Color { 240U, 120U, 40U, 200U });
        advance_row();
    }

    // — Gear indicator: narrow accent block scaled to current gear / 6 --------
    {
        constexpr float kGearMax = 6.0F;
        const float norm = (static_cast<float>(st.gear) + 1.0F) / kGearMax;
        draw_strip(batcher, theme,
                   content_x, cursor_y + 2.0F, content_w * 0.3F, strip_h,
                   norm,
                   cd::ui::renderer::Color {
                         theme.accent.r,
                         theme.accent.g,
                         theme.accent.b,
                         200U });
        advance_row();
    }

    // — Throttle bar (green) ---------------------------------------------------
    {
        draw_strip(batcher, theme,
                   content_x, cursor_y + 2.0F, content_w, strip_h,
                   st.throttle,
                   cd::ui::renderer::Color { 60U, 220U, 60U, 220U });
        advance_row();
    }

    // — Brake bar (red) --------------------------------------------------------
    {
        draw_strip(batcher, theme,
                   content_x, cursor_y + 2.0F, content_w, strip_h,
                   st.brake,
                   cd::ui::renderer::Color { 220U, 60U, 60U, 220U });
        advance_row();
    }

    // — Steer bar (blue; centred: steer in [-1..1] → [0..1]) ------------------
    {
        const float norm = (st.steer + 1.0F) * 0.5F;
        draw_strip(batcher, theme,
                   content_x, cursor_y + 2.0F, content_w, strip_h,
                   std::clamp(norm, 0.0F, 1.0F),
                   cd::ui::renderer::Color { 80U, 120U, 220U, 220U });
        // advance_row() intentionally omitted — last row.
    }
}

}  // namespace cd::editor::panel::vehicle_editor
