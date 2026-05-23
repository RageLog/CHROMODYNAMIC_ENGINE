// =============================================================================
// CHROMODYNAMIC — cd/render/Renderer.hpp
// Sprint S3.9 — high-level per-frame renderer.
//
// `Renderer` is the orchestration layer the engine exposes to applications.
// It owns:
//   * a swapchain bound to a platform window,
//   * `frames_in_flight` rings of per-frame sync primitives (acquire/present
//     semaphores + completion fence + command buffer),
//   * the boilerplate that turns "I want to draw a frame" into the Vulkan
//     `acquire → submit → present` sequence with correct image-layout
//     transitions.
//
// Frame lifecycle (caller-visible):
//
//   auto frame = renderer.begin_frame();    // image acquired, cmd buffer
//                                            // begun, swapchain image is
//                                            // already in COLOR_ATTACHMENT
//                                            // layout
//   // ... record begin_render_pass / draw / etc. on frame->command_buffer
//   renderer.end_frame();                   // COLOR → PRESENT barrier,
//                                            // submit, present
//
// The renderer never reaches into application state; it gives back a
// `FrameContext` and trusts the caller to record into the command buffer.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Format.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/IDevice.hpp>

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace cd::rhi
{
class ICommandBuffer;
}

namespace cd::render
{

// ---- Error domain -----------------------------------------------------------

namespace render_errors
{
inline constexpr std::uint32_t kDomain = 0x000C;

enum class Code : std::uint32_t
{
    kOk = 0,
    kInvalidArgument = 1,
    kSwapchainCreateFailed = 2,
    kSyncCreateFailed = 3,
    kFrameInFlight = 4,       ///< begin_frame called without matching end_frame.
    kSwapchainOutOfDate = 5,  ///< Driver requested swapchain recreate.
    kDeviceError = 6,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}

/// Re-tag an upstream ErrorCode with render_errors::Code, preserving
/// owned diagnostic storage. See material_errors::wrap for the same
/// pattern — both helpers exist so cross-library error chaining keeps
/// the upstream's diagnostic text intact instead of leaving a dangling
/// `string_view`.
[[nodiscard]] inline cd::core::ErrorCode wrap(Code c, const cd::core::ErrorCode& upstream) noexcept
{
    return cd::core::ErrorCode::rewrap(kDomain, static_cast<std::uint32_t>(c), upstream);
}
}  // namespace render_errors

// ---- Description -----------------------------------------------------------

struct RendererDesc
{
    /// Device that backs the renderer. Must outlive the Renderer.
    cd::rhi::IDevice* device { nullptr };
    /// Swapchain configuration. Renderer creates and owns the swapchain.
    cd::rhi::SwapchainDesc swapchain {};
    /// Number of frames recorded ahead of GPU completion. 2 is the SOTA
    /// default; 1 disables parallelism, 3+ adds latency without throughput
    /// gain past triple buffering.
    std::uint32_t frames_in_flight { 2 };
};

// ---- Per-frame context -----------------------------------------------------

/// Snapshot of the frame the caller is currently recording. Non-owning view
/// into Renderer state; valid only between begin_frame() and end_frame().
struct FrameContext
{
    cd::rhi::ICommandBuffer* command_buffer { nullptr };
    /// Swapchain image as a TextureHandle (already transitioned to
    /// COLOR_ATTACHMENT_OPTIMAL by begin_frame).
    cd::rhi::TextureHandle swapchain_image {};
    /// Pre-built default image view for the swapchain image.
    cd::rhi::TextureViewHandle swapchain_image_view {};
    /// Render-area dimensions matching the swapchain extent.
    cd::rhi::Extent2D extent {};
    /// Frame counter; monotonically increasing across begin_frame() calls.
    std::uint64_t frame_index { 0 };
    /// Slot in the frames_in_flight ring (0..frames_in_flight-1).
    std::uint32_t in_flight_index { 0 };
};

// ---- Renderer ---------------------------------------------------------------

class Renderer
{
public:
    [[nodiscard]] static cd::core::Result<Renderer> create(const RendererDesc& desc);

    Renderer() noexcept = default;
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&& other) noexcept;
    Renderer& operator=(Renderer&& other) noexcept;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return device_ != nullptr;
    }

    [[nodiscard]] cd::rhi::SwapchainHandle swapchain() const noexcept
    {
        return swapchain_;
    }

    [[nodiscard]] cd::rhi::Format swapchain_format() const noexcept
    {
        return desc_.swapchain.format;
    }

    [[nodiscard]] cd::rhi::Extent2D swapchain_extent() const noexcept
    {
        return desc_.swapchain.extent;
    }

    [[nodiscard]] std::uint32_t frames_in_flight() const noexcept
    {
        return desc_.frames_in_flight;
    }

    /// Start recording a new frame. On success the returned FrameContext
    /// holds a command buffer that has been begin()'d and a swapchain image
    /// already transitioned to COLOR_ATTACHMENT_OPTIMAL. Calling begin_frame
    /// twice without an end_frame returns kFrameInFlight.
    [[nodiscard]] cd::core::Result<FrameContext> begin_frame();

    /// Finalize the in-flight frame: insert the COLOR→PRESENT barrier on the
    /// swapchain image, end() the command buffer, submit, and present.
    [[nodiscard]] cd::core::Result<void> end_frame();

    /// Block until every queued frame has completed. Call before destruction
    /// (the destructor calls it automatically) and before any swapchain
    /// re-creation.
    void wait_idle();

    /// Tear down + rebuild the swapchain at `new_extent`. Call when
    /// begin_frame() / end_frame() returns kSwapchainOutOfDate, or
    /// proactively on a platform-level window resize. The per-frame sync
    /// ring (semaphores, fences, command buffers) is preserved — only the
    /// swapchain + image views are recreated, so the call is cheap.
    ///
    /// On failure the Renderer is left in a usable state pinned to the
    /// previous swapchain; callers can retry once the underlying issue
    /// (zero-size minimized window, no compatible surface format) clears.
    [[nodiscard]] cd::core::Result<void> recreate_swapchain(cd::rhi::Extent2D new_extent);

private:
    void release_() noexcept;
    void steal_(Renderer&& other) noexcept;

    RendererDesc desc_ {};
    cd::rhi::IDevice* device_ { nullptr };
    cd::rhi::SwapchainHandle swapchain_ {};

    struct PerFrame
    {
        cd::rhi::SemaphoreHandle acquire_sem {};
        cd::rhi::SemaphoreHandle present_sem {};
        cd::rhi::FenceHandle fence {};
        std::unique_ptr<cd::rhi::ICommandBuffer> cmd {};
        bool fence_initialized { false };
    };

    std::vector<PerFrame> frames_ {};

    std::uint64_t frame_counter_ { 0 };
    bool in_progress_ { false };
    std::uint32_t current_image_index_ { 0 };
};

}  // namespace cd::render
