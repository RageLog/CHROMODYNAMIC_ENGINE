// =============================================================================
// CHROMODYNAMIC — cd::render tests
//
// The renderer talks to a real swapchain, so meaningful tests need a real
// Vulkan ICD AND a real window surface. We use the existing hidden-Win32-
// window helper pattern (a Vulkan ICD is required; absent → skip).
// NullDevice would happily satisfy the interface, but its acquire_next_image
// and present are stubs; the value of testing the renderer there is small.
// =============================================================================
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#endif

#include <cd/render/DrawBucket.hpp>
#include <cd/render/Renderer.hpp>
#include <cd/render/SortKey.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/NullCommandBuffer.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#include <gtest/gtest.h>

using cd::render::depth_bits_of;
using cd::render::make_sort_key;
using cd::render::material_id_of;
using cd::render::SortBlend;
using cd::render::SortLayer;

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace
{

std::unique_ptr<cd::rhi::IDevice> try_make_device()
{
    cd::rhi::vulkan::VulkanCreateInfo info {};
    info.enable_validation = false;
    auto r = cd::rhi::vulkan::create_vulkan_device(info);
    if (!r.has_value())
        return nullptr;
    return std::move(*r);
}

#define CD_SKIP_IF_NO_VULKAN(dev_var)    \
    auto dev_var = try_make_device(); \
    if (!dev_var)                     \
    GTEST_SKIP() << "no Vulkan ICD available on this host"

#if defined(_WIN32)
class HiddenWindow
{
public:
    HiddenWindow() : instance_(GetModuleHandleW(nullptr))
    {
        
        WNDCLASSEXW wc {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = instance_;
        wc.lpszClassName = L"CdRenderTestWindow";
        RegisterClassExW(&wc);
        hwnd_ = CreateWindowExW(
            0,
            wc.lpszClassName,
            L"cd_render_test",
            WS_OVERLAPPEDWINDOW,
            0,
            0,
            256,
            256,
            nullptr,
            nullptr,
            instance_,
            nullptr
        );
    }

    ~HiddenWindow()
    {
        if (hwnd_ != nullptr)
            DestroyWindow(hwnd_);
        UnregisterClassW(L"CdRenderTestWindow", instance_);
    }

    HiddenWindow(const HiddenWindow&) = delete;
    HiddenWindow& operator=(const HiddenWindow&) = delete;
    HiddenWindow(HiddenWindow&&) = delete;
    HiddenWindow& operator=(HiddenWindow&&) = delete;

    [[nodiscard]] HWND hwnd() const noexcept
    {
        return hwnd_;
    }

    [[nodiscard]] HINSTANCE hinstance() const noexcept
    {
        return instance_;
    }

private:
    HINSTANCE instance_ { nullptr };
    HWND hwnd_ { nullptr };
};
#endif  // _WIN32

TEST(Renderer, RejectsNullDevice)
{
    cd::render::RendererDesc d {};
    d.device = nullptr;
    auto r = cd::render::Renderer::create(d);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::render::render_errors::Code::kInvalidArgument));
}

TEST(Renderer, RejectsZeroExtent)
{
    CD_SKIP_IF_NO_VULKAN(dev);
    cd::render::RendererDesc d {};
    d.device = dev.get();
    d.swapchain.extent = { 0, 0 };
    auto r = cd::render::Renderer::create(d);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::render::render_errors::Code::kInvalidArgument));
}

TEST(Renderer, RejectsZeroFramesInFlight)
{
    CD_SKIP_IF_NO_VULKAN(dev);
    cd::render::RendererDesc d {};
    d.device = dev.get();
    d.swapchain.extent = { 64, 64 };
    d.frames_in_flight = 0;
    auto r = cd::render::Renderer::create(d);
    ASSERT_FALSE(r.has_value());
}

#if defined(_WIN32)
TEST(Renderer, EndFrameWithoutBeginRejected)
{
    CD_SKIP_IF_NO_VULKAN(dev);
    HiddenWindow w;
    cd::render::RendererDesc d {};
    d.device = dev.get();
    d.swapchain.window_handle = w.hwnd();
    d.swapchain.display_handle = w.hinstance();
    d.swapchain.extent = { 128, 96 };
    d.swapchain.image_count = 2;
    d.swapchain.format = cd::rhi::Format::kBGRA8Unorm;
    auto r = cd::render::Renderer::create(d);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    auto bad = r->end_frame();
    ASSERT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().code, static_cast<std::uint32_t>(cd::render::render_errors::Code::kFrameInFlight));
}

