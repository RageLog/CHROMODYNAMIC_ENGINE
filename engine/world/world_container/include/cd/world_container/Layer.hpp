// =============================================================================
// CHROMODYNAMIC — cd/world_container/Layer.hpp
// gap #18 foundation — Layer = entity grouping inside a Level with
// visibility / lock / colour-tag / draw-order + optional postfx
// override.
//
// Layer doesn't own entities; the ECS does. The Layer carries a
// stable id (string name) that entities reference, plus the
// per-layer metadata the editor + renderer consume.
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace cd::world_container
{

/// Per-layer postfx selectors override the Project defaults. Empty
/// = "inherit from project". Future expansion: per-layer tonemap,
/// per-layer ambient occlusion strength, etc.
struct LayerPostfx
{
    bool override_bloom { false };
    bool enable_bloom   { false };
    bool override_gtao  { false };
    bool enable_gtao    { false };
    bool override_ssr   { false };
    bool enable_ssr     { false };
};

class Layer
{
public:
    Layer() = default;
    explicit Layer(std::string name) : name_(std::move(name)) {}

    [[nodiscard]] std::string_view name() const noexcept { return name_; }
    void set_name(std::string n) { name_ = std::move(n); }

    [[nodiscard]] bool visible() const noexcept { return visible_; }
    void set_visible(bool v) noexcept { visible_ = v; }

    [[nodiscard]] bool locked() const noexcept { return locked_; }
    void set_locked(bool v) noexcept { locked_ = v; }

    [[nodiscard]] cd::math::Vec3f color_tag() const noexcept { return color_tag_; }
    void set_color_tag(cd::math::Vec3f c) noexcept { color_tag_ = c; }

    [[nodiscard]] std::int32_t draw_order() const noexcept { return draw_order_; }
    void set_draw_order(std::int32_t o) noexcept { draw_order_ = o; }

    /// Persistent layers stay resident across all level chunks
    /// (player, UI, persistent NPCs). v1.7 streaming consumes this.
    [[nodiscard]] bool persistent() const noexcept { return persistent_; }
    void set_persistent(bool p) noexcept { persistent_ = p; }

    [[nodiscard]] LayerPostfx&       postfx()       noexcept { return postfx_; }
    [[nodiscard]] const LayerPostfx& postfx() const noexcept { return postfx_; }

private:
    std::string     name_       { "Default" };
    bool            visible_    { true };
    bool            locked_     { false };
    bool            persistent_ { false };
    cd::math::Vec3f color_tag_  { 0.7F, 0.7F, 0.75F };
    std::int32_t    draw_order_ { 0 };
    LayerPostfx     postfx_     {};
};

}  // namespace cd::world_container
