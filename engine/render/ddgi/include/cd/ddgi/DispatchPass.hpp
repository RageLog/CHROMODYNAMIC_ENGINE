// =============================================================================
// CHROMODYNAMIC — cd/ddgi/DispatchPass.hpp
// phase549 — GPU dispatch wiring for the DDGI trace compute shader.
// phase560 — Sprint-2: blend_irradiance + blend_visibility compute passes.
//
// DispatchPass owns the GPU resources that the kDdgiTraceCS compute shader
// reads / writes:
//
//   * ray_radiance image (RGBA16F, width = rays_per_probe, height = probe_count)
//     — populated by the trace shader; consumed by the blend passes (Sprint 2-3).
//   * ray_dir_dist  image (RG16F, same dims) — octahedral-encoded direction +
//     hit distance per ray.
//   * trace_ubo buffer — currently unused for binding (the shader sources its
//     parameters from push-constants), but allocated so future variants that
//     promote parameters to a UBO can keep the same DispatchPass surface.
//   * the descriptor-set layout + pipeline layout + compute pipeline produced
//     from kDdgiTraceCS via the engine's cd::shader compiler.
//
// Sprint-2 (phase560) additionally owns the GPU side of kDdgiBlendIrradianceCS
// and kDdgiBlendVisibilityCS:
//
//   * irradiance_atlas image (RGBA16F, dims from ProbeAtlas::init_from_grid).
//   * visibility_atlas  image (RG16F,   same dims).
//   * one descriptor-set layout / pipeline layout / compute pipeline /
//     descriptor set per blend variant — both consume ray_radiance +
//     ray_dir_dist from the trace pass and write into their own atlas.
//
// All resources are created against a single cd::rhi::IDevice, owned by the
// pass, and destroyed in shutdown(). The class never reaches into the engine
// sample code (`hello_engine`) — it is a self-contained library object that
// can be exercised by a standalone gtest binary.
//
// Sprint-1 scope:
//   * init()/shutdown() create + destroy every resource.
//   * dispatch() records the bind + push-constants + vkCmdDispatch sequence
//     into a caller-provided ICommandBuffer. The TLAS is bound through the
//     descriptor set — when the caller doesn't have a real one yet, init()
//     can be told to skip the TLAS binding (`needs_tlas = false`) and the
//     descriptor-set layout omits the slot, so the pass still compiles + runs
//     against a Vulkan device that has no ray-query support (validation-clean
//     smoke verification).
//
// Sprint-2 scope:
//   * execute_blend_irradiance(cmd, frame_index) dispatches kDdgiBlendIrradianceCS
//     — reads ray_radiance + ray_dir_dist, writes irradiance_atlas.
//   * execute_blend_visibility(cmd, frame_index) dispatches kDdgiBlendVisibilityCS
//     — reads ray_dir_dist, writes visibility_atlas.
//   The sample-from-atlas fragment pass (kDdgiSampleFS) is Sprint-3.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/Result.hpp>
#include <cd/ddgi/Ddgi.hpp>
#include <cd/rhi/Handles.hpp>

#include <cstdint>

namespace cd::rhi { class IDevice;        }
namespace cd::rhi { class ICommandBuffer; }

namespace cd::ddgi
{

/// Layout of the push-constant block consumed by kDdgiTraceCS. Matches the
/// `layout(push_constant) uniform PC { ... } pc;` declaration in the shader,
/// 1-to-1 — std430 packing rules apply.
struct alignas(16) TracePushConstants
{
    float        grid_origin[3];   float max_distance;
    float        grid_spacing[3];  float hysteresis;
    std::uint32_t probes_dim[4];     // xyz = count, w = rays_per_probe
    std::uint32_t probe_face_size;
    std::uint32_t frame_index;
    std::uint32_t _pad0;
    std::uint32_t _pad1;
    float        sky_color[3];     float _pad2;
};
static_assert(sizeof(TracePushConstants) == 80,
              "TracePushConstants must match kDdgiTraceCS push-constant block");

/// Layout of the push-constant block consumed by kDdgiBlendIrradianceCS. The
/// kDdgiBlendVisibilityCS PC block is structurally identical to this one (it
/// reuses the same field positions; the `_pad_or_max_distance` slot holds
/// `hysteresis`'s sibling float — `max_distance` for the visibility shader,
/// an explicit pad for the irradiance shader). std430 packing rules apply.
struct alignas(16) BlendPushConstants
{
    float        grid_origin[3];   float hysteresis;
    float        grid_spacing[3];  float pad_or_max_distance;
    std::uint32_t probes_dim[4];     // xyz = count, w = rays_per_probe
    std::uint32_t probe_face_size;
    std::uint32_t frame_index;
    std::uint32_t _pad0;
    std::uint32_t _pad1;
};
static_assert(sizeof(BlendPushConstants) == 64,
              "BlendPushConstants must match kDdgiBlend{Irradiance,Visibility}CS PC blocks");

/// Init parameters for DispatchPass.
struct DispatchPassDesc
{
    /// Probe grid layout — sizes the ray_radiance / ray_dir_dist images.
    ProbeGrid grid {};
    /// Trace settings — supplies rays_per_probe + hysteresis + max_distance.
    TraceSettings settings {};
    /// Sky-color fallback used on miss rays.
    float sky_color[3] { 0.2F, 0.25F, 0.4F };
    /// When true (default), the descriptor-set layout reserves binding=0 for
    /// the TLAS and dispatch() expects the caller to bind a real
    /// AccelStructureHandle via DispatchPass::bind_tlas() before the
    /// dispatch is recorded. When false, the layout omits the TLAS slot;
    /// the shader source is patched at compile time to skip the ray query.
    /// Used by unit tests on Vulkan devices that lack ray-query support.
    bool needs_tlas { true };
    /// Texels per probe face in the irradiance / visibility atlases. Powers
    /// of two (8 / 16) per Majercik 2019 §4. Sized into ProbeAtlas during
    /// init() and reflected by the blend-pass dispatch dimensions.
    std::uint32_t probe_face_size { 8 };
};

/// Owns the GPU side of the DDGI ray-trace pass.
class CD_NODISCARD DispatchPass
{
public:
    DispatchPass() noexcept = default;
    ~DispatchPass() = default;
    DispatchPass(const DispatchPass&) = delete;
    DispatchPass& operator=(const DispatchPass&) = delete;
    DispatchPass(DispatchPass&&) noexcept = default;
    DispatchPass& operator=(DispatchPass&&) noexcept = default;