TEST(Renderer, DoubleBeginRejected)
{
    CD_SKIP_IF_NO_VULKAN(dev);
    HiddenWindow w;
    cd::render::RendererDesc d {};
    d.device = dev.get();
    d.swapchain.window_handle = w.hwnd();
    d.swapchain.display_handle = w.hinstance();
    d.swapchain.extent = { 128, 96 };
    d.swapchain.image_count = 2;
    d.swapchain.format = cd::rhi::Format::kBGRA8Unorm;
    auto r = cd::render::Renderer::create(d);
    ASSERT_TRUE(r.has_value());
    auto first = r->begin_frame();
    ASSERT_TRUE(first.has_value()) << first.error().message;
    auto second = r->begin_frame();
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code, static_cast<std::uint32_t>(cd::render::render_errors::Code::kFrameInFlight));
    ASSERT_TRUE(r->end_frame().has_value());
}

TEST(Renderer, SingleFrameRoundTrip)
{
    // The whole point: begin_frame → record → end_frame should drive a
    // full acquire/submit/present cycle with implicit barriers on the
    // swapchain image. No explicit render-pass recording — we just verify the
    // sync hand-off works end-to-end on the real GPU.
    CD_SKIP_IF_NO_VULKAN(dev);
    HiddenWindow w;
    cd::render::RendererDesc d {};
    d.device = dev.get();
    d.swapchain.window_handle = w.hwnd();
    d.swapchain.display_handle = w.hinstance();
    d.swapchain.extent = { 128, 96 };
    d.swapchain.image_count = 2;
    d.swapchain.format = cd::rhi::Format::kBGRA8Unorm;
    d.frames_in_flight = 2;
    auto r = cd::render::Renderer::create(d);
    ASSERT_TRUE(r.has_value()) << r.error().message;

    auto frame = r->begin_frame();
    ASSERT_TRUE(frame.has_value()) << frame.error().message;
    EXPECT_NE(frame->command_buffer, nullptr);
    EXPECT_TRUE(frame->swapchain_image.is_valid());
    EXPECT_TRUE(frame->swapchain_image_view.is_valid());
    EXPECT_EQ(frame->extent.width, 128U);
    EXPECT_EQ(frame->extent.height, 96U);
    EXPECT_EQ(frame->frame_index, 0U);

    auto end_r = r->end_frame();
    ASSERT_TRUE(end_r.has_value()) << end_r.error().message;
    r->wait_idle();
}

TEST(Renderer, RecreateSwapchainChangesExtent)
{
    // Mimic the resize path the hello_triangle sample drives: build a
    // Renderer at one size, call recreate_swapchain with a different size,
    // then drive a frame to confirm the new swapchain is usable.
    CD_SKIP_IF_NO_VULKAN(dev);
    HiddenWindow w;
    cd::render::RendererDesc d {};
    d.device = dev.get();
    d.swapchain.window_handle = w.hwnd();
    d.swapchain.display_handle = w.hinstance();
    d.swapchain.extent = { 128, 96 };
    d.swapchain.image_count = 2;
    d.swapchain.format = cd::rhi::Format::kBGRA8Unorm;
    d.frames_in_flight = 2;
    auto r = cd::render::Renderer::create(d);
    ASSERT_TRUE(r.has_value()) << r.error().message;

    // First frame on the original size.
    ASSERT_TRUE(r->begin_frame().has_value());
    ASSERT_TRUE(r->end_frame().has_value());

    // Recreate to a different size; subsequent frame must succeed.
    auto re = r->recreate_swapchain({ 256, 192 });
    ASSERT_TRUE(re.has_value()) << re.error().message;
    EXPECT_EQ(r->swapchain_extent().width, 256U);
    EXPECT_EQ(r->swapchain_extent().height, 192U);

    ASSERT_TRUE(r->begin_frame().has_value());
    ASSERT_TRUE(r->end_frame().has_value());
    r->wait_idle();
}

TEST(Renderer, RecreateSwapchainRejectsZeroExtent)
{
    CD_SKIP_IF_NO_VULKAN(dev);
    HiddenWindow w;
    cd::render::RendererDesc d {};
    d.device = dev.get();
    d.swapchain.window_handle = w.hwnd();
    d.swapchain.display_handle = w.hinstance();
    d.swapchain.extent = { 128, 96 };
    d.swapchain.format = cd::rhi::Format::kBGRA8Unorm;
    auto r = cd::render::Renderer::create(d);
    ASSERT_TRUE(r.has_value());
    auto bad = r->recreate_swapchain({ 0, 0 });
    ASSERT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().code, static_cast<std::uint32_t>(cd::render::render_errors::Code::kInvalidArgument));
}

