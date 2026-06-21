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
//   * to_imgui_key                 -- every cd::platform::KeyCode arm (26 letters
//                                     + 10 digits + editing + modifiers + arrows
//                                     + F1-F12 + kDelete) plus unmapped sentinels
//                                     (kUnknown, kCount) -> ImGuiKey_None.
//   * to_imgui_mouse_button        -- all 5 platform buttons including the kX1/kX2
//                                     side-button fix (formerly fell to default=0,
//                                     misrouting onto Left button).
//
// The live GPU-rendering verification (ImGui_ImplVulkan_RenderDrawData) is
// device-gated, exactly like the rhi Metal tests, and is sealed as
// promote-on-need in ADR-20260616-band4-singletons-scope §imgui_backend.
// =============================================================================
#include <cd/imgui/Context.hpp>
#include <cd/imgui/InputMapping.hpp>
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

TEST(ImGuiBackendErrors, MakeWithEmptyMessageStillCarriesDomainAndCode)
{
    // Default message path — make() can be called with only a Code and no
    // diagnostic string. Domain + code must still be correct.
    const auto ec = cd::imgui::imgui_errors::make(cd::imgui::imgui_errors::Code::kInitFailed);
    EXPECT_EQ(ec.domain, cd::imgui::imgui_errors::kDomain);
    EXPECT_EQ(ec.code, static_cast<std::uint32_t>(cd::imgui::imgui_errors::Code::kInitFailed));
}

// ===========================================================================
// to_imgui_key -- exhaustive mapping table (public since InputMapping.hpp).
// ===========================================================================

TEST(ImGuiBackendInputMapping, AllLettersMappedCorrectly)
{
    using K = cd::platform::KeyCode;
    // Spot-check A, M, Z as representative letter anchors.
    EXPECT_EQ(cd::imgui::to_imgui_key(K::kA), ImGuiKey_A);
    EXPECT_EQ(cd::imgui::to_imgui_key(K::kM), ImGuiKey_M);
    EXPECT_EQ(cd::imgui::to_imgui_key(K::kZ), ImGuiKey_Z);
}

TEST(ImGuiBackendInputMapping, AllLettersMappedExhaustively)
{
    // Every letter A-Z must map to a non-None ImGuiKey.
    using K = cd::platform::KeyCode;
    const cd::platform::KeyCode letters[] = {
        K::kA, K::kB, K::kC, K::kD, K::kE, K::kF, K::kG, K::kH, K::kI,
        K::kJ, K::kK, K::kL, K::kM, K::kN, K::kO, K::kP, K::kQ, K::kR,
        K::kS, K::kT, K::kU, K::kV, K::kW, K::kX, K::kY, K::kZ,
    };
    for (const auto key : letters)
        EXPECT_NE(cd::imgui::to_imgui_key(key), ImGuiKey_None)
            << "Letter at index " << static_cast<int>(key) << " mapped to None";
}

TEST(ImGuiBackendInputMapping, AllDigitsMappedExhaustively)
{
    using K = cd::platform::KeyCode;
    const cd::platform::KeyCode digits[] = {
        K::k0, K::k1, K::k2, K::k3, K::k4,
        K::k5, K::k6, K::k7, K::k8, K::k9,
    };
    for (const auto key : digits)
        EXPECT_NE(cd::imgui::to_imgui_key(key), ImGuiKey_None)
            << "Digit at index " << static_cast<int>(key) << " mapped to None";
}

TEST(ImGuiBackendInputMapping, EditingKeysMappedCorrectly)
{
    using K = cd::platform::KeyCode;
    EXPECT_EQ(cd::imgui::to_imgui_key(K::kSpace),     ImGuiKey_Space);
    EXPECT_EQ(cd::imgui::to_imgui_key(K::kEnter),     ImGuiKey_Enter);
    EXPECT_EQ(cd::imgui::to_imgui_key(K::kEscape),    ImGuiKey_Escape);
    EXPECT_EQ(cd::imgui::to_imgui_key(K::kTab),       ImGuiKey_Tab);
    EXPECT_EQ(cd::imgui::to_imgui_key(K::kBackspace), ImGuiKey_Backspace);
}

