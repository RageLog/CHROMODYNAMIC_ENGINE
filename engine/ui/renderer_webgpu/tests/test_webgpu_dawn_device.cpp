// =============================================================================
// CHROMODYNAMIC — cd::ui::renderer_webgpu headless Dawn render+readback test
//
// PROVES the live Dawn UI pipeline (Submitter::create -> RenderPipeline +
// bind groups, record_pass -> SetPipeline/SetBindGroup/DrawIndexed) actually
// RASTERIZES pixels — not just that the API surface compiles.
//
// Flow (no window, no surface, no swap-chain):
//   dawn::native::Instance               (RAII, synchronous event pump)
//     -> EnumerateAdapters               (pick the first real backend adapter)
//       -> Adapter::CreateDevice         (synchronous; null on failure)
//         -> wgpu::Device::Acquire
//   Submitter::create(device)            (builds the real pipeline)
//   offscreen RGBA8 texture (64x64, RenderAttachment | CopySrc)
//   batcher: one white quad in the top-left + one black quad bottom-right
//   upload -> CommandEncoder:
//       BeginRenderPass(clear to known background) -> record_pass -> End
//       CopyTextureToBuffer (256-aligned bytesPerRow)
//   Submit -> map the readback buffer (MapAsync + Instance::WaitAny, no sleep)
//   ASSERT the quad region holds the quad colour (white) and the cleared
//   background elsewhere holds the clear colour — i.e. the pipeline drew.
//
// Anti-flaky: GTEST_SKIP when no adapter/device can be created headlessly;
// the map wait is a fence/Future wait (Instance::WaitAny), never sleep_for.
//
// Only compiled when CD_UI_WEBGPU_HAVE_DAWN == 1 (CMake gate).
// =============================================================================
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/renderer_webgpu/Submitter.hpp>
#include <cd/ui/renderer_webgpu/WgpuContract.hpp>

#include <dawn/native/DawnNative.h>
#include <webgpu/webgpu_cpp.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace rw = cd::ui::renderer_webgpu;
namespace ur = cd::ui::renderer;

namespace
{

constexpr std::uint32_t kRtWidth  = 64U;   // 64 * 4 = 256-byte rows (aligned)
constexpr std::uint32_t kRtHeight = 64U;
constexpr std::uint32_t kBytesPerPixel = 4U;
constexpr std::uint32_t kBytesPerRow   = kRtWidth * kBytesPerPixel;  // 256

/// One RGBA8 texel read back from the mapped buffer.
struct Texel
{
    std::uint8_t r { 0U };
    std::uint8_t g { 0U };
    std::uint8_t b { 0U };
    std::uint8_t a { 0U };
};

/// Collected uncaptured-device errors (validation / OOM / internal).  A
/// non-empty list after the render means the pipeline or pass was invalid —
/// the test asserts it stays empty so we never "pass" on a silent error.
std::vector<std::string> g_device_errors;

/// Try to create a headless device from a native Dawn instance.  Returns a
/// null wgpu::Device when no adapter/device can be created (caller SKIPs).
/// The created device carries an uncaptured-error callback into
/// g_device_errors so validation failures surface in the test.
[[nodiscard]] wgpu::Device make_headless_device(const dawn::native::Instance& instance)
{
    // Prefer a hardware backend, but accept whatever the system offers
    // (Vulkan / D3D12 on Windows CI).  No surface compatibility requested.
    wgpu::RequestAdapterOptions opts {};
    opts.powerPreference = wgpu::PowerPreference::HighPerformance;

    std::vector<dawn::native::Adapter> adapters = instance.EnumerateAdapters(&opts);
    if (adapters.empty())
    {
        // Fall back to an unconstrained enumeration (e.g. CPU/fallback adapter).
        adapters = instance.EnumerateAdapters();
    }

    wgpu::DeviceDescriptor dev_desc {};
    dev_desc.label = "headless_device";
    dev_desc.SetUncapturedErrorCallback(
        [](const wgpu::Device&, wgpu::ErrorType type, wgpu::StringView message) {
            std::string msg(message.data, message.length == WGPU_STRLEN
                                              ? std::strlen(message.data)
                                              : message.length);
            g_device_errors.push_back(
                "[" + std::to_string(static_cast<std::uint32_t>(type)) + "] " + msg);
        });

    for (auto& adapter : adapters)
    {
        WGPUDevice raw = adapter.CreateDevice(&dev_desc);
        if (raw != nullptr)
        {
            return wgpu::Device::Acquire(raw);
        }
    }
    return wgpu::Device {};
}

}  // namespace