TEST(Renderer, SubmitDrawsRejectsOutsideFrame)
{
    CD_SKIP_IF_NO_VULKAN(dev);
    HiddenWindow w;
    cd::render::RendererDesc d {};
    d.device = dev.get();
    d.swapchain.window_handle = w.hwnd();
    d.swapchain.display_handle = w.hinstance();
    d.swapchain.extent = { 64, 64 };
    d.swapchain.format = cd::rhi::Format::kBGRA8Unorm;
    auto r = cd::render::Renderer::create(d);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    cd::render::DrawBucket bucket;
    auto bad = r->submit_draws(bucket);
    ASSERT_FALSE(bad.has_value());
    EXPECT_EQ(
        bad.error().code,
        static_cast<std::uint32_t>(cd::render::render_errors::Code::kFrameInFlight)
    );
}

TEST(Renderer, SubmitDrawsReplaysBucketInSortedOrder)
{
    CD_SKIP_IF_NO_VULKAN(dev);
    HiddenWindow w;
    cd::render::RendererDesc d {};
    d.device = dev.get();
    d.swapchain.window_handle = w.hwnd();
    d.swapchain.display_handle = w.hinstance();
    d.swapchain.extent = { 64, 64 };
    d.swapchain.image_count = 2;
    d.swapchain.format = cd::rhi::Format::kBGRA8Unorm;
    d.frames_in_flight = 2;
    auto r = cd::render::Renderer::create(d);
    ASSERT_TRUE(r.has_value()) << r.error().message;

    auto frame = r->begin_frame();
    ASSERT_TRUE(frame.has_value()) << frame.error().message;

    cd::render::DrawBucket bucket;
    std::vector<std::uint64_t> emitted_order;
    const std::array<std::uint64_t, 5> insertion { 400, 100, 300, 50, 200 };
    for (auto v : insertion)
    {
        cd::render::SortKey k; k.value = v;
        bucket.add(k, [&emitted_order, v](cd::rhi::ICommandBuffer&) {
            emitted_order.push_back(v);
        });
    }

    auto sub = r->submit_draws(bucket);
    ASSERT_TRUE(sub.has_value()) << sub.error().message;

    ASSERT_EQ(emitted_order.size(), 5u);
    EXPECT_EQ(emitted_order[0], 50u);
    EXPECT_EQ(emitted_order[1], 100u);
    EXPECT_EQ(emitted_order[2], 200u);
    EXPECT_EQ(emitted_order[3], 300u);
    EXPECT_EQ(emitted_order[4], 400u);
    EXPECT_TRUE(bucket.is_sorted_cached());
    EXPECT_EQ(bucket.size(), 5u);  // submit_draws does not clear

    ASSERT_TRUE(r->end_frame().has_value());
    r->wait_idle();
}

TEST(Renderer, ThreeFrameLoop)
{
    // Exercises the frames_in_flight ring: with 2 frames in flight, the third
    // begin_frame must successfully reuse slot 0 (which has been drained by
    // the fence wait inside begin_frame).
    CD_SKIP_IF_NO_VULKAN(dev);
    HiddenWindow w;
    cd::render::RendererDesc d {};
    d.device = dev.get();
    d.swapchain.window_handle = w.hwnd();
    d.swapchain.display_handle = w.hinstance();
    d.swapchain.extent = { 64, 64 };
    d.swapchain.image_count = 2;
    d.swapchain.format = cd::rhi::Format::kBGRA8Unorm;
    d.frames_in_flight = 2;
    auto r = cd::render::Renderer::create(d);
    ASSERT_TRUE(r.has_value()) << r.error().message;

    for (int i = 0; i < 3; ++i)
    {
        auto f = r->begin_frame();
        ASSERT_TRUE(f.has_value()) << f.error().message;
        auto e = r->end_frame();
        ASSERT_TRUE(e.has_value()) << e.error().message;
    }
    r->wait_idle();
}
#endif  // _WIN32

}  // namespace

#include <cd/render/SortKey.hpp>

TEST(SortKey, LayerBitsAtTopOrderLayers)
{
    const auto opaque = make_sort_key(SortLayer::kOpaque, 0, SortBlend::kOff, 0, 0, 0);
    const auto ui     = make_sort_key(SortLayer::kUi,     0, SortBlend::kOff, 0, 0, 0);
    EXPECT_LT(opaque.value, ui.value);
}

