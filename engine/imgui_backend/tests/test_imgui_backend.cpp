// =============================================================================
// CHROMODYNAMIC -- cd::imgui_backend host-side tests (band4 singletons).
//
// The imgui_backend ships a real Vulkan ImGui backend whose draw path is
// device-gated (verified end-to-end by the renderer + the rhi pixel-parity
// capstone). These tests cover everything that is testable WITHOUT a live
// Vulkan device:
//
//   * detail::color_from_name_hash -- pure FNV-1a -> ImU32 (deterministic).
//   * profiler_flamegraph          -- the data/formatting + branch logic,
//                                     driven through a HOST-ONLY ImGui context
//                                     (ImGui core is CPU-only; only
//                                     ImGui_ImplVulkan_* needs a GPU, which we
//                                     never call here).
//   * Context::create              -- the null-argument validation branch that
//                                     returns kInvalidArgument before any
//                                     device handle is touched.
//
// The live GPU-rendering verification (ImGui_ImplVulkan_RenderDrawData) is
// device-gated, exactly like the rhi Metal tests, and is sealed as
// promote-on-need in ADR-20260616-band4-singletons-scope §imgui_backend.
// =============================================================================
#include <cd/imgui/Context.hpp>
#include <cd/imgui/ProfilerView.hpp>
#include <cd/profile/Scope.hpp>

#include <imgui.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace
{

// ---------------------------------------------------------------------------
// RAII helper: a HEADLESS ImGui context with a software font atlas and a
// minimal frame. No platform/renderer backend is initialised, so this runs
// on any host with no GPU. profiler_flamegraph only calls ImGui core
// (draw list, layout, tooltip) which is pure CPU.
// ---------------------------------------------------------------------------
class HeadlessImGuiFrame
{
public:
    HeadlessImGuiFrame() : ctx_{ ImGui::CreateContext() }
    {
        IMGUI_CHECKVERSION();
        ImGui::SetCurrentContext(ctx_);
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2 { 800.0F, 600.0F };
        io.DeltaTime = 1.0F / 60.0F;
        // Build a software font atlas so text/layout calls have metrics.
        unsigned char* pixels = nullptr;
        int w = 0;
        int h = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);  // forces atlas build
        ImGui::NewFrame();
    }

    ~HeadlessImGuiFrame()
    {
        ImGui::EndFrame();
        ImGui::DestroyContext(ctx_);
    }

    HeadlessImGuiFrame(const HeadlessImGuiFrame&) = delete;
    HeadlessImGuiFrame& operator=(const HeadlessImGuiFrame&) = delete;
    HeadlessImGuiFrame(HeadlessImGuiFrame&&) = delete;
    HeadlessImGuiFrame& operator=(HeadlessImGuiFrame&&) = delete;

private:
    ImGuiContext* ctx_ { nullptr };
};

cd::profile::Sample make_sample(const char* name,
                                std::uint64_t start_ns,
                                std::uint64_t dur_ns,
                                std::uint64_t thread) noexcept
{
    cd::profile::Sample s {};
    s.name = name;
    s.start_ns = start_ns;
    s.duration_ns = dur_ns;
    s.thread_hash = thread;
    return s;
}

// ===========================================================================
// detail::color_from_name_hash -- pure, deterministic, GPU-free.
// ===========================================================================

TEST(ImGuiBackendColorHash, IsDeterministicForSameName)
{
    const ImU32 a = cd::imgui::detail::color_from_name_hash("Render");
    const ImU32 b = cd::imgui::detail::color_from_name_hash("Render");
    EXPECT_EQ(a, b);
}

TEST(ImGuiBackendColorHash, DistinctNamesYieldDistinctColors)
{
    const ImU32 a = cd::imgui::detail::color_from_name_hash("Render");
    const ImU32 b = cd::imgui::detail::color_from_name_hash("Physics");
    EXPECT_NE(a, b);
}

