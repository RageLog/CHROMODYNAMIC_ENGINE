# ADR-20260529-M2-sample-framework

## Status (Run 29 / phase373 update)

ACCEPTED 2026-05-29 (original date stamp). **M2A LANDED** in Mega-Marathon Run 29 / phase373 (commit f892a2b -> phase373). Library skeleton + virtual interface + 5 lifecycle-contract gtests all green at 104/104 PASS. M2B (device boot) / M2C (frame loop + ImGui + shader watcher) / M2D (hello_engine port) / M2E (hello_engine_lite + acceptance test) REMAIN PENDING per the Implementation plan section below (5-7 days of focused engineering work). See docs/MARATHON_RUN29_MEGA_A_B.md for the full Run-level audit of why M2B was not attempted in Run 29.

---

Date: 2026-05-29 (Mega-Marathon Milestone M2 design pass; landed in the Run 18 mega-marathon orchestrator session, implementation deferred to a future Run with code-edit capability).

## Context

After the Run 9-16 extraction marathons, samples/engine/hello_engine/main.cpp is 6188 lines with a 2143-line main() body. The extraction pulled out 30+ aggregate structs and 50+ anon-namespace helpers, but the lifecycle scaffolding still lives inline in main():

- Window creation + lifecycle (glfwInit, swapchain, surface)
- Vulkan device + queue selection + extension probing
- Command pool / buffer creation
- IBL bake helpers (sky cubemap, irradiance prefilter, BRDF LUT)
- Render-target factory (HDR + G-Buffer + bloom chain + composite target)
- ImGui dockspace + DearImGui Vulkan backend init / teardown
- Frame timing (delta time, smoothed FPS, frame index)
- Main frame loop scaffolding (poll events, begin frame, swap, present)

Every future sample (hello_pbr, hello_d3d12_pbr planned in M4, hello_skinned_dq when M3-part-2 lands dual-quat skinning, etc.) needs the same scaffolding. Duplicating it 4x means every sample is a 1000+ line file. The user brief is explicit: future samples can write a 200-line main() inheriting from cd::sample::App.

The Run 16 close-out memory entry already flagged this: main() body to under 300 lines per the architectural assessment.

## Decision

Introduce a new engine library cd::sample::sample_framework at engine/world/sample_framework/. This library OWNS the lifecycle so samples only describe WHAT to render.

### Public API (sketch)

    namespace cd::sample {

    struct AppConfig {
        std::string_view title;
        std::uint32_t window_width  { 1920 };
        std::uint32_t window_height { 1080 };
        bool start_maximized { false };
        bool enable_validation { true };
        bool require_ray_query { false };
        bool require_acceleration_structure { false };
        bool enable_imgui_dockspace { true };
        bool enable_shader_hot_reload { true };
        cd::rhi::Backend backend { cd::rhi::Backend::kVulkan };
    };

    class App {
    public:
        explicit App(AppConfig cfg) noexcept;
        virtual ~App();
        App(const App&) = delete;
        App& operator=(const App&) = delete;

        [[nodiscard]] int run();

    protected:
        [[nodiscard]] virtual cd::core::Result<void> on_boot() = 0;
        virtual void on_frame(const FrameContext& fc) = 0;
        virtual void on_shutdown() noexcept = 0;

        [[nodiscard]] cd::rhi::IDevice& device() noexcept;
        [[nodiscard]] cd::shader::ICompiler& shader_compiler() noexcept;
        [[nodiscard]] cd::ibl::IblBundle& ibl() noexcept;
        [[nodiscard]] cd::frame_timing::Timing& timing() noexcept;
        [[nodiscard]] cd::window::IWindow& window() noexcept;
        [[nodiscard]] cd::shader::FileWatcher& shader_watcher() noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> p_;
    };

    struct FrameContext {
        cd::rhi::ICommandBuffer& cmd;
        cd::rhi::SwapchainImageHandle backbuffer;
        cd::rhi::Format backbuffer_format;
        cd::rhi::Extent2D backbuffer_extent;
        float dt;
        std::uint64_t frame_index;
        double total_time;
    };

    template <typename T, typename... Args>
    [[nodiscard]] int run(Args&&... args) {
        T app { std::forward<Args>(args)... };
        return app.run();
    }

    } // namespace cd::sample

