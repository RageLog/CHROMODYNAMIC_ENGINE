// =============================================================================
// CHROMODYNAMIC — cd/ddgi/DispatchPass.hpp
// phase549 — GPU dispatch wiring for the DDGI trace compute shader.
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

    // ---- Accessors --------------------------------------------------------

    [[nodiscard]] cd::rhi::TextureHandle      ray_radiance()      const noexcept { return ray_radiance_; }
    [[nodiscard]] cd::rhi::TextureHandle      ray_dir_dist()      const noexcept { return ray_dir_dist_; }
    [[nodiscard]] cd::rhi::TextureViewHandle  ray_radiance_view() const noexcept { return ray_radiance_view_; }
    [[nodiscard]] cd::rhi::TextureViewHandle  ray_dir_dist_view() const noexcept { return ray_dir_dist_view_; }
    [[nodiscard]] cd::rhi::ComputePipelineHandle pipeline()       const noexcept { return pipeline_; }
    [[nodiscard]] cd::rhi::PipelineLayoutHandle  pipeline_layout() const noexcept { return pipeline_layout_; }
    [[nodiscard]] cd::rhi::DescriptorSetHandle   descriptor_set() const noexcept { return descriptor_set_; }
    [[nodiscard]] cd::rhi::BufferHandle          trace_ubo()      const noexcept { return trace_ubo_; }

    [[nodiscard]] std::uint32_t ray_image_width()  const noexcept { return ray_image_width_; }
    [[nodiscard]] std::uint32_t ray_image_height() const noexcept { return ray_image_height_; }

    /// True iff init() succeeded and shutdown() has not been called since.
    [[nodiscard]] bool initialised() const noexcept { return pipeline_.is_valid(); }

    [[nodiscard]] bool needs_tlas() const noexcept { return needs_tlas_; }

private:
    // ── owned GPU resources ────────────────────────────────────────────────
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

    // ── cached parameters ─────────────────────────────────────────────────
    ProbeGrid     grid_     {};
    TraceSettings settings_ {};
    float         sky_color_[3] { 0.0F, 0.0F, 0.0F };
    bool          needs_tlas_   { true };
    std::uint32_t ray_image_width_  { 0 };
    std::uint32_t ray_image_height_ { 0 };
};

}  // namespace cd::ddgi