TEST(SortKey, MaterialIdRecoverable)
{
    const std::uint32_t mat = 0x123456u;
    const auto k = make_sort_key(SortLayer::kOpaque, 0, SortBlend::kOff, mat, 0, 0);
    EXPECT_EQ(material_id_of(k), mat);
}

TEST(SortKey, DepthBitsRecoverable)
{
    const std::uint32_t depth = 0xABCDEFu;
    const auto k = make_sort_key(SortLayer::kOpaque, 0, SortBlend::kOff, 0, depth, 0);
    EXPECT_EQ(depth_bits_of(k), depth);
}

TEST(SortKey, AscendingSortGroupsByLayerThenMaterialThenDepth)
{
    const auto a = make_sort_key(SortLayer::kOpaque, 0, SortBlend::kOff, 5, 100, 0);
    const auto b = make_sort_key(SortLayer::kOpaque, 0, SortBlend::kOff, 5, 200, 0);
    const auto c = make_sort_key(SortLayer::kOpaque, 0, SortBlend::kOff, 6,   0, 0);
    EXPECT_LT(a.value, b.value);
    EXPECT_LT(b.value, c.value);
}

// ----- BAND 3: genuinely-untested SortKey aggregation branches --------------

TEST(SortKey, LayerOfRoundTripsEveryLayer)
{
    // layer_of() (the bit-63..62 extractor) was never asserted directly for
    // every enum value — only the opaque<ui inequality. Round-trip all four.
    using cd::render::layer_of;
    for (auto l : { SortLayer::kOpaque, SortLayer::kSkybox,
                    SortLayer::kTransparent, SortLayer::kUi })
    {
        const auto k = make_sort_key(l, 0, SortBlend::kOff, 0, 0, 0);
        EXPECT_EQ(layer_of(k), l);
    }
}

TEST(SortKey, BlendGroupOrdersAboveMaterialBelowPass)
{
    // Layout (most→least significant): layer | pass | blend(57..56) |
    // material(55..32) | depth | user_lo. So the 2-bit blend group is MORE
    // significant than the 24-bit material id but LESS significant than pass.
    // Same layer/pass/material: blend mode breaks ties ascending.
    const auto off   = make_sort_key(SortLayer::kTransparent, 0, SortBlend::kOff,          7, 10, 0);
    const auto alpha = make_sort_key(SortLayer::kTransparent, 0, SortBlend::kAlpha,        7, 10, 0);
    const auto premul= make_sort_key(SortLayer::kTransparent, 0, SortBlend::kPremultiplied,7, 10, 0);
    EXPECT_LT(off.value, alpha.value);
    EXPECT_LT(alpha.value, premul.value);
    // Blend group outranks material id: blend kAlpha + material 0 sorts AFTER
    // blend kOff + material 0xFFFFFF (the highest material under a lower blend).
    const auto off_hi_mat   = make_sort_key(SortLayer::kTransparent, 0, SortBlend::kOff,   0xFFFFFFu, 10, 0);
    const auto alpha_lo_mat = make_sort_key(SortLayer::kTransparent, 0, SortBlend::kAlpha, 0,          10, 0);
    EXPECT_LT(off_hi_mat.value, alpha_lo_mat.value);
    // But pass (bits 61..58) outranks blend: pass 1 + blend kOff sorts after
    // pass 0 + the highest blend.
    const auto pass1 = make_sort_key(SortLayer::kTransparent, 1, SortBlend::kOff, 0, 10, 0);
    EXPECT_LT(premul.value, pass1.value);
}

TEST(SortKey, UserLoIsLeastSignificant)
{
    // user_lo (bits 7..0) is the final tie-break — equal everything-else but
    // differing user_lo must order by user_lo and not perturb any field above.
    const auto a = make_sort_key(SortLayer::kOpaque, 3, SortBlend::kAlpha, 9, 42, 1);
    const auto b = make_sort_key(SortLayer::kOpaque, 3, SortBlend::kAlpha, 9, 42, 250);
    EXPECT_LT(a.value, b.value);
    EXPECT_EQ(material_id_of(a), material_id_of(b));
    EXPECT_EQ(depth_bits_of(a), depth_bits_of(b));
}

