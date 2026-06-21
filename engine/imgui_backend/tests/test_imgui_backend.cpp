// =============================================================================
// CHROMODYNAMIC -- cd::imgui_backend host-side tests (band4 singletons).
//
// The imgui_backend ships a real Vulkan ImGui backend whose draw path is
// device-gated (verified end-to-end by the renderer + the rhi pixel-parity
// capstone). These tests cover everything that is testable WITHOUT a live
// Vulkan device:
//
//   * detail::color_from_name_hash -- pure FNV-1a -> ImU32. Pinned to EXACT
//                                     per-channel output for two known names
//                                     + the empty/offset-basis case, plus
//                                     embedded-NUL and one-char-divergence
//                                     edge cases.
//   * profiler_flamegraph          -- the data/formatting + branch logic,
//                                     driven through a HOST-ONLY ImGui context
//                                     (ImGui core is CPU-only; only
//                                     ImGui_ImplVulkan_* needs a GPU, which we
//                                     never call here). Pinned by observing the
//                                     ImGui cursor advance: guard branches
//                                     reserve no canvas, the draw path reserves
//                                     rows*row_height, and repeated thread_hash
//                                     values collapse to a single row.
//   * Context::create              -- the null-argument validation branches
//                                     (window/device, each side) returning
//                                     kInvalidArgument with a diagnostic before
//                                     any device handle is touched, plus the
//                                     public InitDesc defaults.
//   * imgui_errors                 -- domain + Code enum values + make().
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
#include <functional>
#include <string_view>
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

// Extract the four channels via ImGui's portable named shifts so the
// assertions are independent of the IMGUI_USE_BGRA_PACKED_COLOR layout.
struct Rgba
{
    std::uint32_t r { 0 };
    std::uint32_t g { 0 };
    std::uint32_t b { 0 };
    std::uint32_t a { 0 };
};

[[nodiscard]] Rgba unpack(ImU32 c) noexcept
{
    return {
        (c >> IM_COL32_R_SHIFT) & 0xFFu,
        (c >> IM_COL32_G_SHIFT) & 0xFFu,
        (c >> IM_COL32_B_SHIFT) & 0xFFu,
        (c >> IM_COL32_A_SHIFT) & 0xFFu,
    };
}

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

// Pin the EXACT channel output of the FNV-1a -> RGB derivation for two
// known names. Computed independently against the documented algorithm
// (offset basis 2166136261, prime 16777619, then 60 + (byte & 0x7F) per
// channel from hash bytes 0/8/16). Any drift in the constants or the
// channel math breaks these.
TEST(ImGuiBackendColorHash, RenderNamePinsExactChannels)
{
    const Rgba c = unpack(cd::imgui::detail::color_from_name_hash("Render"));
    EXPECT_EQ(c.r, 105u);
    EXPECT_EQ(c.g, 176u);
    EXPECT_EQ(c.b, 72u);
    EXPECT_EQ(c.a, 220u);
}

TEST(ImGuiBackendColorHash, PhysicsNamePinsExactChannels)
{
    const Rgba c = unpack(cd::imgui::detail::color_from_name_hash("Physics"));
    EXPECT_EQ(c.r, 126u);
    EXPECT_EQ(c.g, 64u);
    EXPECT_EQ(c.b, 129u);
    EXPECT_EQ(c.a, 220u);
}

// The empty-name path must reduce to the bare FNV-1a offset basis
// (0x811C9DC5) -> channels (129, 89, 88). Locks the "no bytes consumed"
// behaviour so an accidental seed change is caught.
TEST(ImGuiBackendColorHash, EmptyNamePinsOffsetBasisChannels)
{
    const Rgba c = unpack(cd::imgui::detail::color_from_name_hash(""));
    EXPECT_EQ(c.r, 129u);
    EXPECT_EQ(c.g, 89u);
    EXPECT_EQ(c.b, 88u);
    EXPECT_EQ(c.a, 220u);
}

// A single appended byte must diverge from the empty-name colour: proves
// the loop body actually mixes input rather than ignoring it.
TEST(ImGuiBackendColorHash, OneCharDiffersFromEmpty)
{
    EXPECT_NE(cd::imgui::detail::color_from_name_hash("A"),
              cd::imgui::detail::color_from_name_hash(""));
}

