// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_inspector/src/Inspector.cpp
//
// phase543 — cd::editor::panel::inspector  implementation
// =============================================================================
#include <cd/editor/panel_inspector/Inspector.hpp>

#include <cd/editor/EditHistory.hpp>
#include <cd/editor/TransformCommands.hpp>
#include <cd/material/Material.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Vector.hpp>
#include <cd/scene/Scene.hpp>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <string>

namespace cd::editor::panel::inspector
{

// ---------------------------------------------------------------------------
// State API
// ---------------------------------------------------------------------------

void Inspector::set_target(cd::ecs::Entity e) noexcept
{
    target_ = e;
}

void Inspector::set_world_ptr(cd::ecs::World* world) noexcept
{
    world_ = world;
}

cd::ecs::Entity Inspector::get_selected() const noexcept
{
    return target_;
}

// ---------------------------------------------------------------------------
// M12 W2 — PBR / alpha-mode material binding
// ---------------------------------------------------------------------------

void Inspector::set_material_instance(cd::material::MaterialInstance* mi) noexcept
{
    material_instance_ = mi;
}

cd::material::MaterialInstance* Inspector::material_instance() const noexcept
{
    return material_instance_;
}

void Inspector::set_metallic(float m) noexcept
{
    if (material_instance_ != nullptr)
        material_instance_->set_metallic(m);
}

void Inspector::set_roughness(float r) noexcept
{
    if (material_instance_ != nullptr)
        material_instance_->set_roughness(r);
}

void Inspector::set_alpha_mode(cd::material::AlphaMode mode) noexcept
{
    if (material_instance_ != nullptr)
        material_instance_->set_alpha_mode(mode);
}

void Inspector::set_alpha_cutoff(float c) noexcept
{
    if (material_instance_ != nullptr)
        material_instance_->set_alpha_cutoff(c);
}

float Inspector::metallic() const noexcept
{
    // Defaults mirror the MaterialInstance constructor (dielectric, half-rough).
    return material_instance_ != nullptr ? material_instance_->metallic() : 0.0F;
}

float Inspector::roughness() const noexcept
{
    return material_instance_ != nullptr ? material_instance_->roughness() : 0.5F;
}

cd::material::AlphaMode Inspector::alpha_mode() const noexcept
{
    return material_instance_ != nullptr ? material_instance_->alpha_mode()
                                         : cd::material::AlphaMode::kOpaque;
}

float Inspector::alpha_cutoff() const noexcept
{
    return material_instance_ != nullptr ? material_instance_->alpha_cutoff() : 0.5F;
}

// ---------------------------------------------------------------------------
// DrawBatcher path
// ---------------------------------------------------------------------------

void Inspector::draw(cd::ui::renderer::DrawBatcher& batcher,
                     const cd::ui::widgets::Theme&   theme,
                     const cd::ui::widgets::Rect&    bounds) const
{
    // Background fill.
    batcher.quad(bounds.x, bounds.y, bounds.w, bounds.h,
                 cd::ui::renderer::Color {
                     theme.surface.r,
                     theme.surface.g,
                     theme.surface.b,
                     theme.surface.a });

    if (!bounds.is_valid())
        return;

    constexpr float kRowH    = 20.0F;
    constexpr float kPad     = 6.0F;
    constexpr float kBarH    = 4.0F;
    const float     row_w    = bounds.w - 2.0F * kPad;

    // Separator bar under the title area.
    batcher.quad(bounds.x + kPad, bounds.y + kPad,
                 row_w, kBarH,
                 cd::ui::renderer::Color {
                     theme.accent.r,
                     theme.accent.g,
                     theme.accent.b,
                     theme.accent.a });

    if (!target_.is_valid() || world_ == nullptr)
    {
        // "no selection" indicator row.
        batcher.quad(bounds.x + kPad, bounds.y + kPad + kBarH + kPad,
                     row_w * 0.4F, kRowH,
                     cd::ui::renderer::Color {
                         theme.text_dim.r,
                         theme.text_dim.g,
                         theme.text_dim.b,
                         120U });
        return;
    }

    // Field rows — three rows for Position / Scale / Rotation.
    const cd::scene::LocalTransform* lt =
        world_->get<cd::scene::LocalTransform>(target_);
    if (lt == nullptr)
        return;

    float cursor_y = bounds.y + kPad * 2.0F + kBarH;

    // Helper: draw a labelled value strip.
    auto draw_row = [&](float norm_x, float norm_y, float norm_z)
    {
        // Label placeholder (accent bar on the left).
        batcher.quad(bounds.x + kPad, cursor_y,
                     6.0F, kRowH,
                     cd::ui::renderer::Color {
                         theme.accent.r,
                         theme.accent.g,
                         theme.accent.b,
                         200U });

        // Three value strips (X=red, Y=green, Z=blue tinted).
        const float strip_w = (row_w - 6.0F - kPad) / 3.0F - 2.0F;
        const float sx      = bounds.x + kPad + 6.0F + kPad * 0.5F;

        // --- X ---
        const float fill_x = std::clamp((norm_x + 1.0F) * 0.5F, 0.0F, 1.0F);
        batcher.quad(sx, cursor_y,
                     strip_w, kRowH,
                     cd::ui::renderer::Color {
                         theme.surface_hover.r,
                         theme.surface_hover.g,
                         theme.surface_hover.b,
                         theme.surface_hover.a });
        batcher.quad(sx, cursor_y,
                     strip_w * fill_x, kRowH,
                     cd::ui::renderer::Color { 200U, 80U, 80U, 200U });

        // --- Y ---
        const float fill_y = std::clamp((norm_y + 1.0F) * 0.5F, 0.0F, 1.0F);
        batcher.quad(sx + strip_w + 2.0F, cursor_y,
                     strip_w, kRowH,
                     cd::ui::renderer::Color {
                         theme.surface_hover.r,
                         theme.surface_hover.g,
                         theme.surface_hover.b,
                         theme.surface_hover.a });
        batcher.quad(sx + strip_w + 2.0F, cursor_y,
                     strip_w * fill_y, kRowH,
                     cd::ui::renderer::Color { 80U, 200U, 80U, 200U });

        // --- Z ---
        const float fill_z = std::clamp((norm_z + 1.0F) * 0.5F, 0.0F, 1.0F);
        batcher.quad(sx + (strip_w + 2.0F) * 2.0F, cursor_y,
                     strip_w, kRowH,
                     cd::ui::renderer::Color {
                         theme.surface_hover.r,
                         theme.surface_hover.g,
                         theme.surface_hover.b,
                         theme.surface_hover.a });
        batcher.quad(sx + (strip_w + 2.0F) * 2.0F, cursor_y,
                     strip_w * fill_z, kRowH,
                     cd::ui::renderer::Color { 80U, 80U, 200U, 200U });

        cursor_y += kRowH + kPad;
    };

    // Position (normalised to [-20, 20] → [0, 1]).
    {
        const cd::math::Vec3f& p = lt->value.position;
        constexpr float kRange = 20.0F;
        draw_row(p.x / kRange, p.y / kRange, p.z / kRange);
    }

    // Scale (normalised to [0, 10] → [0, 1]).
    {
        const cd::math::Vec3f& s = lt->value.scale;
        constexpr float kRange = 10.0F;
        draw_row(s.x / kRange, s.y / kRange, s.z / kRange);
    }

    // Rotation — display as a fraction of the quaternion components
    // (w, x, y, z clamped to [-1, 1]).
    {
        const cd::math::Quatf& q = lt->value.rotation;
        draw_row(q.x, q.y, q.z);
    }

    // ---- M12 W2 — PBR / alpha-mode section --------------------------------
    //
    // Rendered only when a MaterialInstance is bound via set_material_instance.
    // Shows metallic + roughness sliders, a 3-button alpha-mode dropdown, and
    // a conditional alpha_cutoff slider (kMask only). Each control is a
    // background strip plus a filled inner strip whose width reflects the
    // current value, mirroring the LocalTransform row pattern above.
    if (material_instance_ != nullptr)
    {
        // PBR section separator bar (slightly dimmer than the title bar).
        cursor_y += kPad * 0.5F;
        batcher.quad(bounds.x + kPad, cursor_y,
                     row_w, 2.0F,
                     cd::ui::renderer::Color {
                         theme.accent.r,
                         theme.accent.g,
                         theme.accent.b,
                         140U });
        cursor_y += 2.0F + kPad;

        // Helper: draw a single horizontal slider strip filled to `norm`.
        auto draw_slider = [&](float norm, cd::ui::renderer::Color fill_color)
        {
            const float clamped = std::clamp(norm, 0.0F, 1.0F);
            // Background.
            batcher.quad(bounds.x + kPad, cursor_y,
                         row_w, kRowH,
                         cd::ui::renderer::Color {
                             theme.surface_hover.r,
                             theme.surface_hover.g,
                             theme.surface_hover.b,
                             theme.surface_hover.a });
            // Fill.
            if (clamped > 0.0F)
            {
                batcher.quad(bounds.x + kPad, cursor_y,
                             row_w * clamped, kRowH,
                             fill_color);
            }
            cursor_y += kRowH + kPad;
        };

        // Metallic slider (gold-tinted fill).
        draw_slider(material_instance_->metallic(),
                    cd::ui::renderer::Color { 220U, 180U, 80U, 220U });

        // Roughness slider (steel-blue fill).
        draw_slider(material_instance_->roughness(),
                    cd::ui::renderer::Color { 140U, 150U, 200U, 220U });

        // Alpha-mode dropdown: three side-by-side cells. The active mode gets
        // the accent colour, inactive ones the surface_hover colour. Cells are
        // sized equally inside row_w with two 2px gaps.
        const cd::material::AlphaMode mode = material_instance_->alpha_mode();
        constexpr float kCellGap = 2.0F;
        const float     cell_w   = (row_w - 2.0F * kCellGap) / 3.0F;

        auto draw_cell = [&](float ox, bool active,
                             cd::ui::renderer::Color active_fill)
        {
            if (active)
            {
                batcher.quad(bounds.x + kPad + ox, cursor_y,
                             cell_w, kRowH, active_fill);
            }
            else
            {
                batcher.quad(bounds.x + kPad + ox, cursor_y,
                             cell_w, kRowH,
                             cd::ui::renderer::Color {
                                 theme.surface_hover.r,
                                 theme.surface_hover.g,
                                 theme.surface_hover.b,
                                 theme.surface_hover.a });
            }
        };

        draw_cell(0.0F,
                  mode == cd::material::AlphaMode::kOpaque,
                  cd::ui::renderer::Color {
                      theme.accent.r,
                      theme.accent.g,
                      theme.accent.b,
                      theme.accent.a });
        draw_cell(cell_w + kCellGap,
                  mode == cd::material::AlphaMode::kMask,
                  cd::ui::renderer::Color { 220U, 160U, 60U, 230U });
        draw_cell((cell_w + kCellGap) * 2.0F,
                  mode == cd::material::AlphaMode::kBlend,
                  cd::ui::renderer::Color { 160U, 100U, 220U, 230U });
        cursor_y += kRowH + kPad;

        // Alpha cutoff slider — only visible when alpha_mode == kMask.
        if (mode == cd::material::AlphaMode::kMask)
        {
            draw_slider(material_instance_->alpha_cutoff(),
                        cd::ui::renderer::Color { 220U, 160U, 60U, 230U });
        }
    }
}

// ---------------------------------------------------------------------------
// ImGui path
// ---------------------------------------------------------------------------

bool Inspector::draw_imgui(cd::scene::Scene&      scene,
                           cd::editor::EditHistory& history,
                           float&                 rot_slider_deg,
                           cd::ecs::Entity&       rot_slider_entity) const
{
    if (!target_.is_valid())
    {
        ImGui::TextDisabled("no selection — click an entity in Scene Tree");
        return false;
    }

    auto* lt = scene.local(target_);
    if (lt == nullptr)
    {
        ImGui::TextDisabled("selected entity has no LocalTransform");
        return false;
    }

    // Entity header.
    ImGui::Text("Entity id=%u", target_.id);
    ImGui::Separator();

    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.60F);

