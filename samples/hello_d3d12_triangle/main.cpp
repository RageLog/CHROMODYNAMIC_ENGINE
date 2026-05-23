// =============================================================================
// CHROMODYNAMIC — samples/hello_d3d12_triangle
//
// v0.36.0 / Phase 14.C — first D3D12 sample that *draws* (not just clears).
//
// Builds a 640x360 Win32 window, opens a flip-model swapchain, compiles
// an inline HLSL VS+PS via cd::rhi_d3d12::compile_hlsl, creates an
// ID3D12PipelineState with a position+color input layout, uploads a
// 3-vertex buffer with one triangle, and renders it through the cd::rhi
// abstraction's standard begin/begin_render_pass/draw/end loop.
//
// Mirrors hello_triangle (Vulkan) at the engine-API level — the same
// IDevice / ICommandBuffer calls produce equivalent output on the
// D3D12 backend. Closes the Phase 14.C "D3D12 backend has draw"
// milestone.
//
// CD_D3D12_TRIANGLE_HEADLESS_FRAMES=N caps the loop for CI.
// =============================================================================
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Version.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/Pipeline.hpp>
#include <cd/rhi_d3d12/D3D12Device.hpp>
#include <cd/rhi_d3d12/D3D12ShaderCompile.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{

constexpr UINT kWidth  = 640;
constexpr UINT kHeight = 360;

bool g_quit = false;

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg)
    {
        case WM_CLOSE:
        case WM_DESTROY:
            g_quit = true;
            PostQuitMessage(0);
            return 0;
        case WM_KEYDOWN:
            if (w == VK_ESCAPE)
            {
                g_quit = true;
                PostQuitMessage(0);
            }
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, w, l);
    }
}

[[nodiscard]] HWND create_window()
{
    WNDCLASSEXW wc {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"CHROMODYNAMIC.hello_d3d12_triangle";
    RegisterClassExW(&wc);

    RECT rc { 0, 0, static_cast<LONG>(kWidth), static_cast<LONG>(kHeight) };
    const DWORD style = WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    AdjustWindowRect(&rc, style, FALSE);
    return CreateWindowExW(
        0, wc.lpszClassName, L"hello_d3d12_triangle",
        style, CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, wc.hInstance, nullptr);
}

void pump()
{
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

// 3 vertices, position (xyz) + color (rgb), tightly packed (24 bytes).
struct Vertex
{
    float x, y, z;
    float r, g, b;
};

constexpr std::array<Vertex, 3> kTriangleVerts {{
    {  0.0F,  0.6F, 0.0F, 1.0F, 0.0F, 0.0F },
    {  0.6F, -0.6F, 0.0F, 0.0F, 1.0F, 0.0F },
    { -0.6F, -0.6F, 0.0F, 0.0F, 0.0F, 1.0F },
}};

constexpr const char* kVertexHLSL = R"(
struct VsIn  { float3 pos : TEXCOORD0; float3 col : TEXCOORD1; };
struct VsOut { float4 pos : SV_POSITION;  float3 col : COLOR; };
VsOut main(VsIn vin)
{
    VsOut o;
    o.pos = float4(vin.pos, 1.0);
    o.col = vin.col;
    return o;
}
)";

constexpr const char* kFragmentHLSL = R"(
struct VsOut { float4 pos : SV_POSITION; float3 col : COLOR; };
float4 main(VsOut psin) : SV_TARGET
{
    return float4(psin.col, 1.0);
}
)";

}  // namespace