// std::string_view length is honoured: an embedded NUL must NOT terminate
// the hash early (the loop iterates the view, not a C-string).
TEST(ImGuiBackendColorHash, EmbeddedNulIsHashedNotTerminated)
{
    const std::string_view with_nul { "ab\0cd", 5 };
    const std::string_view prefix { "ab", 2 };
    EXPECT_NE(cd::imgui::detail::color_from_name_hash(with_nul),
              cd::imgui::detail::color_from_name_hash(prefix));
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

// The empty / degenerate guard paths emit a single TextDisabled line and
// return WITHOUT reserving a canvas. The full path emits a header line
// plus an ImGui::Dummy of height `max(row_height, rows*row_height)`. We
// assert on the cursor-Y advance to pin which branch ran (host-observable,
// no GPU): the no-canvas paths advance far less than the canvas path.

[[nodiscard]] float cursor_advance(const std::function<void()>& body)
{
    ImGui::Begin("flame_probe");
    const float y0 = ImGui::GetCursorPosY();
    body();
    const float y1 = ImGui::GetCursorPosY();
    ImGui::End();
    return y1 - y0;
}

TEST(ImGuiBackendProfilerView, EmptySamplesReservesNoCanvas)
{
    HeadlessImGuiFrame frame;
    // Empty span -> "(no samples in this snapshot)" early return: one text
    // line, no Dummy canvas.
    const float adv = cursor_advance([] {
        cd::imgui::profiler_flamegraph(std::span<const cd::profile::Sample> {});
    });
    // A single text line advances by roughly one font line height; it must
    // be well under the smallest canvas (row_height default 18).
    EXPECT_GT(adv, 0.0F);
    EXPECT_LT(adv, 18.0F);
}

TEST(ImGuiBackendProfilerView, DegenerateRangeReservesNoCanvas)
{
    HeadlessImGuiFrame frame;
    // All samples share the same instant with zero duration -> t_min >= t_max
    // -> "(degenerate sample range)" early return.
    const std::vector<cd::profile::Sample> samples {
        make_sample("Zero", 1000, 0, 0xAA),
        make_sample("Zero2", 1000, 0, 0xAA),
    };
    const float adv = cursor_advance([&] { cd::imgui::profiler_flamegraph(samples); });
    EXPECT_GT(adv, 0.0F);
    EXPECT_LT(adv, 18.0F);
}

TEST(ImGuiBackendProfilerView, SingleThreadReservesOneRowCanvas)
{
    HeadlessImGuiFrame frame;
    // One thread, valid window -> header text + Dummy(canvas_h) where
    // canvas_h == max(row_height, 1*row_height) == row_height. The advance
    // must clear the canvas height even after a header line.
    const std::vector<cd::profile::Sample> samples {
        make_sample("Render", 0, 5000, 0x01),
    };
    const float adv =
        cursor_advance([&] { cd::imgui::profiler_flamegraph(samples, /*row_height=*/20.0F); });
    EXPECT_GE(adv, 20.0F);
}

TEST(ImGuiBackendProfilerView, MultiThreadReservesTallerCanvasThanSingle)
{
    HeadlessImGuiFrame frame;
    // Two distinct thread_hash values -> two rows -> canvas_h == 2*row_height,
    // strictly taller than the one-row case. Pins the per-thread row tally.
    const std::vector<cd::profile::Sample> one {
        make_sample("Render", 0, 5000, 0x01),
    };
    const std::vector<cd::profile::Sample> two {
        make_sample("Render", 0, 5000, 0x01),
        make_sample("Physics", 1000, 3000, 0x02),
    };
    const float adv_one =
        cursor_advance([&] { cd::imgui::profiler_flamegraph(one, /*row_height=*/20.0F); });
    const float adv_two =
        cursor_advance([&] { cd::imgui::profiler_flamegraph(two, /*row_height=*/20.0F); });
    // Both share the same header line; the canvas grows by one row_height.
    EXPECT_GT(adv_two, adv_one);
    EXPECT_GE(adv_two - adv_one, 20.0F - 1.0F);  // ~one extra row, allow rounding
}

TEST(ImGuiBackendProfilerView, RepeatedThreadHashCollapsesToOneRow)
{
    HeadlessImGuiFrame frame;
    // Four samples but only ONE distinct thread_hash -> one row. Advance
    // must match the single-sample one-row case (try_emplace dedups rows).
    const std::vector<cd::profile::Sample> many_same_thread {
        make_sample("Render", 0, 5000, 0x07),
        make_sample("Physics", 1000, 3000, 0x07),
        make_sample("Audio", 2000, 1000, 0x07),
        make_sample("AI", 4000, 2000, 0x07),
    };
    const std::vector<cd::profile::Sample> one {
        make_sample("Render", 0, 5000, 0x07),
    };
    const float adv_many =
        cursor_advance([&] { cd::imgui::profiler_flamegraph(many_same_thread, 20.0F); });
    const float adv_one = cursor_advance([&] { cd::imgui::profiler_flamegraph(one, 20.0F); });
    EXPECT_FLOAT_EQ(adv_many, adv_one);
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

TEST(ImGuiBackendContext, NullWindowAloneRejected)
{
    // The mirror of the above: a non-null device pointer but null window
    // still hits the guard via the `desc.window == nullptr` clause first.
    auto* fake_device = reinterpret_cast<cd::rhi::IDevice*>(0x1);
    cd::imgui::InitDesc desc {};
    desc.window = nullptr;
    desc.device = fake_device;
    auto ctx = cd::imgui::Context::create(desc);
    ASSERT_FALSE(ctx.has_value());
    EXPECT_EQ(ctx.error().code,
              static_cast<std::uint32_t>(cd::imgui::imgui_errors::Code::kInvalidArgument));
}

TEST(ImGuiBackendContext, InvalidArgumentCarriesDiagnosticMessage)
{
    // The null-guard ships a human-readable message; pin its substance so a
    // refactor cannot silently drop the diagnostic.
    cd::imgui::InitDesc desc {};
    auto ctx = cd::imgui::Context::create(desc);
    ASSERT_FALSE(ctx.has_value());
    const std::string_view msg = ctx.error().message;
    EXPECT_NE(msg.find("null"), std::string_view::npos);
}

TEST(ImGuiBackendContext, InitDescDefaultsMatchHeaderContract)
{
    // Behaviour-pin the public InitDesc defaults the header documents:
    // BGRA8 unorm target + double-buffered frames-in-flight.
    const cd::imgui::InitDesc desc {};
    EXPECT_EQ(desc.window, nullptr);
    EXPECT_EQ(desc.device, nullptr);
    EXPECT_EQ(desc.color_format, cd::rhi::Format::kBGRA8Unorm);
    EXPECT_EQ(desc.frames_in_flight, 2u);
}

// ===========================================================================
// Error-domain / code mapping -- pin the public imgui_errors contract.
// ===========================================================================

TEST(ImGuiBackendErrors, DomainAndCodesArePinned)
{
    EXPECT_EQ(cd::imgui::imgui_errors::kDomain, 0x0014u);
    EXPECT_EQ(static_cast<std::uint32_t>(cd::imgui::imgui_errors::Code::kOk), 0u);
    EXPECT_EQ(static_cast<std::uint32_t>(cd::imgui::imgui_errors::Code::kBackendMismatch), 1u);
    EXPECT_EQ(static_cast<std::uint32_t>(cd::imgui::imgui_errors::Code::kInitFailed), 2u);
    EXPECT_EQ(static_cast<std::uint32_t>(cd::imgui::imgui_errors::Code::kInvalidArgument), 3u);
}

TEST(ImGuiBackendErrors, MakeBuildsCodeInDomain)
{
    const auto ec =
        cd::imgui::imgui_errors::make(cd::imgui::imgui_errors::Code::kBackendMismatch, "x");
    EXPECT_EQ(ec.domain, cd::imgui::imgui_errors::kDomain);
    EXPECT_EQ(ec.code, static_cast<std::uint32_t>(cd::imgui::imgui_errors::Code::kBackendMismatch));
}

}  // namespace
