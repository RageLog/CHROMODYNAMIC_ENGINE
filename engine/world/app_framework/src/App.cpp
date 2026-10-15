// =============================================================================
// CHROMODYNAMIC -- engine/world/app_framework/src/App.cpp
// =============================================================================
#include <cd/app/App.hpp>

#include <cd/core/Result.hpp>

#include <utility>

namespace cd::app
{

struct App::Impl
{
    AppConfig cfg;
    std::uint64_t frames_pumped { 0u };
    bool shutdown_requested { false };
    bool has_run { false };          // double-init guard
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

std::uint64_t App::frames_pumped() const noexcept
{
    return p_->frames_pumped;
}

bool App::has_run() const noexcept
{
    return p_->has_run;
}

int App::run()
{
    if (p_->has_run)
    {
        return 1;
    }
    p_->has_run = true;

    if (auto r = on_boot(); !r.has_value())
    {
        on_shutdown();
        return 1;
    }

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

}  // namespace cd::app
