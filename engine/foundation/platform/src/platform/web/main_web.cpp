// =============================================================================
// CHROMODYNAMIC — platform/web/main_web.cpp
// Phase 773 — Emscripten C++ entry point: Canvas + WebGPU + main loop.
//
// This is the WASM entry point for the CHROMODYNAMIC web platform.
//
// Lifecycle:
//   1. main() — detect WebGPU, create WebWindow + WebInputBridge, install
//      the Emscripten requestAnimationFrame loop via emscripten_set_main_loop.
//   2. tick() — called every frame by the browser event loop:
//        a. pump_events() → translate Emscripten input → OSEvent queue
//        b. (Sprint-2+) cd::rhi::webgpu frame encode + present
//        c. should_close → cancel loop
//   3. Cleanup on loop exit (emscripten_cancel_main_loop).
//
// MOMENT:
//   "Web port has a runtime starting point — Canvas + WebGPU + emscripten
//    main loop."  (FINALE-7 W2C Sprint-1)
//
// Dependencies (Sprint-1 scaffolding; real impls land in subsequent sprints):
//   - cd::platform::web::WebWindow   (Canvas IWindow wrapper)
//   - cd::platform::web::WebInputBridge (input event translator)
//   - cd::rhi::webgpu (Sprint-2; stubbed here)
// =============================================================================
#include <cd/platform/web/WebWindow.hpp>
#include <cd/platform/web/WebInputBridge.hpp>
#include <cd/platform/Window.hpp>
#include <cd/core/Log.hpp>

#include <cstdio>
#include <memory>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#else
// Desktop stub: suppress link errors so the TU compiles on CI.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define emscripten_set_main_loop(fn, fps, sim) do {} while(false)
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define emscripten_cancel_main_loop() do {} while(false)
#endif

// ---------------------------------------------------------------------------
// Frame loop context
// ---------------------------------------------------------------------------
namespace
{

/// Holds all frame-persistent state for the main loop.
struct WebFrameContext
{
    std::unique_ptr<cd::platform::web::WebWindow>       window;
    std::unique_ptr<cd::platform::web::WebInputBridge>  input;

    std::vector<cd::platform::OSEvent> event_scratch;
    std::uint64_t frame_index { 0 };

    /// Sprint-2+: WebGPU device handle lives here.
    // cd::rhi::webgpu::Device gpu_device;

    bool initialized { false };
};

/// Global context — acceptable for the single-threaded Emscripten main loop.
/// Per CLAUDE.md §7: would be replaced by explicit context pass in a
/// multi-window / worker-thread scenario.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
WebFrameContext* g_ctx = nullptr;

// ---------------------------------------------------------------------------
// Emscripten tick callback — called once per browser animation frame.
// ---------------------------------------------------------------------------
void tick() noexcept
{
    if (g_ctx == nullptr) {
        emscripten_cancel_main_loop();
        return;
    }

    // --- Frame-0 deferred init (GPU context ready after first rAF) ----------
    if (!g_ctx->initialized) {
        // Sprint-2+: acquire cd::rhi::webgpu::Device via
        //   emscripten_webgpu_get_device() + wgpuDeviceReference().
        // For Sprint-1 scaffolding we log readiness and proceed.
        CD_LOG(Info) << "[web] Frame 0: GPU context ready (Sprint-1 stub)";
        g_ctx->initialized = true;
        return;
    }

    // --- Input pump ---------------------------------------------------------
    g_ctx->event_scratch.clear();
    const bool alive = g_ctx->window->pump_events(g_ctx->event_scratch);

    // Process events (Sprint-1: logging only; Sprint-2+ feeds into InputContext).
    for (const auto& ev : g_ctx->event_scratch) {
        using K = cd::platform::OSEventKind;
        if (ev.kind == K::kResize) {
            CD_LOG(Info) << "[web] Canvas resized: "
                         << g_ctx->window->width() << 'x' << g_ctx->window->height();
            // Sprint-2+: notify RHI swapchain resize.
        }
    }

    if (!alive || g_ctx->window->should_close()) {
        CD_LOG(Info) << "[web] Loop exit requested at frame " << g_ctx->frame_index;
        emscripten_cancel_main_loop();
        return;
    }

    // --- Render (Sprint-2+) -------------------------------------------------
    // Sprint-1 stub: signal browser that a frame completed.
    ++g_ctx->frame_index;
    if (g_ctx->frame_index % 60 == 0) {
        CD_LOG(Info) << "[web] Heartbeat — frame " << g_ctx->frame_index;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// C++ entry point
// ---------------------------------------------------------------------------

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[])
{
    CD_LOG(Info) << "[web] CHROMODYNAMIC Web Platform — Sprint-1 (Phase 773)";
    CD_LOG(Info) << "[web] Built: " << __DATE__ << " " << __TIME__;

#ifndef __EMSCRIPTEN__
    // Desktop CI: just print the message and exit cleanly.
    CD_LOG(Info) << "[web] Desktop build — main_web is WASM-only. Exiting.";
    return 0;
#else
    // --- WebGPU availability check ------------------------------------------
    // Sprint-1: JS-side check in the HTML shell handles navigator.gpu.
    // C++-side we trust the browser; a Sprint-2 check uses the Emscripten
    // wgpu_native bindings once linked.
    CD_LOG(Info) << "[web] WebGPU check: deferred to browser (see shell JS)";

    // --- Create WebWindow ---------------------------------------------------
    cd::platform::web::WebWindowDesc wdesc;
    wdesc.canvas_selector = "#canvas";
    wdesc.width  = 1280;
    wdesc.height = 720;
    wdesc.title  = "CHROMODYNAMIC";

    auto win_result = cd::platform::web::WebWindow::create(wdesc);
    if (!win_result) {
        CD_LOG(Error) << "[web] Failed to create WebWindow: "
                      << win_result.error().message();
        return 1;
    }

    // --- Create input bridge ------------------------------------------------
    auto input = std::make_unique<cd::platform::web::WebInputBridge>();
    input->attach(win_result.value().get());

    // --- Install frame loop context ------------------------------------------
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    g_ctx = new WebFrameContext {
        .window      = std::move(win_result.value()),
        .input       = std::move(input),
        .frame_index = 0,
        .initialized = false,
    };

    CD_LOG(Info) << "[web] Starting requestAnimationFrame loop...";

    // fps=0  → browser controls frame rate (requestAnimationFrame).
    // sim=1  → emscripten_set_main_loop blocks until loop exits (WASM semantics).
    emscripten_set_main_loop(tick, 0, 1);

    // --- Cleanup (reached when emscripten_cancel_main_loop() is called) -----
    CD_LOG(Info) << "[web] Main loop exited. Cleaning up.";

    if (g_ctx != nullptr) {
        g_ctx->input->detach();
        delete g_ctx;  // NOLINT(cppcoreguidelines-owning-memory)
        g_ctx = nullptr;
    }

    return 0;
#endif  // __EMSCRIPTEN__
}
