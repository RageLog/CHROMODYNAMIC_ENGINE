// =============================================================================
// CHROMODYNAMIC — cd/ui/renderer_webgpu/Submitter.hpp
//
// Phase 5 of ADR-20260530-ui-widget-library (phase522).
// Bridges cd::ui::renderer::DrawBatcher to a WebGPU device (Dawn).
//
// Integration status (build-time, set by CMake):
//
//   CD_UI_WEBGPU_HAVE_DAWN == 1  — real Dawn headers present; handles are
//                                   wgpu::Device, wgpu::CommandEncoder, etc.
//   CD_UI_WEBGPU_HAVE_DAWN == 0  — pure stub; all handles are opaque
//                                   integers; dispatch is a no-op.
//
// Public API (identical in both modes — callers see the same header):
//
//   Once at boot:   Submitter::create(device, info)
//                   -> allocates vertex/index GPU buffers, records the
//                      pipeline state (when Dawn is present).
//
//   Per frame:      submitter.upload(batcher);
//                   submitter.record(encoder, extent);
//
//   At shutdown:    submitter.destroy()  (or let dtor fire).
//
// Dawn is pulled via vcpkg feature "webgpu" (see vcpkg.json).  The Dawn
// port exists in the vcpkg baseline (version 20251202.213730) but is NOT
// installed by default because it requires Abseil + Python tooling and
// adds significant build time.  To enable the real backend:
//
//   vcpkg install chromodynamic[webgpu]     # installs Dawn
//   cmake --preset ninja-debug -DCD_ENABLE_WEBGPU=ON
//
// Without those steps CMake falls back to the stub implementation in
// src/Submitter.cpp, which compiles and passes all four build-only tests
// without a physical GPU or Dawn library.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/Result.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>

#include <cstdint>
#include <memory>

// ---------------------------------------------------------------------------
// Dawn / WebGPU handle types
// ---------------------------------------------------------------------------
#if CD_UI_WEBGPU_HAVE_DAWN
#  include <webgpu/webgpu_cpp.h>

namespace cd::ui::renderer_webgpu
{
    using WgpuDevice          = wgpu::Device;
    using WgpuCommandEncoder  = wgpu::CommandEncoder;
}  // namespace cd::ui::renderer_webgpu

#else  // stub types — no Dawn headers required

namespace cd::ui::renderer_webgpu
{
    /// Opaque stub handle for wgpu::Device.  In stub mode the underlying
    /// value is always 0 (null).  When Dawn is present this alias resolves
    /// to wgpu::Device.
    struct WgpuDevice
    {
        std::uintptr_t opaque { 0U };
        [[nodiscard]] bool operator==(const WgpuDevice&) const noexcept = default;
    };

    /// Opaque stub handle for wgpu::CommandEncoder.
    struct WgpuCommandEncoder
    {
        std::uintptr_t opaque { 0U };
        [[nodiscard]] bool operator==(const WgpuCommandEncoder&) const noexcept = default;
    };
}  // namespace cd::ui::renderer_webgpu

#endif  // CD_UI_WEBGPU_HAVE_DAWN

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
namespace cd::ui::renderer_webgpu
{

/// 2-D extent (pixels) — matches cd::rhi::Extent2D shape without pulling in
/// the RHI headers.
struct Extent2D
{
    std::uint32_t width  { 0U };
    std::uint32_t height { 0U };
};

/// Configuration for Submitter::create.
struct SubmitterCreateInfo
{
    /// WebGPU texture format enum value used for the color attachment.
    /// The default (0x12 = WGPUTextureFormat_BGRA8Unorm) is the common
    /// surface format on Windows/macOS.  Pass the correct value from
    /// wgpu::TextureFormat when Dawn is available.
    std::uint32_t color_format { 0x12U };

    /// Maximum vertex + index counts per frame (same semantics as the RHI
    /// submitter).  A GPU vertex buffer of
    /// (max_vertices * sizeof(cd::ui::renderer::Vertex)) is allocated at
    /// create-time via wgpu::Device::CreateBuffer (or stub no-op).
    std::uint32_t max_vertices { 65535U };
    std::uint32_t max_indices  { 65535U * 6U };

    /// Optional debug label propagated to WebGPU object labels so capture
    /// tools (RenderDoc, PIX, Chrome WebGPU inspector) show readable names.
    const char* debug_label { "cd_ui_webgpu" };
};

/// Submitter — owning, move-only handle for the WebGPU UI pipeline + ring
/// vertex/index buffers.  Construct via `create`.  The object is valid for
/// upload()/record() between create() and destroy().
class Submitter
{
public:
    Submitter() = default;
    ~Submitter();

    Submitter(const Submitter&) = delete;
    Submitter& operator=(const Submitter&) = delete;
    Submitter(Submitter&&) noexcept;
    Submitter& operator=(Submitter&&) noexcept;

    // ---- Factory -----------------------------------------------------------

    /// Create a new Submitter.  Returns the object or an error code on
    /// allocation failure (e.g. zero max_vertices).
    ///
    /// In stub mode: always succeeds and returns a valid (no-op) handle.
    /// In Dawn mode: calls wgpu::Device::CreateBuffer for vb + ib, then
    ///               compiles the UI vertex + fragment shader modules.
    [[nodiscard]] static cd::core::Result<Submitter>
    create(WgpuDevice device, const SubmitterCreateInfo& info);

    // ---- Per-frame ---------------------------------------------------------

    /// Upload the batcher's vertex + index spans to the GPU ring buffers.
    ///
    /// Stub:  copies metadata only (vertex_count / index_count), no GPU call.
    /// Dawn:  calls wgpu::Queue::WriteBuffer for both vb and ib.
    ///
    /// Returns false if the batcher exceeds max_vertices / max_indices.
    [[nodiscard]] bool upload(const cd::ui::renderer::DrawBatcher& batcher);

    /// Record UI draw commands into the caller's command encoder.
    ///
    /// Must be called while a render pass is active on `encoder` (the caller
    /// manages render-pass begin/end — WebGPU encodes differently from
    /// Vulkan's ICommandBuffer).
    ///
    /// Stub:  iterates DrawCommands and counts them; no GPU calls.
    /// Dawn:  sets viewport/scissor and issues DrawIndexed per DrawCommand.
    void record(WgpuCommandEncoder encoder, Extent2D viewport_extent) const;

    // ---- Lifecycle ---------------------------------------------------------

    /// Release GPU resources.  Idempotent.  Called automatically on
    /// destruction.
    void destroy() noexcept;

    // ---- State queries -----------------------------------------------------

    /// True after a successful create() and before destroy().
    [[nodiscard]] bool is_valid() const noexcept;

    /// Number of vertices / indices from the most recent upload() call.
    [[nodiscard]] std::uint32_t vertex_count()  const noexcept;
    [[nodiscard]] std::uint32_t index_count()   const noexcept;

    /// Number of DrawCommands recorded in the most recent upload() call.
    [[nodiscard]] std::uint32_t command_count() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace cd::ui::renderer_webgpu