    /// Create the descriptor-set layout + pipeline layout + compute pipeline +
    /// the two storage images. Returns kBackendInitFailed when the engine was
    /// built without glslang (the shader cannot be compiled), kCompileFailed
    /// when the shader source fails to compile, or kResourceCreationFailed
    /// when one of the underlying create_* calls returns an error.
    ///
    /// On failure every partial allocation is rolled back — shutdown() is
    /// safe to call but is a no-op.
    [[nodiscard]] cd::core::Result<void> init(cd::rhi::IDevice& device,
                                              const DispatchPassDesc& desc);

    /// Free every GPU resource owned by the pass. Safe to call on an
    /// uninitialised instance.
    void shutdown(cd::rhi::IDevice& device) noexcept;

    /// Update the descriptor-set TLAS binding. Required before dispatch()
    /// when `desc.needs_tlas == true`. The handle does NOT need to be valid
    /// if needs_tlas was false — the call becomes a no-op.
    [[nodiscard]] cd::core::Result<void>
    bind_tlas(cd::rhi::IDevice& device, cd::rhi::AccelStructureHandle tlas);

    /// Record the trace dispatch into `cmd`. The caller is responsible for
    /// having issued the matching image barriers (UNDEFINED → kUnorderedAccess)
    /// for ray_radiance() + ray_dir_dist() before this call.
    /// `cmd` must be inside a begin() / end() pair.
    void dispatch(cd::rhi::ICommandBuffer& cmd, std::uint32_t frame_index);

    /// Sprint-2 — record the irradiance-blend dispatch into `cmd`. Reads
    /// ray_radiance + ray_dir_dist (populated by the trace pass) and writes
    /// per-probe irradiance into the octahedral irradiance atlas. The
    /// caller is responsible for the matching image barriers
    /// (ray images: kUnorderedAccess → kUnorderedAccess + memory barrier,
    ///  irradiance_atlas: UNDEFINED / prior-state → kUnorderedAccess).
    void execute_blend_irradiance(cd::rhi::ICommandBuffer& cmd,
                                  std::uint32_t frame_index);

    /// Sprint-2 — record the visibility-blend dispatch into `cmd`. Reads
    /// ray_dir_dist (per-ray direction + distance from the trace pass) and
    /// writes (mean_depth, mean_depth²) into the visibility atlas for
    /// Chebyshev gating in the sample pass (Sprint-3).
    void execute_blend_visibility(cd::rhi::ICommandBuffer& cmd,
                                  std::uint32_t frame_index);

    // ---- Accessors --------------------------------------------------------

    [[nodiscard]] cd::rhi::TextureHandle      ray_radiance()      const noexcept { return ray_radiance_; }
    [[nodiscard]] cd::rhi::TextureHandle      ray_dir_dist()      const noexcept { return ray_dir_dist_; }
    [[nodiscard]] cd::rhi::TextureViewHandle  ray_radiance_view() const noexcept { return ray_radiance_view_; }
    [[nodiscard]] cd::rhi::TextureViewHandle  ray_dir_dist_view() const noexcept { return ray_dir_dist_view_; }
    [[nodiscard]] cd::rhi::ComputePipelineHandle pipeline()       const noexcept { return pipeline_; }
    [[nodiscard]] cd::rhi::PipelineLayoutHandle  pipeline_layout() const noexcept { return pipeline_layout_; }
    [[nodiscard]] cd::rhi::DescriptorSetHandle   descriptor_set() const noexcept { return descriptor_set_; }
    [[nodiscard]] cd::rhi::BufferHandle          trace_ubo()      const noexcept { return trace_ubo_; }