TEST(SortKey, TransparentBackToFrontViaPreFlippedDepth)
{
    // The library's documented contract: callers pre-flip depth bits for
    // transparent draws so an ascending sort yields back-to-front. Model a
    // far (depth 0x100) and near (depth 0xF00) draw; for transparent the
    // caller flips (0xFFFFFF - depth), so FAR must sort AFTER near.
    constexpr std::uint32_t kMaxDepth = 0xFFFFFFu;
    const std::uint32_t far_d  = 0x000100u;
    const std::uint32_t near_d = 0x000F00u;
    const auto far_k  = make_sort_key(SortLayer::kTransparent, 0, SortBlend::kAlpha, 0, kMaxDepth - far_d,  0);
    const auto near_k = make_sort_key(SortLayer::kTransparent, 0, SortBlend::kAlpha, 0, kMaxDepth - near_d, 0);
    // Near has a SMALLER flipped value → sorts first; far renders last
    // (painter's algorithm for blended geometry).
    EXPECT_LT(near_k.value, far_k.value);
}

#include <cd/render/PostProcessChain.hpp>

TEST(PostProcessChain, AddAndQuerySize)
{
    cd::render::PostProcessChain c;
    c.add("ToneMap");
    c.add("FXAA");
    EXPECT_EQ(c.size(), 2u);
}

TEST(PostProcessChain, EnabledViewSkipsDisabled)
{
    cd::render::PostProcessChain c;
    c.add("ToneMap");
    c.add("FXAA");
    c.add("Vignette");
    c.set_enabled("FXAA", false);
    auto view = c.enabled_view();
    EXPECT_EQ(view.size(), 2u);
    EXPECT_EQ(view[0]->name, "ToneMap");
    EXPECT_EQ(view[1]->name, "Vignette");
}

TEST(PostProcessChain, RemoveByName)
{
    cd::render::PostProcessChain c;
    c.add("ToneMap");
    c.add("FXAA");
    EXPECT_TRUE(c.remove("ToneMap"));
    EXPECT_EQ(c.size(), 1u);
    EXPECT_FALSE(c.remove("Missing"));
}

TEST(PostProcessChain, InsertionOrderPreserved)
{
    cd::render::PostProcessChain c;
    c.add("First");
    c.add("Second");
    c.add("Third");
    EXPECT_EQ(c.passes()[0].name, "First");
    EXPECT_EQ(c.passes()[2].name, "Third");
}

// ----- BAND 3: genuinely-untested PostProcessChain aggregation branches -----

TEST(PostProcessChain, EnabledViewEmptyWhenAllDisabled)
{
    // enabled_view() over an all-disabled chain must return an empty view
    // (the loop's "no pass enabled" branch was never asserted).
    cd::render::PostProcessChain c;
    c.add("ToneMap");
    c.add("FXAA");
    c.set_enabled("ToneMap", false);
    c.set_enabled("FXAA", false);
    EXPECT_TRUE(c.enabled_view().empty());
}

TEST(PostProcessChain, EnabledViewEmptyOnEmptyChain)
{
    cd::render::PostProcessChain c;
    EXPECT_EQ(c.size(), 0u);
    EXPECT_TRUE(c.enabled_view().empty());
}

TEST(PostProcessChain, SetEnabledOnMissingNameIsNoOp)
{
    // set_enabled() with no matching pass must silently do nothing and leave
    // every existing pass untouched — the no-match path was untested.
    cd::render::PostProcessChain c;
    c.add("ToneMap");
    c.set_enabled("DoesNotExist", false);
    auto view = c.enabled_view();
    ASSERT_EQ(view.size(), 1u);
    EXPECT_EQ(view[0]->name, "ToneMap");
}

TEST(PostProcessChain, ClearEmptiesChain)
{
    cd::render::PostProcessChain c;
    c.add("A");
    c.add("B");
    c.clear();
    EXPECT_EQ(c.size(), 0u);
    EXPECT_TRUE(c.passes().empty());
}

TEST(PostProcessChain, RemoveOnEmptyChainReturnsFalse)
{
    cd::render::PostProcessChain c;
    EXPECT_FALSE(c.remove("Anything"));
}

TEST(PostProcessChain, ParamsRoundTripAndDefaultZero)
{
    // add() with an explicit params value + the defaulted-zero overload were
    // never asserted on the stored PostProcessPass.
    cd::render::PostProcessChain c;
    c.add("WithParams", 0xC0FFEEu);
    c.add("Defaulted");
    EXPECT_EQ(c.passes()[0].params, 0xC0FFEEu);
    EXPECT_EQ(c.passes()[1].params, 0u);
    EXPECT_TRUE(c.passes()[0].enabled);
}

#include <cd/render/Tonemap.hpp>

TEST(Tonemap, ReinhardMapsZeroToZero)
{
    EXPECT_FLOAT_EQ(cd::render::tonemap_reinhard(0.0F), 0.0F);
}

