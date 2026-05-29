# cd::sample_framework (M2A skeleton)

Mega-Marathon Milestone M2 — sample-app lifecycle library. Inherit from
`cd::sample::App`, override `on_boot` / `on_frame` / `on_shutdown`, and
the library owns the rest of the lifecycle (window, device, swapchain,
ImGui, frame timing, shader hot-reload).

## Status

This is the **M2A skeleton** slice. It ships only the virtual interface
and a synthetic frame-driver loop suitable for tests. The real
window / RHI device / swapchain / IBL bake / ImGui Vulkan backend land
in subsequent sub-phases per
[`docs/ADR/ADR-20260529-M2-sample-framework.md`](../../../docs/ADR/ADR-20260529-M2-sample-framework.md):

| Sub-phase | Scope | Status |
|-----------|-------|--------|
| M2A | Library skeleton + virtual interface + bare-frame driver + 5 gtests | **DONE (this slice)** |
| M2B | Window + device + swapchain boot extracted from hello_engine | pending |
| M2C | Frame-loop + ImGui + shader watcher extracted from hello_engine | pending |
| M2D | hello_engine port to `: cd::sample::App` (main() body to ~300 lines) | pending |
| M2E | hello_engine_lite (~200-line validator) + ctest acceptance | pending |

## Minimal usage (M2A)

    #include <cd/sample/App.hpp>
    #include <cd/sample/run.hpp>

    class MyApp final : public cd::sample::App {
    public:
        using cd::sample::App::App;
    protected:
        cd::core::Result<void> on_boot() override        { return {}; }
        void on_frame(const cd::sample::FrameContext&) override { /* draw */ }
        void on_shutdown() noexcept override             { /* free */ }
    };

    int main(int argc, char** argv) {
        cd::sample::AppConfig cfg;
        cfg.title = "My Sample";
        cfg.max_frames = 0;  // M2C: 0 = run until window closes
        return cd::sample::run<MyApp>(argc, argv, cfg);
    }

## Why a library

Per the M2 ADR + memory (Run 16 architectural-wall log): hello_engine
main.cpp is 6161 lines / main() body 2143 lines. Future samples
(hello_pbr, hello_d3d12_pbr, hello_skinned_dq, ...) all need the same
boot scaffolding. Without this library, every new sample would carry a
1000+ line copy of the boot dance. With the library, a new sample is a
~200-line file describing only what to render.

## Dependencies (M2A)

- `cd::core` for `Result<T>` + `ErrorCode`

M2B/M2C will add `cd::rhi`, `cd::window`, `cd::shader`, `cd::ibl`,
`cd::frame_timing`, and the Vulkan / D3D12 backend libs of choice.

## Tests

`cd_test_sample_framework` exercises the lifecycle contract WITHOUT a
real RHI / window: boot/frame/shutdown order, multi-frame pump, early
shutdown via `request_shutdown`, boot-failure path, and the `run<App>`
template entry point.

Run:

    cmake --build --preset ninja-debug --target cd_test_sample_framework
    ctest --preset ninja-debug --output-on-failure -R sample_framework
