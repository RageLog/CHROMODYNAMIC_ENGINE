// =============================================================================
// CHROMODYNAMIC PLAYER -- apps/player/ShowcaseRenderer3D.hpp
// Real Vulkan 1.3 Depth-Buffered 3D Cook-Torrance GGX PBR & Octopath-Style
// HD-2D Hybrid (3D Diorama + Depth-Tested Pixel-Art Sprite Cards) Renderer.
// =============================================================================
#pragma once

#include <cd/element_core/Element.hpp>
#include <cd/framegraph/Targets.hpp>
#include <cd/render/MeshUpload.hpp>
#include <cd/render2d/TextureAtlas2D.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>

#include <array>
#include <cstdint>

namespace cd::player
{

struct ShowcaseCamera3D
{
    float yaw_rad   { 0.48F };
    float pitch_rad { 0.34F };
    float distance  { 11.2F };
    float sim_time  { 0.0F };
};

class ShowcaseRenderer3D
{
public:
    ShowcaseRenderer3D() = default;
    ~ShowcaseRenderer3D() noexcept { destroy(); }

    ShowcaseRenderer3D(const ShowcaseRenderer3D&)            = delete;
    ShowcaseRenderer3D& operator=(const ShowcaseRenderer3D&) = delete;

    [[nodiscard]] bool init(
        cd::rhi::IDevice&                  device,
        cd::rhi::Format                    color_format,
        cd::rhi::Extent2D                  initial_extent,
        cd::rhi::TextureViewHandle         atlas_view,
        cd::rhi::SamplerHandle             atlas_sampler,
        const cd::render2d::TextureAtlas2D& cpu_atlas);

    void destroy() noexcept;

    [[nodiscard]] bool is_valid() const noexcept { return initialized_; }

    /// Records the 3D atmospheric sky, depth-buffered GGX PBR scene, and (when `hd2d_mode` is true)
    /// 3D depth-tested alpha-cutout pixel-art sprite cards into `swapchain_view`.
    [[nodiscard]] bool record(
        cd::rhi::ICommandBuffer&         cmd,
        cd::rhi::TextureViewHandle       swapchain_view,
        cd::rhi::Extent2D                extent,
        const cd::element_core::Stage&   stage,
        const ShowcaseCamera3D&          cam,
        bool                             hd2d_mode);

private:
    static constexpr std::size_t kUboRingSize = 4U;

    [[nodiscard]] bool ensure_depth_target(cd::rhi::Extent2D extent);

    cd::rhi::IDevice*            device_ { nullptr };
    cd::rhi::Format              color_format_ { cd::rhi::Format::kBGRA8Unorm };
    cd::render2d::TextureAtlas2D cpu_atlas_ {};
    bool                         initialized_ { false };

    // Depth attachment (D32Float)
    cd::framegraph::DepthTarget  depth_target_ {};
    bool                         depth_initialized_on_gpu_ { false };

    // Procedural 3D GPU meshes
    cd::render::GpuMesh          mesh_sphere_ {};
    cd::render::GpuMesh          mesh_cube_ {};
    cd::render::GpuMesh          mesh_cylinder_ {};
    cd::render::GpuMesh          mesh_torus_ {};
    cd::render::GpuMesh          mesh_cone_ {};
    cd::render::GpuMesh          mesh_quad_ {};

    // Per-frame UBO ring + descriptor sets
    cd::rhi::DescriptorSetLayoutHandle scene_dsl_ {};
    std::array<cd::rhi::BufferHandle, kUboRingSize>        ubo_ring_ {};
    std::array<cd::rhi::DescriptorSetHandle, kUboRingSize> scene_ds_ring_ {};
    std::uint32_t                ring_cursor_ { 0U };

    // Sky pipeline
    cd::rhi::ShaderModuleHandle     sky_vs_ {};
    cd::rhi::ShaderModuleHandle     sky_fs_ {};
    cd::rhi::PipelineLayoutHandle   sky_layout_ {};
    cd::rhi::GraphicsPipelineHandle sky_pipeline_ {};

    // 3D GGX PBR pipeline
    cd::rhi::ShaderModuleHandle     pbr_vs_ {};
    cd::rhi::ShaderModuleHandle     pbr_fs_ {};
    cd::rhi::PipelineLayoutHandle   pbr_layout_ {};
    cd::rhi::GraphicsPipelineHandle pbr_pipeline_ {};

    // HD-2D 3D Pixel-Art Sprite Card pipeline
    cd::rhi::ShaderModuleHandle     sprite_vs_ {};
    cd::rhi::ShaderModuleHandle     sprite_fs_ {};
    cd::rhi::PipelineLayoutHandle   sprite_layout_ {};
    cd::rhi::GraphicsPipelineHandle sprite_pipeline_ {};
};

}  // namespace cd::player