// This is the key regression test: kDelete existed in cd::platform::KeyCode
// but was missing from the original anonymous-namespace switch, causing it to
// silently return ImGuiKey_None instead of ImGuiKey_Delete. InputMapping.hpp
// adds it.
TEST(ImGuiBackendInputMapping, DeleteKeyMappedToImGuiDelete)
{
    EXPECT_EQ(cd::imgui::to_imgui_key(cd::platform::KeyCode::kDelete),
              ImGuiKey_Delete);
}

TEST(ImGuiBackendInputMapping, ModifierKeysMappedCorrectly)
{
    using K = cd::platform::KeyCode;
    EXPECT_EQ(cd::imgui::to_imgui_key(K::kLShift), ImGuiKey_LeftShift);
    EXPECT_EQ(cd::imgui::to_imgui_key(K::kRShift), ImGuiKey_RightShift);
    EXPECT_EQ(cd::imgui::to_imgui_key(K::kLCtrl),  ImGuiKey_LeftCtrl);
    EXPECT_EQ(cd::imgui::to_imgui_key(K::kRCtrl),  ImGuiKey_RightCtrl);
    EXPECT_EQ(cd::imgui::to_imgui_key(K::kLAlt),   ImGuiKey_LeftAlt);
    EXPECT_EQ(cd::imgui::to_imgui_key(K::kRAlt),   ImGuiKey_RightAlt);
}

TEST(ImGuiBackendInputMapping, ArrowKeysMappedCorrectly)
{
    using K = cd::platform::KeyCode;
    EXPECT_EQ(cd::imgui::to_imgui_key(K::kLeft),  ImGuiKey_LeftArrow);
    EXPECT_EQ(cd::imgui::to_imgui_key(K::kRight), ImGuiKey_RightArrow);
    EXPECT_EQ(cd::imgui::to_imgui_key(K::kUp),    ImGuiKey_UpArrow);
    EXPECT_EQ(cd::imgui::to_imgui_key(K::kDown),  ImGuiKey_DownArrow);
}

TEST(ImGuiBackendInputMapping, FunctionKeysMappedExhaustively)
{
    using K = cd::platform::KeyCode;
    EXPECT_EQ(cd::imgui::to_imgui_key(K::kF1),  ImGuiKey_F1);
    EXPECT_EQ(cd::imgui::to_imgui_key(K::kF2),  ImGuiKey_F2);
    EXPECT_EQ(cd::imgui::to_imgui_key(K::kF3),  ImGuiKey_F3);
    EXPECT_EQ(cd::imgui::to_imgui_key(K::kF4),  ImGuiKey_F4);
    EXPECT_EQ(cd::imgui::to_imgui_key(K::kF5),  ImGuiKey_F5);
    EXPECT_EQ(cd::imgui::to_imgui_key(K::kF6),  ImGuiKey_F6);
    EXPECT_EQ(cd::imgui::to_imgui_key(K::kF7),  ImGuiKey_F7);
    EXPECT_EQ(cd::imgui::to_imgui_key(K::kF8),  ImGuiKey_F8);
    EXPECT_EQ(cd::imgui::to_imgui_key(K::kF9),  ImGuiKey_F9);
    EXPECT_EQ(cd::imgui::to_imgui_key(K::kF10), ImGuiKey_F10);
    EXPECT_EQ(cd::imgui::to_imgui_key(K::kF11), ImGuiKey_F11);
    EXPECT_EQ(cd::imgui::to_imgui_key(K::kF12), ImGuiKey_F12);
}

TEST(ImGuiBackendInputMapping, UnknownKeyMapsToNone)
{
    EXPECT_EQ(cd::imgui::to_imgui_key(cd::platform::KeyCode::kUnknown),
              ImGuiKey_None);
}

