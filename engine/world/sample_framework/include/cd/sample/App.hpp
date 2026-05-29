// =============================================================================
// CHROMODYNAMIC -- cd/sample/App.hpp
// Mega-Marathon M2A skeleton (Run 29 / phase373).
//
// Per ADR-20260529-M2-sample-framework: every sample inherits from
// cd::sample::App and overrides on_boot / on_frame / on_shutdown. The App
// owns the lifecycle (window, device, swapchain, IBL bake, ImGui, frame
// timing, shader hot-reload) so each sample writes a ~200-line main()
// instead of duplicating the 6000-line hello_engine main() body.
//
// This M2A slice ships ONLY the virtual interface + a no-op Impl that
// drives the on_boot -> on_frame -> on_shutdown handshake without
// allocating any RHI objects. M2B brings real window + device boot.
// =============================================================================
#pragma once

#include <cd/core/Result.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace cd::sample
{

/// Configures App lifecycle. M2A surface is intentionally narrow; M2B/M2C
/// extend with require_ray_query, require_acceleration_structure,
/// enable_imgui_dockspace, enable_shader_hot_reload, backend selector,
/// etc. (see ADR-20260529-M2 sketch).
struct AppConfig
{
    std::string title { "cd::sample" };
    std::uint32_t window_width { 1920 };
    std::uint32_t window_height { 1080 };
    bool start_maximized { false };
    bool enable_validation { true };
    /// M2A only: hard cap on frames the App will pump before
    /// on_shutdown is invoked. Tests use this to exercise the loop
    /// without a real swapchain. Set to 0 for "run until window
    /// closes" once M2C wires the real event loop. Default 1 keeps
    /// the M2A bare-frames gtest deterministic.
    std::uint64_t max_frames { 1u };
};

/// Per-frame data handed to App::on_frame.
///
/// M2A surface only carries the bare frame index + dt + total time;
/// M2B/M2C extend with command buffer + backbuffer handle + extent +
/// format. The structure layout MUST remain backwards compatible as
/// fields are appended -- existing samples should not have to recompile
/// when fields are added below `total_time`.
struct FrameContext
{
    float dt { 0.0f };
    double total_time { 0.0 };
    std::uint64_t frame_index { 0u };
};

/// Sample-app base class. Inherit + override the three pure virtuals.
/// Construct via the `run<MyApp>(argc, argv, cfg)` template entry, which
/// owns `App::run()` invocation + return code.
///
/// Lifecycle contract:
///   1. ctor (cheap, no IO)
///   2. on_boot   -- allocates resources. May fail (returns Result<void>).
///   3. on_frame  -- pumped once per frame until shutdown_requested() or
///                   max_frames cap hit (M2A) or window close (M2B+).
///   4. on_shutdown -- releases resources. Must be noexcept.
///   5. dtor
///
/// Move/copy disabled -- the App owns RHI handles that are not safely
/// duplicable.
class App
{
public:
    explicit App(AppConfig cfg) noexcept;
    virtual ~App();

    App(const App&)            = delete;
    App& operator=(const App&) = delete;
    App(App&&)                 = delete;
    App& operator=(App&&)      = delete;

    /// Drives the lifecycle. Returns 0 on graceful shutdown, non-zero
    /// when on_boot fails or on_frame requests an error exit.
    [[nodiscard]] int run();

    /// Read-only access to the config supplied to the constructor.
    [[nodiscard]] const AppConfig& config() const noexcept;

    /// Set from inside on_frame to break the loop early. Polled by the
    /// driver after every on_frame invocation.
    void request_shutdown() noexcept;

    /// True after `request_shutdown` was called or the M2A frame cap
    /// has been reached.
    [[nodiscard]] bool shutdown_requested() const noexcept;

protected:
    /// Allocate sample-owned resources. Called once before the first
    /// on_frame. Returning an error skips on_frame entirely and forces
    /// the App to call on_shutdown before run() returns non-zero.
    [[nodiscard]] virtual cd::core::Result<void> on_boot() = 0;

    /// Pumped once per frame. M2A drives with synthetic FrameContext
    /// (dt = 1/60, total_time advances, frame_index increments). M2B+
    /// fills cmd / backbuffer / extent from the real swapchain.
    virtual void on_frame(const FrameContext& fc) = 0;

    /// Release sample-owned resources. Must be noexcept; called from
    /// the run() epilogue and from the App destructor as a safety net.
    virtual void on_shutdown() noexcept = 0;

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

}  // namespace cd::sample