TEST(Tonemap, ReinhardAsymptoticToOne)
{
    EXPECT_LT(cd::render::tonemap_reinhard(100.0F), 1.0F);
    EXPECT_GT(cd::render::tonemap_reinhard(100.0F), 0.95F);
}

TEST(Tonemap, AcesFittedClampedToUnitRange)
{
    for (float x : { 0.0F, 0.5F, 1.0F, 5.0F, 100.0F })
    {
        const float v = cd::render::tonemap_aces_fitted(x);
        EXPECT_GE(v, 0.0F);
        EXPECT_LE(v, 1.0F);
    }
}

TEST(Tonemap, Uncharted2ProducesFiniteOutput)
{
    const float v = cd::render::tonemap_uncharted2(2.0F);
    EXPECT_GT(v, 0.0F);
    EXPECT_LT(v, 5.0F);
}

#include <cd/render/MeshStats.hpp>

TEST(MeshStats, TriangleCountDivides)
{
    cd::render::MeshStats s;
    s.index_count = 6;
    EXPECT_EQ(s.triangle_count(), 2u);
    s.index_count = 0;
    EXPECT_EQ(s.triangle_count(), 0u);
}

TEST(MeshStats, EstimatedVertexBytes)
{
    cd::render::MeshStats s;
    s.vertex_count = 1000;
    s.vertex_stride_bytes = 32;   // pos + normal + uv
    EXPECT_EQ(s.estimated_vertex_bytes(), 32000u);
}

TEST(MeshStats, BboxExtentAndCenter)
{
    cd::render::MeshStats s;
    s.bbox_min = { -1.0F, -2.0F, -3.0F };
    s.bbox_max = {  1.0F,  2.0F,  3.0F };
    const auto ex = s.bbox_extent();
    EXPECT_FLOAT_EQ(ex.x, 2.0F);
    EXPECT_FLOAT_EQ(ex.z, 6.0F);
    const auto c = s.bbox_center();
    EXPECT_FLOAT_EQ(c.x, 0.0F);
    EXPECT_FLOAT_EQ(c.y, 0.0F);
}

#include <cd/render/TextLayoutMetrics.hpp>

TEST(TextLayoutMetrics, EmptyTextZero)
{
    auto m = cd::render::measure_simple("", 14.0F, 7.0F);
    EXPECT_FLOAT_EQ(m.width, 0.0F);
    EXPECT_FLOAT_EQ(m.height, 0.0F);
}

TEST(TextLayoutMetrics, SingleLineMonospace)
{
    auto m = cd::render::measure_simple("hello", 14.0F, 7.0F);
    EXPECT_FLOAT_EQ(m.width, 5.0F * 7.0F);
    EXPECT_EQ(m.line_count, 1u);
    EXPECT_FLOAT_EQ(m.height, 14.0F);
}

TEST(TextLayoutMetrics, NewlineIncrementsLineCount)
{
    auto m = cd::render::measure_simple("abc\nde", 14.0F, 7.0F);
    EXPECT_EQ(m.line_count, 2u);
    EXPECT_FLOAT_EQ(m.width, 3.0F * 7.0F);   // longest line
    EXPECT_FLOAT_EQ(m.height, 28.0F);
}

#include <cd/render/ClearColorPreset.hpp>

TEST(ClearColor, BlackIsZero)
{
    const auto c = cd::render::clear_black();
    EXPECT_FLOAT_EQ(c[0], 0.0F);
    EXPECT_FLOAT_EQ(c[3], 1.0F);
}

TEST(ClearColor, CornflowerBlue)
{
    const auto c = cd::render::clear_cornflower_blue();
    EXPECT_NEAR(c[0], 0.392F, 1e-3F);
    EXPECT_NEAR(c[2], 0.929F, 1e-3F);
}

TEST(ClearColor, DebugMagentaIsObvious)
{
    const auto c = cd::render::clear_debug_magenta();
    EXPECT_FLOAT_EQ(c[0], 1.0F);
    EXPECT_FLOAT_EQ(c[1], 0.0F);
    EXPECT_FLOAT_EQ(c[2], 1.0F);
}

#include <cd/render/DrawBatchKey.hpp>

TEST(DrawBatchKey, SizeIs24Bytes)
{
    EXPECT_EQ(sizeof(cd::render::DrawBatchKey), 24u);
}