    // ---- Position ----------------------------------------------------------
    ImGui::SeparatorText("Position");
    {
        static cd::math::Vec3f pre_pos {};
        float xyz[3] {
            lt->value.position.x,
            lt->value.position.y,
            lt->value.position.z };
        ImGui::Text("X Y Z");
        ImGui::SameLine();
        const bool changed = ImGui::DragFloat3("##insp_pos", xyz, 0.05F,
                                               -20.0F, 20.0F, "%.3f");
        if (ImGui::IsItemActivated())  pre_pos = lt->value.position;
        if (changed) lt->value.position = { xyz[0], xyz[1], xyz[2] };
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            const cd::math::Vec3f delta {
                lt->value.position.x - pre_pos.x,
                lt->value.position.y - pre_pos.y,
                lt->value.position.z - pre_pos.z };
            if (delta.x != 0.0F || delta.y != 0.0F || delta.z != 0.0F)
            {
                lt->value.position = pre_pos;
                history.push(std::make_unique<cd::editor::TranslateCommand>(
                    scene, target_, delta));
            }
        }
    }

    // ---- Scale -------------------------------------------------------------
    ImGui::SeparatorText("Scale");
    {
        static cd::math::Vec3f pre_scale { 1.0F, 1.0F, 1.0F };
        float xyz[3] {
            lt->value.scale.x,
            lt->value.scale.y,
            lt->value.scale.z };
        ImGui::Text("X Y Z");
        ImGui::SameLine();
        const bool changed = ImGui::DragFloat3("##insp_scale", xyz, 0.02F,
                                               0.01F, 10.0F, "%.3f");
        if (ImGui::IsItemActivated())  pre_scale = lt->value.scale;
        if (changed) lt->value.scale = { xyz[0], xyz[1], xyz[2] };
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            const cd::math::Vec3f factor {
                (pre_scale.x != 0.0F) ? (lt->value.scale.x / pre_scale.x) : 1.0F,
                (pre_scale.y != 0.0F) ? (lt->value.scale.y / pre_scale.y) : 1.0F,
                (pre_scale.z != 0.0F) ? (lt->value.scale.z / pre_scale.z) : 1.0F };
            if (factor.x != 1.0F || factor.y != 1.0F || factor.z != 1.0F)
            {
                lt->value.scale = pre_scale;
                history.push(std::make_unique<cd::editor::ScaleCommand>(
                    scene, target_, factor));
            }
        }
    }

    // ---- Rotation (Euler ZYX, degrees) -------------------------------------
    ImGui::SeparatorText("Rotation (Euler, deg)");
    {
        constexpr float kRad2Deg = 180.0F / std::numbers::pi_v<float>;
        constexpr float kDeg2Rad = std::numbers::pi_v<float> / 180.0F;
        static cd::math::Quatf pre_rot { 0.0F, 0.0F, 0.0F, 1.0F };
        static float           editor_euler[3] { 0.0F, 0.0F, 0.0F };
        static bool            editing = false;

        auto quat_to_euler = [](const cd::math::Quatf& q) {
            const float sx = 2.0F * (q.w * q.x + q.y * q.z);
            const float cx = 1.0F - 2.0F * (q.x * q.x + q.y * q.y);
            const float roll  = std::atan2(sx, cx);
            float sy = 2.0F * (q.w * q.y - q.z * q.x);
            sy = std::clamp(sy, -1.0F, 1.0F);
            const float pitch = std::asin(sy);
            const float sz = 2.0F * (q.w * q.z + q.x * q.y);
            const float cz = 1.0F - 2.0F * (q.y * q.y + q.z * q.z);
            const float yaw   = std::atan2(sz, cz);
            return std::array<float, 3> { roll, pitch, yaw };
        };
        auto euler_to_quat = [](float ex, float ey, float ez) {
            const float hx = ex * 0.5F;
            const float hy = ey * 0.5F;
            const float hz = ez * 0.5F;
            const float cx = std::cos(hx);
            const float sx = std::sin(hx);
            const float cy = std::cos(hy);
            const float sy = std::sin(hy);
            const float cz = std::cos(hz);
            const float szl = std::sin(hz);
            cd::math::Quatf q;
            q.w = cz * cy * cx + szl * sy * sx;
            q.x = cz * cy * sx - szl * sy * cx;
            q.y = cz * sy * cx + szl * cy * sx;
            q.z = szl * cy * cx - cz * sy * sx;
            return q;
        };

        if (!editing)
        {
            const auto e = quat_to_euler(lt->value.rotation);
            editor_euler[0] = e[0] * kRad2Deg;
            editor_euler[1] = e[1] * kRad2Deg;
            editor_euler[2] = e[2] * kRad2Deg;
        }
        ImGui::Text("X Y Z");
        ImGui::SameLine();
        const bool changed = ImGui::DragFloat3(
            "##insp_rot", editor_euler, 1.0F, -180.0F, 180.0F, "%.1f");
        if (ImGui::IsItemActivated())
        {
            pre_rot = lt->value.rotation;
            editing = true;
        }
        if (changed && editing)
            lt->value.rotation = euler_to_quat(
                editor_euler[0] * kDeg2Rad,
                editor_euler[1] * kDeg2Rad,
                editor_euler[2] * kDeg2Rad);
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            const cd::math::Quatf final_rot = lt->value.rotation;
            lt->value.rotation = pre_rot;
            history.push(std::make_unique<cd::editor::RotateCommand>(
                scene, target_, final_rot));
            editing = false;
        }
    }

    // ---- Rotation slider (Y-axis live demo) --------------------------------
    ImGui::SeparatorText("Rotate Y (live slider)");
    {
        // Reset the slider accumulator when the selection changes.
        if (rot_slider_entity.id != target_.id)
        {
            rot_slider_deg    = 0.0F;
            rot_slider_entity = target_;
        }

        static cd::math::Quatf pre_slider_rot { 0.0F, 0.0F, 0.0F, 1.0F };

        const bool changed = ImGui::SliderFloat(
            "##insp_rot_y", &rot_slider_deg, -180.0F, 180.0F, "%.1f deg");

        if (ImGui::IsItemActivated())
            pre_slider_rot = lt->value.rotation;

        if (changed)
        {
            const float half = rot_slider_deg * (std::numbers::pi_v<float> / 180.0F) * 0.5F;
            const cd::math::Quatf qy { 0.0F, std::sin(half), 0.0F, std::cos(half) };
            lt->value.rotation = qy;
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            const cd::math::Quatf final_rot = lt->value.rotation;
            lt->value.rotation = pre_slider_rot;
            history.push(std::make_unique<cd::editor::RotateCommand>(
                scene, target_, final_rot));
        }
    }

    ImGui::PopItemWidth();
    return true;
}

}  // namespace cd::editor::panel::inspector