    // ---- Sprint-2 blend-pass accessors -----------------------------------
    [[nodiscard]] cd::rhi::TextureHandle      irradiance_atlas()      const noexcept { return irradiance_atlas_; }
    [[nodiscard]] cd::rhi::TextureHandle      visibility_atlas()      const noexcept { return visibility_atlas_; }
    [[nodiscard]] cd::rhi::TextureViewHandle  irradiance_atlas_view() const noexcept { return irradiance_atlas_view_; }
    [[nodiscard]] cd::rhi::TextureViewHandle  visibility_atlas_view() const noexcept { return visibility_atlas_view_; }
    [[nodiscard]] cd::rhi::ComputePipelineHandle blend_irradiance_pipeline()       const noexcept { return blend_irr_pipeline_; }
    [[nodiscard]] cd::rhi::ComputePipelineHandle blend_visibility_pipeline()       const noexcept { return blend_vis_pipeline_; }
    [[nodiscard]] cd::rhi::DescriptorSetHandle   blend_irradiance_descriptor_set() const noexcept { return blend_irr_descriptor_set_; }
    [[nodiscard]] cd::rhi::DescriptorSetHandle   blend_visibility_descriptor_set() const noexcept { return blend_vis_descriptor_set_; }

    [[nodiscard]] std::uint32_t ray_image_width()  const noexcept { return ray_image_width_; }
    [[nodiscard]] std::uint32_t ray_image_height() const noexcept { return ray_image_height_; }
    [[nodiscard]] std::uint32_t atlas_width()      const noexcept { return atlas_width_; }
    [[nodiscard]] std::uint32_t atlas_height()     const noexcept { return atlas_height_; }
    [[nodiscard]] std::uint32_t probe_face_size()  const noexcept { return probe_face_size_; }

    /// True iff init() succeeded and shutdown() has not been called since.
    [[nodiscard]] bool initialised() const noexcept { return pipeline_.is_valid(); }

    [[nodiscard]] bool needs_tlas() const noexcept { return needs_tlas_; }

private:
    // ── owned GPU resources — trace pass ───────────────────────────────────
    cd::rhi::ShaderModuleHandle        shader_module_      {};
    cd::rhi::DescriptorSetLayoutHandle set_layout_         {};
    cd::rhi::PipelineLayoutHandle      pipeline_layout_    {};
    cd::rhi::ComputePipelineHandle     pipeline_           {};
    cd::rhi::DescriptorSetHandle       descriptor_set_     {};
    cd::rhi::TextureHandle             ray_radiance_       {};
    cd::rhi::TextureHandle             ray_dir_dist_       {};
    cd::rhi::TextureViewHandle         ray_radiance_view_  {};
    cd::rhi::TextureViewHandle         ray_dir_dist_view_  {};
    cd::rhi::BufferHandle              trace_ubo_          {};

    // ── owned GPU resources — Sprint-2 blend passes ────────────────────────
    cd::rhi::ShaderModuleHandle        blend_irr_module_         {};
    cd::rhi::ShaderModuleHandle        blend_vis_module_         {};
    cd::rhi::DescriptorSetLayoutHandle blend_set_layout_         {};   // shared by both variants
    cd::rhi::PipelineLayoutHandle      blend_pipeline_layout_    {};   // shared by both variants
    cd::rhi::ComputePipelineHandle     blend_irr_pipeline_       {};
    cd::rhi::ComputePipelineHandle     blend_vis_pipeline_       {};
    cd::rhi::DescriptorSetHandle       blend_irr_descriptor_set_ {};
    cd::rhi::DescriptorSetHandle       blend_vis_descriptor_set_ {};
    cd::rhi::TextureHandle             irradiance_atlas_         {};
    cd::rhi::TextureHandle             visibility_atlas_         {};
    cd::rhi::TextureViewHandle         irradiance_atlas_view_    {};
    cd::rhi::TextureViewHandle         visibility_atlas_view_    {};

    // ── cached parameters ─────────────────────────────────────────────────
    ProbeGrid     grid_     {};
    TraceSettings settings_ {};
    float         sky_color_[3] { 0.0F, 0.0F, 0.0F };
    bool          needs_tlas_   { true };
    std::uint32_t ray_image_width_  { 0 };
    std::uint32_t ray_image_height_ { 0 };
    std::uint32_t probe_face_size_  { 0 };
    std::uint32_t atlas_width_      { 0 };
    std::uint32_t atlas_height_     { 0 };
};

}  // namespace cd::ddgi