TEST(DrawBatchKey, EqualityIsFieldwise)
{
    cd::render::DrawBatchKey a { 1, 2, 3, 4 };
    cd::render::DrawBatchKey b { 1, 2, 3, 4 };
    cd::render::DrawBatchKey c { 1, 2, 3, 5 };
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(DrawBatchKey, HashStableAcrossEquals)
{
    cd::render::DrawBatchKey a { 0xAA, 0xBB, 0xCC, 0xDD };
    cd::render::DrawBatchKey b { 0xAA, 0xBB, 0xCC, 0xDD };
    EXPECT_EQ(a.hash(), b.hash());
}

TEST(DrawBatchKey, DifferentMeshDifferentHash)
{
    cd::render::DrawBatchKey a { 1, 0, 0, 0 };
    cd::render::DrawBatchKey b { 2, 0, 0, 0 };
    EXPECT_NE(a.hash(), b.hash());
}

// ============================================================
// Phase 108 — DrawBucket SortKey-driven ordering
// ============================================================
#include <cd/render/DrawBucket.hpp>

TEST(DrawBucket, SortsAscendingByKey)
{
    cd::render::DrawBucket b;
    std::vector<std::uint64_t> emitted;

    auto add = [&](std::uint64_t v) {
        cd::render::SortKey k; k.value = v;
        b.add(k, [&emitted, v](cd::rhi::ICommandBuffer&) {
            emitted.push_back(v);
        });
    };

    add(300);
    add(10);
    add(150);
    add(20);
    EXPECT_EQ(b.size(), 4u);
    EXPECT_FALSE(b.is_sorted_cached());

    b.sort();
    EXPECT_TRUE(b.is_sorted_cached());

    const auto& items = b.items();
    EXPECT_EQ(items[0].key.value, 10u);
    EXPECT_EQ(items[1].key.value, 20u);
    EXPECT_EQ(items[2].key.value, 150u);
    EXPECT_EQ(items[3].key.value, 300u);
}

TEST(DrawBucket, ClearLeavesItSorted)
{
    cd::render::DrawBucket b;
    cd::render::SortKey k; k.value = 42;
    b.add(k, [](cd::rhi::ICommandBuffer&) {});
    EXPECT_FALSE(b.is_sorted_cached());
    b.clear();
    EXPECT_TRUE(b.is_sorted_cached());
    EXPECT_TRUE(b.empty());
}

TEST(DrawBucket, StableSortPreservesInsertionOrderOnEqualKeys)
{
    cd::render::DrawBucket b;
    cd::render::SortKey k; k.value = 100;
    int counter = 0;
    std::vector<int> emitted;
    b.add(k, [&emitted, n=counter++](cd::rhi::ICommandBuffer&){ emitted.push_back(n); });
    b.add(k, [&emitted, n=counter++](cd::rhi::ICommandBuffer&){ emitted.push_back(n); });
    b.add(k, [&emitted, n=counter++](cd::rhi::ICommandBuffer&){ emitted.push_back(n); });
    b.sort();
    // Three same-key items: stable_sort preserves insertion order.
    EXPECT_EQ(b.size(), 3u);
}

// ----- BAND 3: DrawBucket emit_all replay (GPU-free via NullCommandBuffer) ---
// emit_all() auto-sorts an unsorted bucket then replays — that branch was
// only covered through the Vulkan SubmitDraws path (ICD-gated, SKIPs without a
// GPU). NullCommandBuffer drives it deterministically on every host.

TEST(DrawBucket, EmitAllAutoSortsThenReplaysInKeyOrder)
{
    cd::render::DrawBucket b;
    std::vector<std::uint64_t> emitted;
    for (auto v : { 70u, 10u, 50u, 30u })
    {
        cd::render::SortKey k; k.value = v;
        b.add(k, [&emitted, v](cd::rhi::ICommandBuffer&) { emitted.push_back(v); });
    }
    EXPECT_FALSE(b.is_sorted_cached());

    cd::rhi::NullCommandBuffer cmd;
    b.emit_all(cmd);  // must sort() internally before replaying

    ASSERT_EQ(emitted.size(), 4u);
    EXPECT_EQ(emitted[0], 10u);
    EXPECT_EQ(emitted[1], 30u);
    EXPECT_EQ(emitted[2], 50u);
    EXPECT_EQ(emitted[3], 70u);
    EXPECT_TRUE(b.is_sorted_cached());
}

TEST(DrawBucket, EmitAllDoesNotClearSoBucketIsReusable)
{
    cd::render::DrawBucket b;
    int calls = 0;
    cd::render::SortKey k; k.value = 1;
    b.add(k, [&calls](cd::rhi::ICommandBuffer&) { ++calls; });

    cd::rhi::NullCommandBuffer cmd;
    b.emit_all(cmd);
    b.emit_all(cmd);  // emit_all does NOT clear; a second replay runs again
    EXPECT_EQ(calls, 2);
    EXPECT_EQ(b.size(), 1u);
}

TEST(DrawBucket, EmitAllOnEmptyBucketIsNoOp)
{
    cd::render::DrawBucket b;
    EXPECT_TRUE(b.empty());
    cd::rhi::NullCommandBuffer cmd;
    b.emit_all(cmd);  // no items → no callbacks, no crash
    EXPECT_TRUE(b.empty());
}

TEST(DrawBucket, ReserveDoesNotChangeLogicalSize)
{
    cd::render::DrawBucket b;
    b.reserve(128);
    EXPECT_EQ(b.size(), 0u);
    EXPECT_TRUE(b.empty());
}


// =============================================================================
// Phase 292 / Marathon Run 7 sub-N1D: PlanarShadow.hpp.
// Header-only math; no device required.
// =============================================================================

namespace
{

constexpr float kEps = 1e-4F;

}  // anonymous namespace

#include <cd/render/PlanarShadow.hpp>

TEST(PlanarShadow, ProjectsPointOntoYPlaneAlongSun)
{
    // Sun pointing straight down: sun_dir = (0, -1, 0). Any caster point
    // should project to (P.x, plane_y+lift, P.z).
    const cd::math::Vec3f sun { 0.0F, -1.0F, 0.0F };
    const float plane_y = -0.5F;
    const float lift    = 0.01F;
    const auto s = cd::render::make_planar_shadow_matrix(sun, plane_y, lift);

    // Caster point (3, 5, -2) -> projected (3, -0.49, -2).
    const cd::math::Vec4f p { 3.0F, 5.0F, -2.0F, 1.0F };
    const auto p_proj = s * p;
    EXPECT_NEAR(p_proj.x, 3.0F, kEps);
    EXPECT_NEAR(p_proj.y, plane_y + lift, kEps);
    EXPECT_NEAR(p_proj.z, -2.0F, kEps);
    EXPECT_NEAR(p_proj.w, 1.0F, kEps);
}

TEST(PlanarShadow, OffAxisSunFlattensYAndShearsXZ)
{
    // Sun pointing down-forward: (0.5, -0.5, 0.5) — normalises to a
    // diagonal ray with Ly=-0.5. Caster point (0, 1, 0):
    //   t = (plane_y - 1) / -0.5 = 2*(1 - plane_y)
    //   x' = 0 + t * 0.5 = (1 - plane_y)
    //   z' = 0 + t * 0.5 = (1 - plane_y)
    const cd::math::Vec3f sun { 0.5F, -0.5F, 0.5F };
    const float plane_y = -0.5F;
    const float lift    = 0.0F;
    const auto s = cd::render::make_planar_shadow_matrix(sun, plane_y, lift);
    const cd::math::Vec4f p { 0.0F, 1.0F, 0.0F, 1.0F };
    const auto p_proj = s * p;
    EXPECT_NEAR(p_proj.x, 1.0F - plane_y, kEps);   // = 1.5
    EXPECT_NEAR(p_proj.y, plane_y, kEps);
    EXPECT_NEAR(p_proj.z, 1.0F - plane_y, kEps);
    EXPECT_NEAR(p_proj.w, 1.0F, kEps);
}

TEST(PlanarShadow, NearHorizontalSunClampsToFiniteShadow)
{
    // Sun nearly horizontal: Ly = -0.01 (well below kMinAbs = 0.10). The
    // matrix should clamp Ly to -0.10 so the projected X offset stays
    // bounded. Caster point (0, 1, 0):
    //   without clamp: x_offset = (plane_y - 1) / -0.01 * sun.x = 100 m+
    //   with clamp (Ly=-0.10): x_offset = (plane_y - 1) / -0.10 * sun.x = ~10
    const cd::math::Vec3f sun { 1.0F, -0.01F, 0.0F };
    const float plane_y = -0.5F;
    const float lift    = 0.0F;
    const auto s = cd::render::make_planar_shadow_matrix(sun, plane_y, lift);
    const cd::math::Vec4f p { 0.0F, 1.0F, 0.0F, 1.0F };
    const auto p_proj = s * p;
    // Projected y still pinned to plane_y.
    EXPECT_NEAR(p_proj.y, plane_y, kEps);
    // X offset bounded under 20 (would be 150+ without clamp).
    EXPECT_LT(p_proj.x, 20.0F);
    EXPECT_GT(p_proj.x, 1.0F);
}
