// =============================================================================
// CHROMODYNAMIC — cd/debug_draw/DebugDraw.hpp
// phase1057 — GPU half of the debug-line stack, promoted to a library.
//
// cd::debug_line (phase 1030) is the RHI-independent CPU batch; this
// library owns everything a consumer previously had to hand-roll
// (hello_engine phases 1031/1034 were the prototype):
//   * the kLineList pipeline (via cd::material::Material),
//   * a lazily-grown host-visible vertex buffer,
//   * the frames-in-flight-safe growth path (outgrown buffers are
//     PARKED and destroyed only after `frame_idx + kDestroyMargin`,
//     mirroring the TLAS-ring convention — destroying a buffer the
//     GPU may still read mid-frame is a use-after-free; see the
//     phase 1034 review finding),
//   * the once-per-frame flush (upload + bind + draw).
//
// Consumers append shapes into a LineBatch anywhere during the frame
// and call flush() LAST in their colour pass.
//
// Shader contract: the default embedded shaders write a single colour
// attachment. Consumers whose pass uses MRT (e.g. hello_engine's
// 4-target HDR pass) supply their own fragment source via
// RendererDesc::fragment_glsl so every attachment gets a defined
// value.
// =============================================================================
#pragma once

#include <cd/core/Result.hpp>
#include <cd/debug_line/DebugLine.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/shader/Compiler.hpp>

#include <cstdint>
#include <deque>
#include <span>
#include <string_view>

// Forward-declare the material to keep this header light; the .cpp
// owns the heavy include.
#include <cd/material/Material.hpp>

namespace cd::debug_draw
{

/// Pipeline-compatibility description. The colour/depth formats MUST
/// match the render pass the consumer flushes inside (same contract
/// as any cd::material::Material).
struct RendererDesc
{
    std::span<const cd::rhi::Format> color_attachment_formats {};
    cd::rhi::Format depth_attachment_format { cd::rhi::Format::kD32Float };
    /// Optional GLSL overrides. Empty = the library's embedded
    /// single-attachment shaders. Consumers with MRT passes supply a
    /// fragment shader writing every attachment (see header note).
    std::string_view vertex_glsl {};
    std::string_view fragment_glsl {};
    /// Optional on-disk hot-reload paths (ADR-20260529-X5 precedence:
    /// path wins over string when both are set).
    std::string_view vertex_glsl_path {};
    std::string_view fragment_glsl_path {};
    std::string_view name { "cd::debug_draw/line" };
};

/// Owns the GPU side of debug-line rendering. Move-only; destroy()
/// must run while the device is idle (same contract as Material).
class Renderer
{
public:
    /// Frames that must elapse before an outgrown vertex buffer is
    /// really destroyed (fif=2 fences only frame N-2; margin 3 is
    /// the engine-wide TLAS-ring convention).
    static constexpr std::uint32_t kDestroyMargin = 3;

    [[nodiscard]] static cd::core::Result<Renderer>
    create(cd::rhi::IDevice& device,
           cd::shader::ICompiler* compiler,
           const RendererDesc& desc);

    Renderer() noexcept = default;
    Renderer(Renderer&&) noexcept = default;
    Renderer& operator=(Renderer&&) noexcept = default;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    ~Renderer() = default;  // GPU handles released via destroy()

    /// Upload the batch and record the draw. No-op on an empty batch
    /// (the deferred-destroy queue still ticks). Call LAST in the
    /// colour pass so one upload covers every consumer that appended
    /// lines this frame. `frame_idx` drives the parked-buffer
    /// reclaim; pass the same monotonically increasing counter the
    /// frame loop uses elsewhere.
    void flush(cd::rhi::IDevice& device,
               cd::rhi::ICommandBuffer& cmd,
               const cd::debug_line::LineBatch& batch,
               const cd::math::Mat4f& view_proj,
               std::uint32_t frame_idx);

    /// Rebuild ONLY the pipeline from `desc` (hot-reload path: the
    /// on-disk GLSL changed). The vertex buffer and parked queue are
    /// untouched — they carry no shader state. On failure the OLD
    /// pipeline is kept and false returned, mirroring the engine's
    /// prim/shadow hot-reload contract so a broken shader edit never
    /// kills the running pipeline.
    [[nodiscard]] bool recreate_pipeline(cd::rhi::IDevice& device,
                                         cd::shader::ICompiler* compiler,
                                         const RendererDesc& desc);

    /// Release the pipeline + vertex buffer + every parked buffer.
    /// Device must be idle.
    void destroy(cd::rhi::IDevice& device) noexcept;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return material_.is_valid();
    }

    /// Observability for tests / stats panels.
    [[nodiscard]] std::uint64_t vertex_capacity_bytes() const noexcept
    {
        return vb_capacity_;
    }
    [[nodiscard]] std::size_t parked_buffer_count() const noexcept
    {
        return parked_.size();
    }

private:
    struct Parked
    {
        cd::rhi::BufferHandle h {};
        std::uint32_t destroy_at_frame { 0 };
    };

    cd::material::Material material_ {};
    cd::rhi::BufferHandle  vb_ {};
    std::uint64_t          vb_capacity_ { 0 };
    std::deque<Parked>     parked_;
};

}  // namespace cd::debug_draw
