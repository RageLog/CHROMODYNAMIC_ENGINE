// =============================================================================
// CHROMODYNAMIC — cd/scene_renderer/SceneRenderer.hpp
// FAZ 36 Wave 36.1 & 36.2 — Unified 3D SceneRenderer & StageMode (Pure2D / HD2D / Pure3D)
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Vector.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/scene_renderer/SurfaceAndPostFxPipeline.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace cd::scene_renderer
{

/// Engine stage orchestration presentation mode.
enum class StageMode : std::uint8_t
{
    Pure2D = 0,
    HD2D = 1,
    Pure3D = 2,
};

[[nodiscard]] constexpr std::string_view stage_mode_to_string(StageMode mode) noexcept
{
    switch (mode)
    {
    case StageMode::Pure2D: return "Pure2D";
    case StageMode::HD2D:   return "HD2D";
    case StageMode::Pure3D: return "Pure3D";
    }
    return "Pure3D";
}

[[nodiscard]] constexpr StageMode parse_stage_mode(std::string_view str) noexcept
{
    if (str == "Pure2D" || str == "pure2d" || str == "2d" || str == "2D")
    {
        return StageMode::Pure2D;
    }
    if (str == "HD2D" || str == "hd2d" || str == "hd-2d" || str == "HD-2D")
    {
        return StageMode::HD2D;
    }
    return StageMode::Pure3D;
}

struct SceneDirectionalLight
{
    cd::math::Vec3f direction { 0.0F, -1.0F, 0.0F };
    cd::math::Vec3f color { 1.0F, 1.0F, 1.0F };
    float intensity { 1.0F };
    float ambient { 0.2F };
    bool cast_shadows { true };
};

struct ScenePointLight
{
    cd::math::Vec3f position { 0.0F, 0.0F, 0.0F };
    cd::math::Vec3f color { 1.0F, 1.0F, 1.0F };
    float intensity { 100.0F };
    float radius { 10.0F };
};

struct SceneMeshInstance
{
    cd::rhi::BufferHandle vertex_buffer {};
    cd::rhi::BufferHandle index_buffer {};
    cd::rhi::IndexType index_type { cd::rhi::IndexType::kUInt32 };
    std::uint32_t index_count { 0 };
    std::uint32_t first_index { 0 };
    std::int32_t vertex_offset { 0 };
    cd::math::Mat4f model_matrix { cd::math::Mat4f::identity() };
    cd::math::Mat4f prev_model_matrix { cd::math::Mat4f::identity() };
    cd::math::Vec4f tint { 1.0F, 1.0F, 1.0F, 1.0F };
    float metallic { 0.0F };
    float roughness { 0.5F };
    cd::rhi::TextureViewHandle albedo_view {};
    cd::rhi::TextureViewHandle normal_view {};
    cd::rhi::TextureViewHandle metallic_roughness_view {};
    bool is_pbr { true };
    bool cast_shadows { true };
    float alpha_cutoff { 0.0F };
};

struct SceneSpriteCardHD2D
{
    cd::math::Vec3f position { 0.0F, 0.0F, 0.0F };
    cd::math::Vec2f size { 1.0F, 1.0F };
    cd::math::Vec4f uv_rect { 0.0F, 0.0F, 1.0F, 1.0F };
    cd::math::Vec4f tint { 1.0F, 1.0F, 1.0F, 1.0F };
    cd::rhi::BufferHandle vertex_buffer {};
    cd::rhi::BufferHandle index_buffer {};
    cd::rhi::TextureViewHandle texture_view {};
    bool y_locked { true };
    float alpha_cutoff { 0.5F };
    bool cast_shadows { true };
};

struct SceneViewData
{
    StageMode stage_mode { StageMode::Pure3D };
    cd::math::Mat4f view_matrix { cd::math::Mat4f::identity() };
    cd::math::Mat4f proj_matrix { cd::math::Mat4f::identity() };
    cd::math::Mat4f prev_view_proj { cd::math::Mat4f::identity() };
    cd::math::Vec3f camera_pos { 0.0F, 0.0F, 0.0F };
    cd::math::Vec3f camera_target { 0.0F, 0.0F, 0.0F };
    float near_z { 0.1F };
    float far_z { 1000.0F };
    float fov_y { 1.04719755F };
    float aspect { 16.0F / 9.0F };
    float focus_distance_dof { 10.0F };
    SceneDirectionalLight sun {};
    std::vector<ScenePointLight> point_lights {};
    std::vector<SceneMeshInstance> meshes {};
    std::vector<SceneSpriteCardHD2D> sprite_cards {};
};

/// Push constant payload for Level 3 analytical atmosphere/sky shader.
struct AnalyticalSkyPush
{
    float cam_right[4] { 1.0F, 0.0F, 0.0F, 1.0F };
    float cam_up[4]    { 0.0F, 1.0F, 0.0F, 1.0F };
    float cam_fwd[4]   { 0.0F, 0.0F, -1.0F, 0.0F };
    float sun_dir[4]   { 0.0F, -1.0F, 0.0F, 1.0F };
    float sun_color[4] { 1.0F, 1.0F, 1.0F, 1.0F };
};

struct SkyPassParams
{
    cd::math::Vec3f camera_eye { 0.0F, 0.0F, 0.0F };
    cd::math::Vec3f camera_target { 0.0F, 0.0F, -1.0F };
    float fov_y { 1.04719755F };
    float aspect { 16.0F / 9.0F };
    cd::math::Vec3f sun_dir { 0.0F, -1.0F, 0.0F };
    cd::math::Vec3f sun_color { 1.0F, 1.0F, 1.0F };
    float sun_intensity { 1.0F };
    bool has_sun { true };
};

/// Build analytical sky push constants from camera and sun parameters.
[[nodiscard]] AnalyticalSkyPush build_analytical_sky_push(const SkyPassParams& params) noexcept;

/// Record analytical sky pass into a draw recorder with a valid pipeline layout.
void record_analytical_sky(
    cd::rhi::IDrawRecorder& cmd,
    cd::rhi::PipelineLayoutHandle layout,
    const SkyPassParams& params
);

/// Record a single scene mesh instance into a draw recorder with a valid pipeline layout.
void record_scene_mesh_instance(
    cd::rhi::IDrawRecorder& cmd,
    cd::rhi::PipelineLayoutHandle layout,
    const SceneMeshInstance& mesh,
    const cd::math::Mat4f& view_proj
);

}  // namespace cd::scene_renderer