TEST(ImGuiBackendInputMapping, CountSentinelMapsToNone)
{
    // kCount is not a real key; the mapping must not crash and must return None
    // so that handle_event suppresses the AddKeyEvent call.
    EXPECT_EQ(cd::imgui::to_imgui_key(cd::platform::KeyCode::kCount),
              ImGuiKey_None);
}

// to_imgui_key is pure and deterministic — same input always yields same output.
TEST(ImGuiBackendInputMapping, KeyMappingIsDeterministic)
{
    const auto a = cd::imgui::to_imgui_key(cd::platform::KeyCode::kW);
    const auto b = cd::imgui::to_imgui_key(cd::platform::KeyCode::kW);
    EXPECT_EQ(a, b);
    EXPECT_EQ(a, ImGuiKey_W);
}

// ===========================================================================
// to_imgui_mouse_button -- all 5 button slots + kX1/kX2 regression.
// ===========================================================================

TEST(ImGuiBackendInputMapping, MouseLeftMapsToSlot0)
{
    EXPECT_EQ(cd::imgui::to_imgui_mouse_button(cd::platform::MouseButton::kLeft), 0);
}

TEST(ImGuiBackendInputMapping, MouseRightMapsToSlot1)
{
    EXPECT_EQ(cd::imgui::to_imgui_mouse_button(cd::platform::MouseButton::kRight), 1);
}

TEST(ImGuiBackendInputMapping, MouseMiddleMapsToSlot2)
{
    EXPECT_EQ(cd::imgui::to_imgui_mouse_button(cd::platform::MouseButton::kMiddle), 2);
}

// Regression: before InputMapping.hpp, kX1 and kX2 fell to default=0, which
// misrouted side-button presses onto the left-button state. The correct ImGui
// slots are 3 and 4 (ImGuiMouseButton_COUNT = 5).
TEST(ImGuiBackendInputMapping, MouseX1MapsToSlot3NotSlot0)
{
    const int slot = cd::imgui::to_imgui_mouse_button(cd::platform::MouseButton::kX1);
    EXPECT_EQ(slot, 3);
    EXPECT_NE(slot, 0);  // Must not misroute onto Left button.
}

TEST(ImGuiBackendInputMapping, MouseX2MapsToSlot4NotSlot0)
{
    const int slot = cd::imgui::to_imgui_mouse_button(cd::platform::MouseButton::kX2);
    EXPECT_EQ(slot, 4);
    EXPECT_NE(slot, 0);  // Must not misroute onto Left button.
}

TEST(ImGuiBackendInputMapping, AllMouseSlotsAreDistinct)
{
    // No two distinct buttons may share an ImGui slot — that would cause one
    // button's state to bleed into another in a live session.
    using M = cd::platform::MouseButton;
    const int left   = cd::imgui::to_imgui_mouse_button(M::kLeft);
    const int right  = cd::imgui::to_imgui_mouse_button(M::kRight);
    const int middle = cd::imgui::to_imgui_mouse_button(M::kMiddle);
    const int x1     = cd::imgui::to_imgui_mouse_button(M::kX1);
    const int x2     = cd::imgui::to_imgui_mouse_button(M::kX2);
    EXPECT_NE(left,   right);
    EXPECT_NE(left,   middle);
    EXPECT_NE(left,   x1);
    EXPECT_NE(left,   x2);
    EXPECT_NE(right,  middle);
    EXPECT_NE(right,  x1);
    EXPECT_NE(right,  x2);
    EXPECT_NE(middle, x1);
    EXPECT_NE(middle, x2);
    EXPECT_NE(x1,     x2);
}

TEST(ImGuiBackendInputMapping, AllMouseSlotsWithinImGuiRange)
{
    // ImGui defines ImGuiMouseButton_COUNT = 5; slots 0-4 are valid.
    using M = cd::platform::MouseButton;
    const cd::platform::MouseButton buttons[] = {
        M::kLeft, M::kRight, M::kMiddle, M::kX1, M::kX2,
    };
    for (const auto btn : buttons)
    {
        const int slot = cd::imgui::to_imgui_mouse_button(btn);
        EXPECT_GE(slot, 0);
        EXPECT_LT(slot, 5);  // ImGuiMouseButton_COUNT = 5
    }
}

