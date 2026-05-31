// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_inspector/src/Inspector.cpp
//
// phase543 — cd::editor::panel::inspector  implementation
// =============================================================================
#include <cd/editor/panel_inspector/Inspector.hpp>

#include <cd/editor/EditHistory.hpp>
#include <cd/editor/TransformCommands.hpp>
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
            const float hx = ex * 0.5F, hy = ey * 0.5F, hz = ez * 0.5F;
            const float cx = std::cos(hx), sx = std::sin(hx);
            const float cy = std::cos(hy), sy = std::sin(hy);
            const float cz = std::cos(hz), szl = std::sin(hz);
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
