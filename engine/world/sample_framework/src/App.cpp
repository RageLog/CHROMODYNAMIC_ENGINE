// =============================================================================
// CHROMODYNAMIC -- engine/world/sample_framework/src/App.cpp
// Mega-Marathon M2A skeleton (Run 29 / phase373).
// =============================================================================
#include <cd/sample/App.hpp>

#include <cd/core/Result.hpp>

#include <utility>

namespace cd::sample
{

// PIMPL holder. M2A keeps the surface trivial: just the config + the
// frame-driver state. M2B/M2C extend with window + device + swapchain +
// shader_watcher + ibl + imgui + frame_timing.
struct App::Impl
{
    AppConfig cfg;
    std::uint64_t frames_pumped { 0u };
    bool shutdown_requested { false };
    double total_time_sec { 0.0 };

    explicit Impl(AppConfig c) noexcept
        : cfg(std::move(c))
    {
    }
};

App::App(AppConfig cfg) noexcept
    : p_(std::make_unique<Impl>(std::move(cfg)))
{
}

App::~App() = default;

const AppConfig& App::config() const noexcept
{
    return p_->cfg;
}

void App::request_shutdown() noexcept
{
    p_->shutdown_requested = true;
}

bool App::shutdown_requested() const noexcept
{
    return p_->shutdown_requested;
}

int App::run()
{
    // Boot phase. Returning an error must skip on_frame and still
    // invoke on_shutdown to honor the lifecycle contract.
    if (auto r = on_boot(); !r.has_value())
    {
        on_shutdown();
        return 1;
    }

    // M2A bare-frame driver: pump synthetic FrameContext until either
    // (a) request_shutdown was called or (b) max_frames cap reached.
    // M2C replaces this body with the real window + swapchain event
    // loop. max_frames = 0 means "drive forever" (M2C semantics);
    // M2A treats 0 the same as 1 so the gtest path stays bounded.
    const std::uint64_t cap = (p_->cfg.max_frames == 0u) ? 1u : p_->cfg.max_frames;
    constexpr float kFixedDt = 1.0f / 60.0f;

    while (!p_->shutdown_requested && p_->frames_pumped < cap)
    {
        FrameContext fc;
        fc.dt           = kFixedDt;
        fc.frame_index  = p_->frames_pumped;
        fc.total_time   = p_->total_time_sec;

        on_frame(fc);

        ++p_->frames_pumped;
        p_->total_time_sec += static_cast<double>(kFixedDt);
    }

    on_shutdown();
    return 0;
}

}  // namespace cd::sample