// ---------------------------------------------------------------------------
// Headless render + readback: the pipeline must rasterize the UI quad.
// ---------------------------------------------------------------------------
TEST(WebGpuDawnDevice, HeadlessRenderProducesQuadPixels)
{
    g_device_errors.clear();

    // --- Instance + headless device ----------------------------------------
    // Require TimedWaitAny so Instance::WaitAny(future, timeoutNS) can block
    // until the GPU map completes (the default native instance disables it and
    // a timed WaitAny then returns "Timeout waits not supported").
    const std::array<wgpu::InstanceFeatureName, 1U> inst_features {
        wgpu::InstanceFeatureName::TimedWaitAny };
    wgpu::InstanceDescriptor inst_desc {};
    inst_desc.requiredFeatureCount = inst_features.size();
    inst_desc.requiredFeatures     = inst_features.data();

    dawn::native::Instance native_instance(&inst_desc);

    wgpu::Device device = make_headless_device(native_instance);
    if (!device)
    {
        GTEST_SKIP() << "no headless WebGPU adapter/device available on this host";
    }

    // The wgpu::Instance backing this device is what we WaitAny() on.
    const wgpu::Instance instance = device.GetAdapter().GetInstance();
    ASSERT_TRUE(instance) << "device must expose its owning instance";

    // --- Submitter (builds the live pipeline) ------------------------------
    rw::SubmitterCreateInfo info {};
    info.max_vertices = 256U;
    info.max_indices  = 768U;
    info.color_format = rw::texture_format::kRgba8Unorm;  // matches the RT
    info.debug_label  = "headless_readback";

    auto sub_res = rw::Submitter::create(device, info);
    ASSERT_TRUE(sub_res.has_value()) << "Submitter::create must succeed on a real device";
    auto& submitter = *sub_res;
    ASSERT_TRUE(submitter.is_valid());

    // --- Offscreen RGBA8 render target -------------------------------------
    wgpu::TextureDescriptor rt_desc {};
    rt_desc.label         = "headless_rt";
    rt_desc.usage         = wgpu::TextureUsage::RenderAttachment
                          | wgpu::TextureUsage::CopySrc;
    rt_desc.dimension     = wgpu::TextureDimension::e2D;
    rt_desc.size          = wgpu::Extent3D { kRtWidth, kRtHeight, 1U };
    rt_desc.format        = wgpu::TextureFormat::RGBA8Unorm;
    rt_desc.mipLevelCount = 1U;
    rt_desc.sampleCount   = 1U;
    const wgpu::Texture rt = device.CreateTexture(&rt_desc);
    ASSERT_TRUE(rt) << "render-target texture creation failed";
    const wgpu::TextureView rt_view = rt.CreateView();
    ASSERT_TRUE(rt_view);

    // --- Readback buffer (CopyDst | MapRead) -------------------------------
    wgpu::BufferDescriptor rb_desc {};
    rb_desc.label = "headless_readback_buf";
    rb_desc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
    rb_desc.size  = static_cast<std::uint64_t>(kBytesPerRow) * kRtHeight;
    const wgpu::Buffer readback = device.CreateBuffer(&rb_desc);
    ASSERT_TRUE(readback);

    // --- A known UI draw: an opaque-white quad covering the top-left 32x32 --
    ur::DrawBatcher batcher;
    batcher.begin_frame();
    batcher.quad(0.0F, 0.0F, 32.0F, 32.0F, ur::Color::white());
    ASSERT_TRUE(submitter.upload(batcher));
    ASSERT_EQ(submitter.vertex_count(), 4U);
    ASSERT_EQ(submitter.command_count(), 1U);

    // --- Record: clear to opaque-blue, draw the quad, then copy out --------
    const wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
    ASSERT_TRUE(encoder);

    wgpu::RenderPassColorAttachment color {};
    color.view       = rt_view;
    color.loadOp     = wgpu::LoadOp::Clear;
    color.storeOp    = wgpu::StoreOp::Store;
    color.clearValue = wgpu::Color { 0.0, 0.0, 1.0, 1.0 };  // opaque blue

    wgpu::RenderPassDescriptor pass_desc {};
    pass_desc.colorAttachmentCount = 1U;
    pass_desc.colorAttachments     = &color;

    const wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&pass_desc);
    ASSERT_TRUE(pass);
    submitter.record_pass(pass, rw::Extent2D { kRtWidth, kRtHeight });
    pass.End();

    // Copy the rendered texture into the mappable readback buffer.
    wgpu::TexelCopyTextureInfo copy_src {};
    copy_src.texture  = rt;
    copy_src.mipLevel = 0U;
    copy_src.origin   = wgpu::Origin3D { 0U, 0U, 0U };
    copy_src.aspect   = wgpu::TextureAspect::All;

    wgpu::TexelCopyBufferLayout copy_layout {};
    copy_layout.offset       = 0U;
    copy_layout.bytesPerRow  = kBytesPerRow;
    copy_layout.rowsPerImage = kRtHeight;

    wgpu::TexelCopyBufferInfo copy_dst {};
    copy_dst.buffer = readback;
    copy_dst.layout = copy_layout;

    const wgpu::Extent3D copy_extent { kRtWidth, kRtHeight, 1U };
    encoder.CopyTextureToBuffer(&copy_src, &copy_dst, &copy_extent);

    const wgpu::CommandBuffer cmd = encoder.Finish();
    device.GetQueue().Submit(1U, &cmd);

    // --- Map the readback buffer (fence/Future wait, NO sleep) -------------
    bool mapped = false;
    wgpu::MapAsyncStatus map_status = wgpu::MapAsyncStatus::Error;
    const wgpu::Future map_future = readback.MapAsync(
        wgpu::MapMode::Read, 0U, static_cast<std::size_t>(rb_desc.size),
        wgpu::CallbackMode::WaitAnyOnly,
        [&mapped, &map_status](wgpu::MapAsyncStatus status, wgpu::StringView) {
            map_status = status;
            mapped     = (status == wgpu::MapAsyncStatus::Success);
        });

    // Wait synchronously for the GPU work + map to complete.  5 s budget is
    // ~250x a healthy headless submit; a hang becomes a ctest TIMEOUT, not a
    // wedge.  WaitAny pumps the instance's event queue (no sleep_for).
    const wgpu::WaitStatus wait =
        instance.WaitAny(map_future, 5'000'000'000ULL /* ns */);
    ASSERT_EQ(wait, wgpu::WaitStatus::Success) << "MapAsync did not complete";
    ASSERT_TRUE(mapped)
        << "buffer map failed, status=" << static_cast<std::uint32_t>(map_status);

    const void* mapped_ptr =
        readback.GetConstMappedRange(0U, static_cast<std::size_t>(rb_desc.size));
    ASSERT_NE(mapped_ptr, nullptr);

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(rb_desc.size));
    std::memcpy(pixels.data(), mapped_ptr, pixels.size());
    readback.Unmap();

    // No silent validation / OOM / internal error may have fired during the
    // pipeline build, the render pass or the copy.
    EXPECT_TRUE(g_device_errors.empty())
        << "device reported " << g_device_errors.size()
        << " uncaptured error(s); first: "
        << (g_device_errors.empty() ? "" : g_device_errors.front());

    // --- Verify pixels ------------------------------------------------------
    const auto texel_at = [&pixels](std::uint32_t x, std::uint32_t y) -> Texel {
        const std::size_t base =
            static_cast<std::size_t>(y) * kBytesPerRow +
            static_cast<std::size_t>(x) * kBytesPerPixel;
        return Texel {
            pixels.at(base + 0U), pixels.at(base + 1U),
            pixels.at(base + 2U), pixels.at(base + 3U),
        };
    };

    // (1) Centre of the quad (8,8) must be (near-)white — the pipeline drew it.
    const Texel inside = texel_at(8U, 8U);
    EXPECT_GT(inside.r, 200U) << "quad interior should be white (R)";
    EXPECT_GT(inside.g, 200U) << "quad interior should be white (G)";
    EXPECT_GT(inside.b, 200U) << "quad interior should be white (B)";
    EXPECT_GT(inside.a, 200U) << "quad interior should be opaque";

    // (2) A pixel well OUTSIDE the quad (50,50) must still be the clear blue —
    //     proving the draw was scissor/geometry-bounded, not a full clear.
    const Texel outside = texel_at(50U, 50U);
    EXPECT_LT(outside.r, 64U)  << "background should stay near-zero red";
    EXPECT_LT(outside.g, 64U)  << "background should stay near-zero green";
    EXPECT_GT(outside.b, 200U) << "background should retain the blue clear";

    // (3) The interior and the background must DIFFER — the strongest single
    //     assertion that the rasterizer produced quad geometry over the clear.
    const std::array<std::uint8_t, 4U> inside_rgba {
        inside.r, inside.g, inside.b, inside.a };
    const std::array<std::uint8_t, 4U> outside_rgba {
        outside.r, outside.g, outside.b, outside.a };
    EXPECT_NE(inside_rgba, outside_rgba)
        << "rendered quad must differ from the cleared background";
}