int main()
{
    std::fprintf(stdout,
                 "CHROMODYNAMIC %u.%u.%u — hello_d3d12_triangle\n",
                 static_cast<unsigned>(cd::core::kEngineVersion.major),
                 static_cast<unsigned>(cd::core::kEngineVersion.minor),
                 static_cast<unsigned>(cd::core::kEngineVersion.patch));

    // ---- Device + window + swapchain ---------------------------------------
    cd::rhi_d3d12::D3D12CreateInfo dci {};
    dci.enable_validation = false;
    auto dev_r = cd::rhi_d3d12::create_d3d12_device(dci);
    if (!dev_r.has_value())
    {
        std::fprintf(stderr, "[d3d12] device init failed: %.*s\n",
                     static_cast<int>(dev_r.error().message.size()),
                     dev_r.error().message.data());
        return 1;
    }
    auto& device = **dev_r;
    const auto adapter = device.adapter_name();
    std::fprintf(stdout, "[d3d12] adapter=%.*s\n",
                 static_cast<int>(adapter.size()), adapter.data());

    HWND hwnd = create_window();
    if (!hwnd) return 1;
    ShowWindow(hwnd, SW_SHOWNORMAL);

    cd::rhi::SwapchainDesc sd {};
    sd.window_handle = hwnd;
    sd.display_handle = GetModuleHandleW(nullptr);
    sd.extent = { kWidth, kHeight };
    sd.image_count = 2;
    sd.format = cd::rhi::Format::kRGBA8Unorm;
    sd.vsync = true;
    auto swap_r = device.create_swapchain(sd);
    if (!swap_r.has_value()) return 2;
    const auto swap = *swap_r;

    // ---- Compile shaders ---------------------------------------------------
    cd::rhi_d3d12::CompileOptions vs_opts {};
    vs_opts.source = kVertexHLSL;
    vs_opts.entry_point = "main";
    vs_opts.stage = cd::rhi::ShaderStage::kVertex;
    vs_opts.source_name = "triangle.vs.hlsl";
    auto vs_blob = cd::rhi_d3d12::compile_hlsl(vs_opts);
    if (!vs_blob.has_value())
    {
        std::fprintf(stderr, "[d3d12] VS compile failed: %.*s\n",
                     static_cast<int>(vs_blob.error().message.size()),
                     vs_blob.error().message.data());
        return 3;
    }

    cd::rhi_d3d12::CompileOptions ps_opts {};
    ps_opts.source = kFragmentHLSL;
    ps_opts.entry_point = "main";
    ps_opts.stage = cd::rhi::ShaderStage::kFragment;
    ps_opts.source_name = "triangle.ps.hlsl";
    auto ps_blob = cd::rhi_d3d12::compile_hlsl(ps_opts);
    if (!ps_blob.has_value())
    {
        std::fprintf(stderr, "[d3d12] PS compile failed: %.*s\n",
                     static_cast<int>(ps_blob.error().message.size()),
                     ps_blob.error().message.data());
        return 4;
    }
    std::fprintf(stdout, "[d3d12] shaders compiled (VS=%zu B, PS=%zu B)\n",
                 vs_blob->size(), ps_blob->size());

    // ---- Shader modules ----------------------------------------------------
    cd::rhi::ShaderModuleDesc vs_md {};
    vs_md.stage = cd::rhi::ShaderStage::kVertex;
    vs_md.code = vs_blob->data();
    vs_md.code_size = vs_blob->size();
    auto vs_h_r = device.create_shader_module(vs_md);
    if (!vs_h_r.has_value()) return 5;

    cd::rhi::ShaderModuleDesc ps_md {};
    ps_md.stage = cd::rhi::ShaderStage::kFragment;
    ps_md.code = ps_blob->data();
    ps_md.code_size = ps_blob->size();
    auto ps_h_r = device.create_shader_module(ps_md);
    if (!ps_h_r.has_value()) return 6;

    // ---- Pipeline layout (empty root signature) ----------------------------
    cd::rhi::PipelineLayoutDesc pld {};
    auto layout_r = device.create_pipeline_layout(pld);
    if (!layout_r.has_value()) return 7;

    // ---- Graphics pipeline -------------------------------------------------
    std::array<cd::rhi::VertexBinding, 1> bindings {{
        { /*binding=*/0, /*stride=*/sizeof(Vertex), /*per_instance=*/false }
    }};
    std::array<cd::rhi::VertexAttribute, 2> attrs {{
        { /*location=*/0, /*binding=*/0, cd::rhi::Format::kRGB32Float, /*offset=*/0 },
        { /*location=*/1, /*binding=*/0, cd::rhi::Format::kRGB32Float,
          /*offset=*/sizeof(float) * 3 },
    }};
    std::array<cd::rhi::Format, 1> color_formats { cd::rhi::Format::kRGBA8Unorm };

    cd::rhi::GraphicsPipelineDesc gpd {};
    gpd.layout = *layout_r;
    gpd.vertex_shader = *vs_h_r;
    gpd.fragment_shader = *ps_h_r;
    gpd.vertex_bindings = bindings;
    gpd.vertex_attributes = attrs;
    gpd.topology = cd::rhi::PrimitiveTopology::kTriangleList;
    gpd.raster.cull = cd::rhi::CullMode::kNone;  // single tri, both winding OK
    gpd.depth_stencil.depth_test = false;
    gpd.color_attachment_formats = color_formats;

    auto pipeline_r = device.create_graphics_pipeline(gpd);
    if (!pipeline_r.has_value())
    {
        std::fprintf(stderr, "[d3d12] graphics pipeline failed: %.*s\n",
                     static_cast<int>(pipeline_r.error().message.size()),
                     pipeline_r.error().message.data());
        return 8;
    }
    std::fprintf(stdout, "[d3d12] PSO + root signature OK\n");

    // ---- Vertex buffer (UPLOAD heap, map+memcpy) ---------------------------
    cd::rhi::BufferDesc bd {};
    bd.size = sizeof(kTriangleVerts);
    bd.usage = cd::rhi::BufferUsage::kVertex;
    bd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto vb_r = device.create_buffer(bd);
    if (!vb_r.has_value()) return 9;
    auto up_r = device.upload_buffer(*vb_r, 0,
        std::span<const std::byte> {
            reinterpret_cast<const std::byte*>(kTriangleVerts.data()),
            sizeof(kTriangleVerts)
        });
    if (!up_r.has_value()) return 10;
    std::fprintf(stdout, "[d3d12] vertex buffer uploaded (%zu B)\n",
                 sizeof(kTriangleVerts));

    // ---- Render loop -------------------------------------------------------
#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable : 4996)
#endif
    const char* cap_env = std::getenv("CD_D3D12_TRIANGLE_HEADLESS_FRAMES");
#if defined(__clang__)
    #pragma clang diagnostic pop
#elif defined(_MSC_VER)
    #pragma warning(pop)
#endif
    const std::uint32_t max_frames = (cap_env && *cap_env)
        ? static_cast<std::uint32_t>(std::atoi(cap_env)) : 0u;
    std::uint32_t frame = 0;
    while (!g_quit)
    {
        pump();
        if (g_quit) break;

        auto idx_r = device.acquire_next_image(swap, {}, {}, 0);
        if (!idx_r.has_value()) break;
        const std::uint32_t image_idx = *idx_r;

        auto cb = device.create_command_buffer(cd::rhi::QueueType::kGraphics);
        cb->begin();

        cd::rhi::ColorAttachmentInfo att {};
        att.view = device.swapchain_image_view(swap, image_idx);
        att.load_op = cd::rhi::LoadOp::kClear;
        att.store_op = cd::rhi::StoreOp::kStore;
        att.clear_color.f32[0] = 0.10F;
        att.clear_color.f32[1] = 0.12F;
        att.clear_color.f32[2] = 0.15F;
        att.clear_color.f32[3] = 1.0F;

        cd::rhi::RenderPassBeginInfo rpi {};
        rpi.render_area = { { 0, 0 }, { kWidth, kHeight } };
        rpi.color_attachments = std::span<const cd::rhi::ColorAttachmentInfo> { &att, 1 };
        cb->begin_render_pass(rpi);

        cb->set_viewport(cd::rhi::Viewport {
            0.0F, 0.0F, static_cast<float>(kWidth), static_cast<float>(kHeight),
            0.0F, 1.0F });
        cb->set_scissor(cd::rhi::Rect2D { { 0, 0 }, { kWidth, kHeight } });

        cb->bind_graphics_pipeline(*pipeline_r);
        cb->bind_vertex_buffer(0, *vb_r, 0);
        cb->draw(3, 1, 0, 0);

        cb->end_render_pass();
        cb->end();

        device.submit(*cb);
        (void)device.present(swap, image_idx, {});
        device.wait_idle();

        ++frame;
        if (max_frames > 0 && frame >= max_frames)
        {
            std::fprintf(stdout, "[d3d12] headless cap reached at frame=%u\n", frame);
            break;
        }
    }

    device.destroy_buffer(*vb_r);
    device.destroy_graphics_pipeline(*pipeline_r);
    device.destroy_pipeline_layout(*layout_r);
    device.destroy_shader_module(*vs_h_r);
    device.destroy_shader_module(*ps_h_r);
    device.destroy_swapchain(swap);

    std::fprintf(stdout, "[d3d12] triangle OK — rendered %u frame(s)\n", frame);
    return 0;
}
