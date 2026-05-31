// CHROMODYNAMIC Web Platform — Emscripten Main Loop Stub
// Phase 1 Design: Minimal proof-of-concept for browser integration
// Status: Stub (pre-integration T4.5)
//
// Usage:
//   Emscripten cross-compile: emcxx main_web.cpp -o output.html -O2 -s WASM=1
//   Output: output.wasm, output.js, output.html (with Canvas + WebGPU detection)
//
// Architecture:
//   - emscripten_set_main_loop() wraps per-frame tick callback
//   - WebGPU initialization deferred to frame 0 (canvas context ready)
//   - ImGui integration placeholder for future UI
//
// Dependencies (post-T2.7):
//   - cd::rhi::webgpu (WebGPU backend, T2.7 implementation)
//   - cd::platform (Window, EventPump abstractions)
//   - cd::core (log, handle, memory foundation)
//   - Emscripten browser.h + SDL2 (optional window)

#include <memory>
#include <cstdio>

// Emscripten API
#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
    #include <emscripten/html5.h>
#else
    // Stub for non-Emscripten build (compile-time check only)
    #define emscripten_set_main_loop(fn, fps, sim) do {} while (0)
    #define emscripten_cancel_main_loop() do {} while (0)
#endif

// Forward declarations (placeholder; real types from cd::* namespaces post-T4.5)
namespace cd::web {

    // Stub: Opaque WebGPU context (real type from cd::rhi::webgpu after T2.7)
    struct WebGpuContext {
        // Placeholder fields
        // Post-integration (T4.5): contains cd::rhi::webgpu::Device, swapchain surface, etc.
    };

    // Stub: Frame loop state
    struct FrameLoopState {
        bool initialized = false;
        bool should_exit = false;
        WebGpuContext gpu_ctx {};
        // Post-integration: ImGui state, input state, frame counter, perf timers
    };

    // Global frame loop instance
    // TODO (T4.5): Replace with explicit context object (no global state per CLAUDE.md §7)
    static FrameLoopState* g_frame_loop = nullptr;

    // ===== Emscripten Main Loop Callback =====
    // Called once per frame (or per vsync if fps=0 in emscripten_set_main_loop)
    void tick_callback() {
        if (!g_frame_loop) {
            return;  // Not initialized
        }

        if (g_frame_loop->should_exit) {
            emscripten_cancel_main_loop();
            return;
        }

        // Frame 0: Initialize GPU context
        if (!g_frame_loop->initialized) {
            // TODO (T4.5): Call cd::rhi::webgpu::initialize_context(...)
            // Polling WebGPU device readiness; on success set initialized=true
            // For now, assume immediate success
            g_frame_loop->initialized = true;
            fprintf(stderr, "[web] GPU context initialized\n");
            return;
        }

        // Per-frame work
        // TODO (T4.5):
        //   - Poll input from Emscripten event handlers
        //   - Tick application logic
        //   - cd::rhi::core::CommandList encoder
        //   - cd::rhi::graph frame builder
        //   - Present to WebGPU swapchain
        //   - ImGui render overlay

        // Stub frame work
        static int frame_count = 0;
        if (++frame_count % 60 == 0) {
            fprintf(stderr, "[web] Frame %d\n", frame_count);
        }
    }

    // ===== Canvas and WebGPU Initialization (deferred to first tick) =====
    // Return non-zero if WebGPU unavailable (fallback to WebGL2 / error state)
    int query_webgpu_support() {
#ifdef __EMSCRIPTEN__
        // Post-T4.5: Use emscripten_webgpu_get_device() or custom JS check
        // For stub: return 0 (assume available; real check delegates to navigator.gpu)
        fprintf(stderr, "[web] WebGPU support check (stub)\n");
        return 0;
#else
        return -1;  // Not a web build
#endif
    }

    // ===== Emscripten HTML5 Event Handlers (placeholder) =====
    // Post-T4.5: wire to cd::platform::EventPump
    EM_BOOL canvas_resize_callback(
        int event_type,
        const EmscriptenUiEvent* ui_event,
        void* user_data
    ) {
        // Stub: trigger swapchain resize on next frame
        fprintf(stderr, "[web] Canvas resize: %dx%d\n", ui_event->documentBody.clientWidth, ui_event->documentBody.clientHeight);
        return EM_TRUE;
    }

    EM_BOOL keydown_callback(int event_type, const EmscriptenKeyboardEvent* key_event, void* user_data) {
        // Stub: queue input event to cd::platform::EventPump
        if (key_event->key[0] == 27) {  // ESC
            if (g_frame_loop) g_frame_loop->should_exit = true;
        }
        fprintf(stderr, "[web] Key: %s (code: %s)\n", key_event->key, key_event->code);
        return EM_TRUE;
    }

}  // namespace cd::web

// ===== C++ Entry Point =====
int main(int argc, char* argv[]) {
    fprintf(stderr, "[web] CHROMODYNAMIC Web Platform (Emscripten stub, Phase 1)\n");
    fprintf(stderr, "[web] Build: %s %s\n", __DATE__, __TIME__);

#ifdef __EMSCRIPTEN__
    fprintf(stderr, "[web] Emscripten environment detected\n");

    // Check WebGPU support
    int webgpu_support = cd::web::query_webgpu_support();
    if (webgpu_support != 0) {
        fprintf(stderr, "[web] WARNING: WebGPU not available; fallback to WebGL2 (Phase 2+)\n");
    }

    // Initialize frame loop state
    cd::web::g_frame_loop = new cd::web::FrameLoopState();

    // Register Emscripten event handlers
    // Post-T4.5: Connect to platform::Window canvas and SDL2 event dispatch
    emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, cd::web::canvas_resize_callback);
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, EM_TRUE, cd::web::keydown_callback);

    // Start main loop
    // Parameters: callback, fps (0 = requestAnimationFrame), simulate_infinite_loop (1 = block exit)
    fprintf(stderr, "[web] Starting main loop (requestAnimationFrame mode)\n");
    emscripten_set_main_loop(cd::web::tick_callback, 0, 1);

    // emscripten_set_main_loop never returns (unless emscripten_cancel_main_loop called)
    fprintf(stderr, "[web] Main loop exited\n");

    delete cd::web::g_frame_loop;
    cd::web::g_frame_loop = nullptr;

#else
    // Desktop build: stub main
    fprintf(stderr, "[web] Desktop build (non-Emscripten) — main_web.cpp is WASM-target only\n");
    fprintf(stderr, "[web] If building for desktop, use engine/samples/hello_engine/main.cpp instead\n");
    return 1;
#endif

    return 0;
}