TEST(ImGuiBackendInputMapping, CountSentinelMouseButtonFallsToZero)
{
    // kCount is a sentinel, never emitted by the platform layer.
    // It must not crash; it returns 0 (safe no-op falls to Left slot).
    const int slot = cd::imgui::to_imgui_mouse_button(cd::platform::MouseButton::kCount);
    EXPECT_GE(slot, 0);
    EXPECT_LT(slot, 5);
}

// ===========================================================================
// InputMapping combined -- consistency between key and mouse APIs.
// ===========================================================================

TEST(ImGuiBackendInputMapping, KeyMappingIsNoexcept)
{
    // Contractual: both mapping functions are marked noexcept.
    // This test guards against accidental noexcept removal during refactors.
    static_assert(noexcept(cd::imgui::to_imgui_key(cd::platform::KeyCode::kA)),
                  "to_imgui_key must be noexcept");
    static_assert(noexcept(cd::imgui::to_imgui_mouse_button(cd::platform::MouseButton::kLeft)),
                  "to_imgui_mouse_button must be noexcept");
    SUCCEED();
}

// ===========================================================================
// ProfilerView supplemental edge cases.
// ===========================================================================

// Verify that a single sample with zero-duration still has t_min < t_max
// when paired with a non-zero-duration peer — the degenerate guard fires
// only when ALL samples share the exact same time-range with zero span.
TEST(ImGuiBackendProfilerView, SingleNonZeroDurationSampleDrawsCanvas)
{
    HeadlessImGuiFrame frame;
    const std::vector<cd::profile::Sample> samples {
        make_sample("Solo", 1000, 100, 0x10),
    };
    // t_max = 1100, t_min = 1000 -> window = 100 ns -> NOT degenerate.
    // Canvas must be >= row_height.
    const float adv =
        cursor_advance([&] { cd::imgui::profiler_flamegraph(samples, 20.0F); });
    EXPECT_GE(adv, 20.0F);
}

// Three threads with non-overlapping time ranges must each get their own row;
// canvas height >= 3 * row_height.
TEST(ImGuiBackendProfilerView, ThreeThreadsReserveTripleRowCanvas)
{
    HeadlessImGuiFrame frame;
    const std::vector<cd::profile::Sample> samples {
        make_sample("A", 0,    1000, 0x01),
        make_sample("B", 1000, 1000, 0x02),
        make_sample("C", 2000, 1000, 0x03),
    };
    const float adv =
        cursor_advance([&] { cd::imgui::profiler_flamegraph(samples, 20.0F); });
    EXPECT_GE(adv, 3.0F * 20.0F - 1.0F);  // 3 rows, allow rounding
}

// A custom row_height of 1.0 F (minimum practical) must not crash and must
// still generate a non-zero canvas height.
TEST(ImGuiBackendProfilerView, MinimalRowHeightDoesNotCrash)
{
    HeadlessImGuiFrame frame;
    const std::vector<cd::profile::Sample> samples {
        make_sample("Tiny", 0, 500, 0xFF),
    };
    const float adv =
        cursor_advance([&] { cd::imgui::profiler_flamegraph(samples, 1.0F); });
    EXPECT_GE(adv, 1.0F);
}

// An extremely large duration_ns value must not overflow the floating-point
// range used in the per-bar x-coordinate math and must not crash.
TEST(ImGuiBackendProfilerView, VeryLargeDurationHandledWithoutCrash)
{
    HeadlessImGuiFrame frame;
    // ~584 years in nanoseconds — near the uint64_t saturation boundary.
    constexpr std::uint64_t kHuge = UINT64_MAX / 2u;
    const std::vector<cd::profile::Sample> samples {
        make_sample("Huge", 0, kHuge, 0x01),
    };
    ImGui::Begin("large_dur_probe");
    cd::imgui::profiler_flamegraph(samples, 18.0F);
    ImGui::End();
    SUCCEED();
}

}  // namespace
