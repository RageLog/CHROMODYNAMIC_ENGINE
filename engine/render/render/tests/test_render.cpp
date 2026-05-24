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

#include <cd/render/Renderer.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi_vulkan/VulkanDevice.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

namespace
{

std::unique_ptr<cd::rhi::IDevice> try_make_device()
{
    cd::rhi_vulkan::VulkanCreateInfo info {};
    info.enable_validation = false;
    auto r = cd::rhi_vulkan::create_vulkan_device(info);
    if (!r.has_value())
        return nullptr;
    return std::move(*r);
}

#define SKIP_IF_NO_VULKAN(dev_var)    \
    auto dev_var = try_make_device(); \
    if (!dev_var)                     \
    GTEST_SKIP() << "no Vulkan ICD available on this host"

#if defined(_WIN32)
class HiddenWindow
{
public:
    HiddenWindow()
    {
        instance_ = GetModuleHandleW(nullptr);
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
    SKIP_IF_NO_VULKAN(dev);
    cd::render::RendererDesc d {};
    d.device = dev.get();
    d.swapchain.extent = { 0, 0 };
    auto r = cd::render::Renderer::create(d);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::render::render_errors::Code::kInvalidArgument));
}

TEST(Renderer, RejectsZeroFramesInFlight)
{
    SKIP_IF_NO_VULKAN(dev);
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
    SKIP_IF_NO_VULKAN(dev);
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
    SKIP_IF_NO_VULKAN(dev);
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
    SKIP_IF_NO_VULKAN(dev);
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
    SKIP_IF_NO_VULKAN(dev);
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
    SKIP_IF_NO_VULKAN(dev);
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

TEST(Renderer, ThreeFrameLoop)
{
    // Exercises the frames_in_flight ring: with 2 frames in flight, the third
    // begin_frame must successfully reuse slot 0 (which has been drained by
    // the fence wait inside begin_frame).
    SKIP_IF_NO_VULKAN(dev);
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
    using namespace cd::render;
    const auto opaque = make_sort_key(SortLayer::kOpaque, 0, SortBlend::kOff, 0, 0, 0);
    const auto ui     = make_sort_key(SortLayer::kUi,     0, SortBlend::kOff, 0, 0, 0);
    EXPECT_LT(opaque.value, ui.value);
}

TEST(SortKey, MaterialIdRecoverable)
{
    using namespace cd::render;
    const std::uint32_t mat = 0x123456u;
    const auto k = make_sort_key(SortLayer::kOpaque, 0, SortBlend::kOff, mat, 0, 0);
    EXPECT_EQ(material_id_of(k), mat);
}

TEST(SortKey, DepthBitsRecoverable)
{
    using namespace cd::render;
    const std::uint32_t depth = 0xABCDEFu;
    const auto k = make_sort_key(SortLayer::kOpaque, 0, SortBlend::kOff, 0, depth, 0);
    EXPECT_EQ(depth_bits_of(k), depth);
}

TEST(SortKey, AscendingSortGroupsByLayerThenMaterialThenDepth)
{
    using namespace cd::render;
    const auto a = make_sort_key(SortLayer::kOpaque, 0, SortBlend::kOff, 5, 100, 0);
    const auto b = make_sort_key(SortLayer::kOpaque, 0, SortBlend::kOff, 5, 200, 0);
    const auto c = make_sort_key(SortLayer::kOpaque, 0, SortBlend::kOff, 6,   0, 0);
    EXPECT_LT(a.value, b.value);
    EXPECT_LT(b.value, c.value);
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