TEST(ImGuiBackendColorHash, AlphaIsFixedAndChannelsAreFloored)
{
    // The encoder ORs a fixed alpha (220) and floors each RGB channel at 60
    // (60 + 0..127). Verify the alpha byte and the per-channel lower bound.
    const ImU32 c = cd::imgui::detail::color_from_name_hash("AnyName");
    const std::uint32_t r = (c >> IM_COL32_R_SHIFT) & 0xFFu;
    const std::uint32_t g = (c >> IM_COL32_G_SHIFT) & 0xFFu;
    const std::uint32_t b = (c >> IM_COL32_B_SHIFT) & 0xFFu;
    const std::uint32_t a = (c >> IM_COL32_A_SHIFT) & 0xFFu;
    EXPECT_EQ(a, 220u);
    EXPECT_GE(r, 60u);
    EXPECT_LE(r, 60u + 127u);
    EXPECT_GE(g, 60u);
    EXPECT_LE(g, 60u + 127u);
    EXPECT_GE(b, 60u);
    EXPECT_LE(b, 60u + 127u);
}

TEST(ImGuiBackendColorHash, EmptyNameDoesNotCrashAndIsStable)
{
    const ImU32 a = cd::imgui::detail::color_from_name_hash("");
    const ImU32 b = cd::imgui::detail::color_from_name_hash("");
    EXPECT_EQ(a, b);
}

// ===========================================================================
// profiler_flamegraph -- the data/branch logic, host-side ImGui only.
// We assert it does not crash and exercises each early-return branch.
// ===========================================================================

TEST(ImGuiBackendProfilerView, EmptySamplesTakesNoSampleBranch)
{
    HeadlessImGuiFrame frame;
    ImGui::Begin("flame_empty");
    // Empty span -> "(no samples in this snapshot)" early return.
    cd::imgui::profiler_flamegraph(std::span<const cd::profile::Sample> {});
    ImGui::End();
    SUCCEED();
}

TEST(ImGuiBackendProfilerView, DegenerateRangeTakesGuardBranch)
{
    HeadlessImGuiFrame frame;
    // All samples share the same instant with zero duration -> t_min >= t_max
    // -> "(degenerate sample range)" early return.
    const std::vector<cd::profile::Sample> samples {
        make_sample("Zero", 1000, 0, 0xAA),
        make_sample("Zero2", 1000, 0, 0xAA),
    };
    ImGui::Begin("flame_degenerate");
    cd::imgui::profiler_flamegraph(samples);
    ImGui::End();
    SUCCEED();
}

TEST(ImGuiBackendProfilerView, MultiThreadSamplesRenderWithoutCrash)
{
    HeadlessImGuiFrame frame;
    // Two threads, overlapping bars, a valid window -> full draw path
    // (row assignment, per-bar rects, hover test, Dummy advance).
    const std::vector<cd::profile::Sample> samples {
        make_sample("Render", 0, 5000, 0x01),
        make_sample("Physics", 1000, 3000, 0x02),
        make_sample("Audio", 2000, 1000, 0x01),
        make_sample("AI", 4000, 2000, 0x02),
    };
    ImGui::Begin("flame_multi");
    cd::imgui::profiler_flamegraph(samples, /*row_height=*/22.0F);
    ImGui::End();
    SUCCEED();
}

// ===========================================================================
// Context::create -- null-argument validation (no device required).
// ===========================================================================

TEST(ImGuiBackendContext, NullWindowAndDeviceRejected)
{
    cd::imgui::InitDesc desc {};
    desc.window = nullptr;
    desc.device = nullptr;
    auto ctx = cd::imgui::Context::create(desc);
    ASSERT_FALSE(ctx.has_value());
    EXPECT_EQ(ctx.error().domain, cd::imgui::imgui_errors::kDomain);
    EXPECT_EQ(ctx.error().code,
              static_cast<std::uint32_t>(cd::imgui::imgui_errors::Code::kInvalidArgument));
}

TEST(ImGuiBackendContext, NullDeviceAloneRejected)
{
    // A non-null window pointer but null device still hits the kInvalidArgument
    // guard (the `|| desc.device == nullptr` clause) before any Vulkan call.
    auto* fake_window = reinterpret_cast<cd::platform::IWindow*>(0x1);
    cd::imgui::InitDesc desc {};
    desc.window = fake_window;
    desc.device = nullptr;
    auto ctx = cd::imgui::Context::create(desc);
    ASSERT_FALSE(ctx.has_value());
    EXPECT_EQ(ctx.error().code,
              static_cast<std::uint32_t>(cd::imgui::imgui_errors::Code::kInvalidArgument));
}

}  // namespace