### What App owns (Impl PIMPL)

- cd::window::IWindow (glfw or custom backend)
- cd::rhi::IDevice + selected queue + command pool
- cd::rhi::Swapchain + per-frame image acquire / present fence ring
- cd::shader::ICompiler (glslang)
- cd::shader::FileWatcher (M1 / X5 integration, registered with sample shaders during on_boot)
- cd::ibl::IblBundle (sky cubemap + irradiance + BRDF LUT, lazily baked on first ibl() access)
- cd::frame_timing::Timing (smoothed dt + FPS counter)
- ImGui state (Vulkan backend + dockspace flag handling)
- Main loop driver (poll events, begin_frame, on_frame, end_frame, shader_watcher.poll_and_reload, swap + present)

### Secondary sample for validation

samples/engine/hello_engine_lite/main.cpp -- a deliberately minimal 200-line sample that draws a single rotating textured cube + sun. Purpose: prove the API surface is small enough that a new contributor can write a sample without copying hello_engine.

## Consequences

- New library engine/world/sample_framework/ with its own CMakeLists.txt + tests/. Public include path cd/sample/App.hpp.
- hello_engine becomes a 200-300 line main.cpp plus the unchanged Hello*.hpp aggregate files (which become methods on HelloEngine or remain free functions called from on_boot).
- All other samples (asset/hello_*, render/hello_*) gain a migration path. They migrate opportunistically, not as part of M2 -- M2 only validates the API with hello_engine + hello_engine_lite. Migrations land in subsequent Runs as needed.
- The shader-hot-reload integration (M1 / X5) becomes a single boolean (AppConfig::enable_shader_hot_reload) instead of every sample wiring its own FileWatcher.
- The library lives in world/ not render/ because it bundles window + input + ImGui + frame timing alongside rendering; the dependency DAG has it depending on every render/ + ui/ + input/ library above it.

## Rejected alternatives

- Static functions in a cd::sample namespace, no class. Rejected -- the lifecycle has too much state for free functions to be clean.
- Inheritance-free callbacks (std::function on_frame). Rejected -- type-erased callbacks lose access to derived-class member fields.
- One library per service. Rejected as too fine-grained -- the whole point is single bundle.
- CRTP / static-polymorphism. Rejected -- compile-time overhead, hard error messages, no clean entry point.
- Embedded ECS world / scene tree as a default. Rejected for V1 -- some samples (hello_triangle, hello_quad) do not want ECS overhead.

## Implementation plan (for the future Run that picks this up)

Recommended subdivision into 5 sub-phases of one Run each:

1. M2A -- new library skeleton (CMakeLists.txt, public/include, src/, tests/, empty App class with virtual hooks that no-op).
2. M2B -- App::Impl with window + device + swapchain boot extracted from hello_engine main() lines 1-400 of the current main().
3. M2C -- App::Impl with frame loop + ImGui + shader watcher extracted from hello_engine main() lines 2000-2143 of the current main().
4. M2D -- hello_engine port: derive HelloEngine : cd::sample::App, slice the remaining 1700 lines of main() into on_boot / on_frame / on_shutdown.
5. M2E -- hello_engine_lite (200-line validator) + ctest target + acceptance test for pre/post pixel-identical hello_engine output.

Estimated 5-7 days of focused engineering work.

## Reference

- samples/engine/hello_engine/main.cpp -- current 6188-line monolith being deconstructed.
- engine/world/ -- existing peer libraries (anim/, audio/, ecs/, input/, scene/) showing the namespace + folder convention.
- engine/render/shader/include/cd/shader/FileWatcher.hpp -- M1 dependency.
- docs/ADR/ADR-20260529-X5-shader-on-disk-hot-reload.md -- M1 / X5 ADR that this design depends on for the shader_watcher integration.

## Demir Kural status

Pure engineering ADR. No academic citations required. Industry-state reference: Filament Engine + View + Renderer class triangle, bgfx event loop convention, Bevy App plugin chain, Unreal UEngine + UWorld separation -- all are surveyed in the future Run pre-implementation researcher sweep (engineering SOTA, not academic).
