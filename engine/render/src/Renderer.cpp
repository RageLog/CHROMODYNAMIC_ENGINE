// =============================================================================
// CHROMODYNAMIC — cd/render/Renderer.cpp
// =============================================================================
#include <cd/render/Renderer.hpp>
#include <cd/render/DrawBucket.hpp>
#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/ICommandBuffer.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

namespace cd::render
{

namespace
{

constexpr std::uint64_t kTimelineForever = std::numeric_limits<std::uint64_t>::max();

}  // namespace

// ---- Move/release helpers --------------------------------------------------

Renderer::~Renderer()
{
    release();
}

Renderer::Renderer(Renderer&& other) noexcept
{
    steal(std::move(other));
}

Renderer& Renderer::operator=(Renderer&& other) noexcept
{
    if (this != &other)
    {
        release();
        steal(std::move(other));
    }
    return *this;
}

void Renderer::release() noexcept
{
    if (device_ == nullptr)
        return;
    // Wait for every in-flight frame to drain before tearing down sync
    // primitives — otherwise the driver would reject destruction while a
    // submit is outstanding.
    device_->wait_idle();
    for (auto& f : frames_)
    {
        f.cmd.reset();
        if (f.fence.is_valid())
            device_->destroy_fence(f.fence);
        if (f.acquire_sem.is_valid())
            device_->destroy_semaphore(f.acquire_sem);
        if (f.present_sem.is_valid())
            device_->destroy_semaphore(f.present_sem);
    }
    frames_.clear();
    if (swapchain_.is_valid())
        device_->destroy_swapchain(swapchain_);
    swapchain_ = {};
    device_ = nullptr;
    frame_counter_ = 0;
    in_progress_ = false;
}

void Renderer::steal(Renderer&& other) noexcept
{
    desc_ = other.desc_;
    device_ = other.device_;
    swapchain_ = other.swapchain_;
    frames_ = std::move(other.frames_);
    frame_counter_ = other.frame_counter_;
    in_progress_ = other.in_progress_;
    current_image_index_ = other.current_image_index_;
    other.device_ = nullptr;
    other.swapchain_ = {};
    other.frame_counter_ = 0;
    other.in_progress_ = false;
}

// ---- Factory ---------------------------------------------------------------

cd::core::Result<Renderer> Renderer::create(const RendererDesc& desc)
{
    if (desc.device == nullptr)
    {
        return std::unexpected(
            render_errors::make(render_errors::Code::kInvalidArgument, "Renderer::create: device is null")
        );
    }
    if (desc.frames_in_flight == 0 || desc.frames_in_flight > 8)
    {
        return std::unexpected(
            render_errors::make(
                render_errors::Code::kInvalidArgument,
                "Renderer::create: frames_in_flight must be in [1,8]"
            )
        );
    }
    if (desc.swapchain.extent.width == 0 || desc.swapchain.extent.height == 0)
    {
        return std::unexpected(
            render_errors::make(render_errors::Code::kInvalidArgument, "Renderer::create: swapchain extent is zero")
        );
    }

    Renderer r;
    r.desc_ = desc;
    r.device_ = desc.device;

    auto sc = r.device_->create_swapchain(desc.swapchain);
    if (!sc.has_value())
    {
        return std::unexpected(render_errors::wrap(render_errors::Code::kSwapchainCreateFailed, sc.error()));
    }
    r.swapchain_ = *sc;

    // Per-frame sync ring. We start every fence in the SIGNALED state so the
    // first begin_frame() can wait on it without blocking forever — there is
    // no prior submit to wait on.
    r.frames_.resize(desc.frames_in_flight);
    for (auto& f : r.frames_)
    {
        auto a = r.device_->create_semaphore();
        if (!a.has_value())
        {
            r.release();
            return std::unexpected(render_errors::wrap(render_errors::Code::kSyncCreateFailed, a.error()));
        }
        f.acquire_sem = *a;
        auto p = r.device_->create_semaphore();
        if (!p.has_value())
        {
            r.release();
            return std::unexpected(render_errors::wrap(render_errors::Code::kSyncCreateFailed, p.error()));
        }
        f.present_sem = *p;
        auto fence = r.device_->create_fence(/*signaled=*/true);
        if (!fence.has_value())
        {
            r.release();
            return std::unexpected(render_errors::wrap(render_errors::Code::kSyncCreateFailed, fence.error()));
        }
        f.fence = *fence;
        f.fence_initialized = true;
        f.cmd = r.device_->create_command_buffer(cd::rhi::QueueType::kGraphics);
        if (f.cmd == nullptr)
        {
            r.release();
            return std::unexpected(
                render_errors::make(
                    render_errors::Code::kSyncCreateFailed,
                    "Renderer::create: create_command_buffer returned null"
                )
            );
        }
    }

    return r;
}

// ---- Frame begin -----------------------------------------------------------

cd::core::Result<FrameContext> Renderer::begin_frame()
{
    if (device_ == nullptr)
    {
        return std::unexpected(
            render_errors::make(render_errors::Code::kInvalidArgument, "Renderer is not initialized")
        );
    }
    if (in_progress_)
    {
        return std::unexpected(
            render_errors::make(render_errors::Code::kFrameInFlight, "begin_frame called without matching end_frame")
        );
    }
    const auto slot = static_cast<std::uint32_t>(frame_counter_ % desc_.frames_in_flight);
    auto& f = frames_[slot];

    // Block until the GPU finished the previous use of this slot. This
    // guarantees the command buffer is reusable and the sync primitives are
    // available again.
    auto w = device_->wait_for_fence(f.fence, kTimelineForever);
    if (!w.has_value())
    {
        return std::unexpected(render_errors::wrap(render_errors::Code::kDeviceError, w.error()));
    }
    device_->reset_fence(f.fence);

    // Acquire next swapchain image. The acquire_sem will be signaled when the
    // image is renderable; we feed it into submit() as a wait below.
    auto idx = device_->acquire_next_image(swapchain_, f.acquire_sem, cd::rhi::FenceHandle {}, kTimelineForever);
    if (!idx.has_value())
    {
        // The RHI surfaces kSwapchainOutOfDate when the surface is resized; we
        // translate to our own domain so callers can branch on render_errors.
        if (idx.error().code == static_cast<std::uint32_t>(cd::rhi::rhi_errors::Code::kSwapchainOutOfDate))
        {
            return std::unexpected(render_errors::wrap(render_errors::Code::kSwapchainOutOfDate, idx.error()));
        }
        return std::unexpected(render_errors::wrap(render_errors::Code::kDeviceError, idx.error()));
    }
    current_image_index_ = *idx;

    const auto sc_image = device_->swapchain_image(swapchain_, current_image_index_);
    const auto sc_view = device_->swapchain_image_view(swapchain_, current_image_index_);

    // Begin command buffer + record the implicit UNDEFINED→COLOR_ATTACHMENT
    // barrier so the caller's begin_render_pass is in-spec without their
    // having to think about layout transitions.
    f.cmd->begin();
    std::array<cd::rhi::TextureBarrier, 1> tb {
        cd::rhi::TextureBarrier {
                                 .texture = sc_image,
                                 .from = cd::rhi::ResourceState::kUndefined,
                                 .to = cd::rhi::ResourceState::kColorAttachment,
                                 .range = { .base_mip = 0, .mip_count = 1, .base_layer = 0, .layer_count = 1 },
                                 }
    };
    f.cmd->barrier({}, tb);

    FrameContext ctx {};
    ctx.command_buffer = f.cmd.get();
    ctx.swapchain_image = sc_image;
    ctx.swapchain_image_view = sc_view;
    ctx.extent = desc_.swapchain.extent;
    ctx.frame_index = frame_counter_;
    ctx.in_flight_index = slot;
    in_progress_ = true;
    return ctx;
}

// ---- Frame end -------------------------------------------------------------

cd::core::Result<void> Renderer::end_frame()
{
    if (!in_progress_)
    {
        return std::unexpected(
            render_errors::make(render_errors::Code::kFrameInFlight, "end_frame called without matching begin_frame")
        );
    }
    const auto slot = static_cast<std::uint32_t>(frame_counter_ % desc_.frames_in_flight);
    auto& f = frames_[slot];

    // COLOR_ATTACHMENT → PRESENT_SRC so the present engine is allowed to
    // consume the image.
    const auto sc_image = device_->swapchain_image(swapchain_, current_image_index_);
    std::array<cd::rhi::TextureBarrier, 1> tb {
        cd::rhi::TextureBarrier {
                                 .texture = sc_image,
                                 .from = cd::rhi::ResourceState::kColorAttachment,
                                 .to = cd::rhi::ResourceState::kPresent,
                                 .range = { .base_mip = 0, .mip_count = 1, .base_layer = 0, .layer_count = 1 },
                                 }
    };
    f.cmd->barrier({}, tb);
    f.cmd->end();

    std::array<cd::rhi::ICommandBuffer*, 1> cbs { f.cmd.get() };
    std::array<cd::rhi::SemaphoreSubmit, 1> waits { cd::rhi::SemaphoreSubmit { .semaphore = f.acquire_sem } };
    std::array<cd::rhi::SemaphoreSubmit, 1> sigs { cd::rhi::SemaphoreSubmit { .semaphore = f.present_sem } };
    cd::rhi::SubmitDesc sd {};
    sd.command_buffers = cbs;
    sd.wait_semaphores = waits;
    sd.signal_semaphores = sigs;
    sd.signal_fence = f.fence;
    auto sr = device_->submit(sd);
    if (!sr.has_value())
    {
        in_progress_ = false;
        ++frame_counter_;
        return std::unexpected(render_errors::wrap(render_errors::Code::kDeviceError, sr.error()));
    }

    std::array<cd::rhi::SemaphoreHandle, 1> present_waits { f.present_sem };
    auto pr = device_->present(swapchain_, current_image_index_, present_waits);
    in_progress_ = false;
    ++frame_counter_;
    if (!pr.has_value())
    {
        if (pr.error().code == static_cast<std::uint32_t>(cd::rhi::rhi_errors::Code::kSwapchainOutOfDate))
        {
            return std::unexpected(render_errors::wrap(render_errors::Code::kSwapchainOutOfDate, pr.error()));
        }
        return std::unexpected(render_errors::wrap(render_errors::Code::kDeviceError, pr.error()));
    }
    return {};
}

// ---- Phase 149 — DrawBucket bridge ----------------------------------------

cd::core::Result<void> Renderer::submit_draws(DrawBucket& bucket)
{
    if (!in_progress_)
    {
        return std::unexpected(render_errors::make(
            render_errors::Code::kFrameInFlight,
            "submit_draws requires an active begin_frame"
        ));
    }
    const auto slot = static_cast<std::uint32_t>(frame_counter_ % desc_.frames_in_flight);
    auto& cmd = *frames_[slot].cmd;
    bucket.emit_all(cmd);
    return {};
}

void Renderer::wait_idle()
{
    if (device_ != nullptr)
        device_->wait_idle();
}

cd::core::Result<void> Renderer::recreate_swapchain(cd::rhi::Extent2D new_extent)
{
    if (device_ == nullptr)
    {
        return std::unexpected(render_errors::make(render_errors::Code::kInvalidArgument, "Renderer not initialized"));
    }
    // Minimized windows report (0, 0). The driver would reject the create,
    // and there's nothing to render anyway — defer.
    if (new_extent.width == 0 || new_extent.height == 0)
    {
        return std::unexpected(
            render_errors::make(
                render_errors::Code::kInvalidArgument,
                "recreate_swapchain: zero extent (window minimized?)"
            )
        );
    }
    // Drain in-flight frames so the driver isn't using the old swapchain
    // or its image views while we tear them down.
    device_->wait_idle();

    // Best-effort: destroy old swapchain, then create the new one. If the
    // new create fails we leave the Renderer in a broken state (no
    // swapchain); callers retry the recreate next frame.
    if (swapchain_.is_valid())
    {
        device_->destroy_swapchain(swapchain_);
        swapchain_ = {};
    }
    desc_.swapchain.extent = new_extent;
    auto sc = device_->create_swapchain(desc_.swapchain);
    if (!sc.has_value())
    {
        return std::unexpected(render_errors::wrap(render_errors::Code::kSwapchainCreateFailed, sc.error()));
    }
    swapchain_ = *sc;
    in_progress_ = false;  // no in-flight frame after wait_idle
    // The PerFrame ring stays the same — fences/semaphores/cmd buffers
    // are agnostic of the swapchain.
    return {};
}

}  // namespace cd::render
