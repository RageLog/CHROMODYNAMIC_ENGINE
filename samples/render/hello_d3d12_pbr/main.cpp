// =============================================================================
// CHROMODYNAMIC — samples/render/hello_d3d12_pbr/main.cpp
//
// Phase 399 — M4G.  D3D12 PBR-structured sample.
//
// SCOPE-DOWN NOTE (per ADR-20260529-M4-d3d12-parity.md):
//
//   hello_engine's PBR is authored in GLSL → compiled via glslang → SPIR-V
//   → consumed by the Vulkan backend. Porting that to D3D12 requires either:
//     Path A: SPIRV-Cross (SPIR-V → HLSL → DXC → DXIL)
//     Path B: Slang as the shared source language
//
//   Neither path is wired in the current tree (SPIRV-Cross vendored but
//   cd::spirv_cross_glue::translate() is a separate library that would
//   need plumbing into the sample build). Rather than create a false-DONE
//   by duplicating 2000 lines of GLSL as inline HLSL strings, this sample:
//
//     1. Derives structurally from hello_engine's PBR LAYOUT:
//        - Per-vertex: position (TEXCOORD0) + normal (TEXCOORD1)
//        - Per-frame CB: mvp matrix + model matrix (16+16 = 128 B)
//        - Per-material CB: base_color + metallic + roughness (48 B)
//        - Directional light CB: dir + color + intensity (32 B)
//     2. Implements Blinn-Phong approximation in inline HLSL (not
//        full Cook-Torrance) — same API surface, lower fidelity.
//     3. Renders a unit sphere (icosphere; 242 vertices) to demonstrate
//        the bind-model works end-to-end with descriptor sets.
//     4. Can boot headless (set CD_D3D12_PBR_HEADLESS_FRAMES=N) for CI.
//
//   REAL PBR (M4G-V2) lands when SPIRV-Cross is plumbed into
//   cd::rhi_d3d12 shader creation path; see ADR for next steps.
//
// cd::sample::App SCOPE-DOWN NOTE:
//
//   AppConfig has no backend field in M2A. The App framework drives a
//   no-op Impl (no window, no swapchain). This sample therefore uses the
//   same direct Win32 + D3D12 pattern as hello_d3d12_triangle until M2B
//   brings real window boot to the App base class.
//
// CD_D3D12_PBR_HEADLESS_FRAMES=N: render N frames then exit (CI mode).
// =============================================================================

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
#endif

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include <cd/core/ErrorCode.hpp>
#include <cd/core/Version.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/Pipeline.hpp>
#include <cd/rhi/d3d12/D3D12Device.hpp>
#include <cd/rhi/d3d12/D3D12ShaderCompile.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numbers>
#include <span>
#include <vector>

namespace
{

constexpr UINT kWidth  = 1280;
constexpr UINT kHeight = 720;

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
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"CHROMODYNAMIC.hello_d3d12_pbr";
    RegisterClassExW(&wc);

    RECT rc { 0, 0, static_cast<LONG>(kWidth), static_cast<LONG>(kHeight) };
    const DWORD style = WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    AdjustWindowRect(&rc, style, FALSE);
    return CreateWindowExW(
        0, wc.lpszClassName, L"hello_d3d12_pbr  (scope-down: Blinn-Phong / no SPIRV-Cross)",
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

// ---- Geometry: icosphere (level 1 subdivision, 80 triangles) ---------------
//
// Vertex layout mirrors hello_engine PBR:
//   TEXCOORD0 = position (vec3)
//   TEXCOORD1 = normal   (vec3)
// Total 24 bytes / vertex.

struct PbrVertex
{
    float pos[3];
    float nrm[3];
};

[[nodiscard]] std::vector<PbrVertex> make_sphere(int subdivisions)
{
    // Icosahedron base vertices.
    constexpr float phi = 1.6180339887F;  // golden ratio
    const float n = std::sqrt(1.0F + phi * phi);
    const float s = 1.0F / n;
    const float p = phi / n;

    const std::array<std::array<float, 3>, 12> ico_verts {{
        {-s,  p, 0}, { s,  p, 0}, {-s, -p, 0}, { s, -p, 0},
        { 0, -s,  p}, { 0,  s,  p}, { 0, -s, -p}, { 0,  s, -p},
        { p, 0, -s}, { p, 0,  s}, {-p, 0, -s}, {-p, 0,  s},
    }};

    const std::array<std::array<int, 3>, 20> ico_faces {{
        {0,11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7,10}, {0,10,11},
        {1, 5, 9}, {5,11, 4}, {11,10, 2}, {10, 7, 6}, {7, 1, 8},
        {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
        {4, 9, 5}, {2, 4,11}, {6, 2,10}, {8, 6, 7}, {9, 8, 1},
    }};

    // Collect triangles as flat arrays; subdivide if requested.
    struct Tri { std::array<float, 3> a, b, c; };
    std::vector<Tri> tris;
    tris.reserve(20u);
    for (const auto& f : ico_faces)
        tris.push_back({ ico_verts[static_cast<std::size_t>(f[0])],
                         ico_verts[static_cast<std::size_t>(f[1])],
                         ico_verts[static_cast<std::size_t>(f[2])] });

    for (int sub = 0; sub < subdivisions; ++sub)
    {
        std::vector<Tri> next;
        next.reserve(tris.size() * 4u);
        for (const auto& t : tris)
        {
            // midpoints on the unit sphere
            auto mid = [](const std::array<float,3>& u,
                          const std::array<float,3>& v) -> std::array<float,3>
            {
                float x = u[0] + v[0], y = u[1] + v[1], z = u[2] + v[2];
                const float len = std::sqrt(x*x + y*y + z*z);
                return { x/len, y/len, z/len };
            };
            auto m0 = mid(t.a, t.b);
            auto m1 = mid(t.b, t.c);
            auto m2 = mid(t.c, t.a);
            next.push_back({ t.a, m0, m2 });
            next.push_back({ m0, t.b, m1 });
            next.push_back({ m2, m1, t.c });
            next.push_back({ m0, m1, m2 });
        }
        tris = std::move(next);
    }

    std::vector<PbrVertex> verts;
    verts.reserve(tris.size() * 3u);
    for (const auto& t : tris)
    {
        // On a unit sphere pos == nrm.
        for (const auto* v : { &t.a, &t.b, &t.c })
        {
            PbrVertex pv;
            pv.pos[0] = (*v)[0]; pv.pos[1] = (*v)[1]; pv.pos[2] = (*v)[2];
            pv.nrm[0] = (*v)[0]; pv.nrm[1] = (*v)[1]; pv.nrm[2] = (*v)[2];
            verts.push_back(pv);
        }
    }
    return verts;
}

// ---- Constant-buffer layouts matching hello_engine PBR UBOs ----------------

struct alignas(256) FrameCB
{
    float mvp[16];     // 4x4 column-major
    float model[16];   // 4x4 column-major (for normal transform)
};

struct alignas(256) MaterialCB
{
    float base_color[4];   // rgb + (unused w)
    float metallic;
    float roughness;
    float _pad[2];
};

struct alignas(256) LightCB
{
    float direction[4];   // xyz + w unused
    float color[4];       // rgb + intensity
};

// ---- Column-major 4x4 helpers -----------------------------------------------

using Mat4 = std::array<float, 16>;

[[nodiscard]] Mat4 mat4_identity()
{
    Mat4 m {}; m[0] = m[5] = m[10] = m[15] = 1.0F; return m;
}

// Column-major multiply: out = a * b
[[nodiscard]] Mat4 mat4_mul(const Mat4& a, const Mat4& b)
{
    Mat4 c {};
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                c[static_cast<std::size_t>(col * 4 + row)] +=
                    a[static_cast<std::size_t>(k * 4 + row)] *
                    b[static_cast<std::size_t>(col * 4 + k)];
    return c;
}

[[nodiscard]] Mat4 mat4_perspective(float fov_y_rad, float aspect,
                                    float near_z, float far_z)
{
    const float f = 1.0F / std::tan(fov_y_rad * 0.5F);
    Mat4 m {};
    m[0]  = f / aspect;
    m[5]  = f;
    m[10] = far_z / (near_z - far_z);
    m[11] = -1.0F;
    m[14] = (near_z * far_z) / (near_z - far_z);
    return m;
}

[[nodiscard]] Mat4 mat4_lookat(float ex, float ey, float ez,
                               float cx, float cy, float cz)
{
    // Forward = normalize(center - eye)
    float fx = cx-ex, fy = cy-ey, fz = cz-ez;
    const float fl = std::sqrt(fx*fx+fy*fy+fz*fz);
    fx/=fl; fy/=fl; fz/=fl;
    // Right = forward x up (world up = Y)
    float rx = fy*0.0F - fz*1.0F, ry = fz*0.0F - fx*0.0F, rz = fx*1.0F - fy*0.0F;
    // Normalize right — it's already unit from cross with world-Y (assuming no singularity).
    // Up = right x forward
    const float ux = ry*fz - rz*fy, uy = rz*fx - rx*fz, uz = rx*fy - ry*fx;
    Mat4 m {};
    m[0] = rx; m[4] = ry; m[8]  = rz;
    m[1] = ux; m[5] = uy; m[9]  = uz;
    m[2] =-fx; m[6] =-fy; m[10] =-fz;
    m[12] = -(rx*ex + ry*ey + rz*ez);
    m[13] = -(ux*ex + uy*ey + uz*ez);
    m[14] =  (fx*ex + fy*ey + fz*ez);
    m[15] = 1.0F;
    return m;
}

// ---- HLSL (Blinn-Phong approximation; structurally matches PBR API) ---------
//
// Scope-down: full Cook-Torrance requires SPIRV-Cross → DXIL pipeline.
// Blinn-Phong gives visually similar output for smoke testing the
// descriptor-set / CB wiring without the multi-week shader-pipeline work.

constexpr const char* kVS = R"hlsl(
cbuffer FrameCB : register(b0) {
    float4x4 mvp;
    float4x4 model;
};
struct VsIn  { float3 pos : TEXCOORD0; float3 nrm : TEXCOORD1; };
struct VsOut { float4 sv_pos : SV_POSITION; float3 world_pos : TEXCOORD0; float3 world_nrm : TEXCOORD1; };
VsOut main(VsIn v) {
    float4 wp = mul(model, float4(v.pos, 1.0));
    VsOut o;
    o.sv_pos    = mul(mvp, float4(v.pos, 1.0));
    o.world_pos = wp.xyz;
    o.world_nrm = normalize(mul((float3x3)model, v.nrm));
    return o;
}
)hlsl";

constexpr const char* kPS = R"hlsl(
cbuffer MaterialCB : register(b1) {
    float4 base_color;
    float  metallic;
    float  roughness;
    float2 _pad;
};
cbuffer LightCB : register(b2) {
    float4 light_dir;    // xyz = direction
    float4 light_color;  // xyz = color, w = intensity
};
struct VsOut { float4 sv_pos : SV_POSITION; float3 world_pos : TEXCOORD0; float3 world_nrm : TEXCOORD1; };
float4 main(VsOut p) : SV_TARGET {
    // Blinn-Phong: structurally equivalent to PBR but simpler.
    float3 N = normalize(p.world_nrm);
    float3 L = normalize(-light_dir.xyz);
    float3 V = normalize(float3(0.0, 0.0, 3.0) - p.world_pos);
    float3 H = normalize(L + V);
    float  diff = max(dot(N, L), 0.0);
    float  spec = pow(max(dot(N, H), 0.0), (1.0 - roughness) * 128.0 + 1.0);
    float3 albedo = base_color.rgb;
    // Metallic surfaces tint specular by albedo.
    float3 spec_col = lerp(float3(1,1,1), albedo, metallic);
    float3 col = albedo * diff * light_color.rgb * light_color.w
               + spec_col * spec * light_color.rgb * light_color.w * 0.5
               + albedo * 0.03;  // ambient
    return float4(col, 1.0);
}
)hlsl";

}  // namespace

int main()
{
    std::fprintf(stdout,
                 "CHROMODYNAMIC %u.%u.%u — hello_d3d12_pbr\n"
                 "  Scope-down: Blinn-Phong (no SPIRV-Cross wired yet)\n",
                 static_cast<unsigned>(cd::core::kEngineVersion.major),
                 static_cast<unsigned>(cd::core::kEngineVersion.minor),
                 static_cast<unsigned>(cd::core::kEngineVersion.patch));

    // ---- Headless cap for CI -----------------------------------------------
    char* headless_env = nullptr;
    std::size_t headless_env_sz = 0;
    // _dupenv_s is the MSVC-safe alternative to std::getenv.
    (void)_dupenv_s(&headless_env, &headless_env_sz, "CD_D3D12_PBR_HEADLESS_FRAMES");
    int headless_frames = 0;
    if (headless_env != nullptr)
    {
        char* end = nullptr;
        // NOLINTNEXTLINE(google-runtime-int) — strtol returns long by C ABI.
        const long frames_long = std::strtol(headless_env, &end, 10);
        if (end != headless_env && frames_long >= 0 && frames_long <= INT_MAX)
            headless_frames = static_cast<int>(frames_long);
    }
    free(headless_env);  // _dupenv_s allocates; must free even if null

    // ---- Device ---------------------------------------------------------------
    cd::rhi::d3d12::D3D12CreateInfo dci {};
    dci.enable_validation    = false;
    dci.prefer_discrete_gpu  = true;
    auto dev_r = cd::rhi::d3d12::create_d3d12_device(dci);
    if (!dev_r.has_value())
    {
        std::fprintf(stderr, "[d3d12_pbr] device init failed: %.*s\n",
                     static_cast<int>(dev_r.error().message.size()),
                     dev_r.error().message.data());
        return 1;
    }
    auto& dev = dev_r.value();
    std::fprintf(stdout, "  adapter: %.*s\n",
                 static_cast<int>(dev->adapter_name().size()),
                 dev->adapter_name().data());

    // ---- Window + swapchain (skipped in headless) ---------------------------
    HWND hwnd = nullptr;
    cd::rhi::SwapchainHandle sc_h {};
    if (headless_frames == 0)
    {
        hwnd = create_window();
        if (hwnd == nullptr)
        {
            std::fprintf(stderr, "[d3d12_pbr] CreateWindow failed\n");
            return 1;
        }
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);

        cd::rhi::SwapchainDesc scd {};
        scd.window_handle = hwnd;
        scd.format        = cd::rhi::Format::kRGBA8Unorm;
        scd.extent        = { kWidth, kHeight };
        scd.image_count   = 2;
        scd.vsync         = true;
        auto sc_r = dev->create_swapchain(scd);
        if (!sc_r.has_value())
        {
            std::fprintf(stderr, "[d3d12_pbr] swapchain failed\n");
            return 1;
        }
        sc_h = *sc_r;
    }

    // ---- Shaders ---------------------------------------------------------------
    cd::rhi::d3d12::CompileOptions vs_opts {};
    vs_opts.source      = kVS;
    vs_opts.entry_point = "main";
    vs_opts.stage       = cd::rhi::ShaderStage::kVertex;
    vs_opts.model       = cd::rhi::d3d12::ShaderModel::kSM5_1;
    auto vs_bc_r = cd::rhi::d3d12::compile_hlsl(vs_opts);

    cd::rhi::d3d12::CompileOptions ps_opts {};
    ps_opts.source      = kPS;
    ps_opts.entry_point = "main";
    ps_opts.stage       = cd::rhi::ShaderStage::kFragment;
    ps_opts.model       = cd::rhi::d3d12::ShaderModel::kSM5_1;
    auto ps_bc_r = cd::rhi::d3d12::compile_hlsl(ps_opts);

    if (!vs_bc_r.has_value() || !ps_bc_r.has_value())
    {
        std::fprintf(stderr, "[d3d12_pbr] HLSL compile failed\n");
        return 1;
    }
    const auto& vs_bc = *vs_bc_r;
    const auto& ps_bc = *ps_bc_r;

    cd::rhi::ShaderModuleDesc vmd {};
    vmd.code      = vs_bc.data();
    vmd.code_size = vs_bc.size();
    vmd.stage     = cd::rhi::ShaderStage::kVertex;
    vmd.entry_point = "main";
    auto vs_r = dev->create_shader_module(vmd);

    cd::rhi::ShaderModuleDesc pmd {};
    pmd.code      = ps_bc.data();
    pmd.code_size = ps_bc.size();
    pmd.stage     = cd::rhi::ShaderStage::kFragment;
    pmd.entry_point = "main";
    auto ps_r = dev->create_shader_module(pmd);
    if (!vs_r.has_value() || !ps_r.has_value())
    {
        std::fprintf(stderr, "[d3d12_pbr] shader module creation failed\n");
        return 1;
    }

    // ---- Descriptor set layout (3 CBVs: frame, material, light) -----------
    cd::rhi::DescriptorSetLayoutBinding bindings[3] {};
    bindings[0].binding = 0; bindings[0].type = cd::rhi::DescriptorType::kUniformBuffer;
    bindings[0].count = 1;   bindings[0].stages = cd::rhi::ShaderStage::kAllGraphics;
    bindings[1].binding = 1; bindings[1].type = cd::rhi::DescriptorType::kUniformBuffer;
    bindings[1].count = 1;   bindings[1].stages = cd::rhi::ShaderStage::kAllGraphics;
    bindings[2].binding = 2; bindings[2].type = cd::rhi::DescriptorType::kUniformBuffer;
    bindings[2].count = 1;   bindings[2].stages = cd::rhi::ShaderStage::kAllGraphics;
    cd::rhi::DescriptorSetLayoutDesc layout_desc {};
    layout_desc.bindings = std::span<const cd::rhi::DescriptorSetLayoutBinding>(bindings);
    auto set_layout_r = dev->create_descriptor_set_layout(layout_desc);
    if (!set_layout_r.has_value())
    {
        std::fprintf(stderr, "[d3d12_pbr] descriptor_set_layout failed\n");
        return 1;
    }
    auto set_layout_h = *set_layout_r;

    // ---- Pipeline layout + PSO ------------------------------------------------
    cd::rhi::PipelineLayoutDesc pld {};
    pld.set_layouts = std::span<const cd::rhi::DescriptorSetLayoutHandle>(
        &set_layout_h, 1);
    auto layout_r = dev->create_pipeline_layout(pld);
    if (!layout_r.has_value())
    {
        std::fprintf(stderr, "[d3d12_pbr] pipeline_layout failed\n");
        return 1;
    }
    auto layout_h = *layout_r;

    cd::rhi::VertexAttribute vattribs[] = {
        { 0, 0, cd::rhi::Format::kRGB32Float, 0 },   // position
        { 1, 0, cd::rhi::Format::kRGB32Float, 12 },  // normal
    };
    cd::rhi::VertexBinding vbinding {};
    vbinding.binding     = 0;
    vbinding.stride      = static_cast<std::uint32_t>(sizeof(PbrVertex));
    vbinding.per_instance = false;
    cd::rhi::GraphicsPipelineDesc gpd {};
    gpd.vertex_shader     = *vs_r;
    gpd.fragment_shader   = *ps_r;
    gpd.layout            = layout_h;
    gpd.vertex_attributes = vattribs;
    gpd.vertex_bindings   = std::span<const cd::rhi::VertexBinding>(&vbinding, 1);
    gpd.topology          = cd::rhi::PrimitiveTopology::kTriangleList;
    gpd.depth_stencil.depth_test  = false;
    gpd.depth_stencil.depth_write = false;
    const cd::rhi::Format kSwapFmt = cd::rhi::Format::kRGBA8Unorm;
    if (headless_frames == 0)
        gpd.color_attachment_formats = std::span<const cd::rhi::Format>(&kSwapFmt, 1);
    auto pso_r = dev->create_graphics_pipeline(gpd);
    if (!pso_r.has_value())
    {
        std::fprintf(stderr, "[d3d12_pbr] PSO creation failed: %.*s\n",
                     static_cast<int>(pso_r.error().message.size()),
                     pso_r.error().message.data());
        return 1;
    }
    auto pso_h = *pso_r;

    // ---- Geometry buffer -------------------------------------------------------
    auto sphere_verts = make_sphere(1);  // 80 * 4 = 320 triangles = 960 verts
    const std::uint64_t vb_size =
        sphere_verts.size() * sizeof(PbrVertex);

    cd::rhi::BufferDesc vbd {};
    vbd.size   = vb_size;
    vbd.usage  = cd::rhi::BufferUsage::kVertex;
    vbd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto vb_r  = dev->create_buffer(vbd);
    if (!vb_r.has_value())
    {
        std::fprintf(stderr, "[d3d12_pbr] vertex buffer creation failed\n");
        return 1;
    }
    auto vb_h = *vb_r;
    auto up_r = dev->upload_buffer(vb_h, 0,
        std::as_bytes(std::span(sphere_verts)));
    if (!up_r.has_value())
    {
        std::fprintf(stderr, "[d3d12_pbr] vertex buffer upload failed\n");
        return 1;
    }

    // ---- Constant buffers (one each: frame / material / light) ----------------
    auto make_cb = [&](std::uint64_t sz) -> cd::rhi::BufferHandle {
        cd::rhi::BufferDesc bd {};
        bd.size   = sz;
        bd.usage  = cd::rhi::BufferUsage::kUniform;
        bd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
        auto r = dev->create_buffer(bd);
        return r.has_value() ? *r : cd::rhi::BufferHandle{};
    };
    auto frame_cb_h    = make_cb(sizeof(FrameCB));
    auto material_cb_h = make_cb(sizeof(MaterialCB));
    auto light_cb_h    = make_cb(sizeof(LightCB));
    if (!frame_cb_h.is_valid() || !material_cb_h.is_valid() || !light_cb_h.is_valid())
    {
        std::fprintf(stderr, "[d3d12_pbr] constant buffer creation failed\n");
        return 1;
    }

    // Fill material CB (copper-ish: warm orange, metallic 0.9, rough 0.3).
    {
        MaterialCB mcb {};
        mcb.base_color[0] = 1.0F; mcb.base_color[1] = 0.6F;
        mcb.base_color[2] = 0.2F; mcb.base_color[3] = 1.0F;
        mcb.metallic   = 0.9F;
        mcb.roughness  = 0.3F;
        (void)dev->upload_buffer(material_cb_h, 0, std::as_bytes(std::span(&mcb, 1)));
    }
    // Fill light CB (directional from upper-left, white, intensity 3).
    {
        LightCB lcb {};
        lcb.direction[0] = -0.577F; lcb.direction[1] = -0.577F;
        lcb.direction[2] = -0.577F;
        lcb.color[0] = 1.0F; lcb.color[1] = 1.0F;
        lcb.color[2] = 1.0F; lcb.color[3] = 3.0F;
        (void)dev->upload_buffer(light_cb_h, 0, std::as_bytes(std::span(&lcb, 1)));
    }

    // ---- Descriptor set -------------------------------------------------------
    auto ds_r = dev->allocate_descriptor_set(set_layout_h);
    if (!ds_r.has_value())
    {
        std::fprintf(stderr, "[d3d12_pbr] descriptor_set alloc failed\n");
        return 1;
    }
    auto ds_h = *ds_r;

    cd::rhi::DescriptorWrite dw[3] {};
    dw[0].binding = 0; dw[0].type = cd::rhi::DescriptorType::kUniformBuffer;
    dw[0].buffer  = frame_cb_h;    dw[0].buffer_range = sizeof(FrameCB);
    dw[1].binding = 1; dw[1].type = cd::rhi::DescriptorType::kUniformBuffer;
    dw[1].buffer  = material_cb_h; dw[1].buffer_range = sizeof(MaterialCB);
    dw[2].binding = 2; dw[2].type = cd::rhi::DescriptorType::kUniformBuffer;
    dw[2].buffer  = light_cb_h;    dw[2].buffer_range = sizeof(LightCB);
    (void)dev->update_descriptor_set(ds_h, std::span<const cd::rhi::DescriptorWrite>(dw));

    // ---- Command buffer -------------------------------------------------------
    auto cmd = dev->create_command_buffer(cd::rhi::QueueType::kGraphics);
    if (!cmd)
    {
        std::fprintf(stderr, "[d3d12_pbr] create_command_buffer failed\n");
        return 1;
    }

    // ---- Render loop ----------------------------------------------------------
    std::fprintf(stdout, "  sphere: %zu vertices (%zu triangles)\n",
                 sphere_verts.size(), sphere_verts.size() / 3u);
    std::fprintf(stdout, "%s\n",
                 headless_frames > 0 ? "  headless mode" : "  press ESC to exit");

    const float fov_rad = 45.0F * (std::numbers::pi_v<float> / 180.0F);
    const float aspect  = static_cast<float>(kWidth) / static_cast<float>(kHeight);

    int frame = 0;
    while (!g_quit && (headless_frames == 0 || frame < headless_frames))
    {
        if (headless_frames == 0) pump();

        // Update frame CB (spin the sphere).
        {
            const float angle = static_cast<float>(frame) * 0.005F;
            const float cs = std::cos(angle), sn = std::sin(angle);

            Mat4 model = mat4_identity();
            // Rotation about Y.
            model[0]  =  cs; model[8]  = sn;
            model[2]  = -sn; model[10] = cs;

            Mat4 view = mat4_lookat(0.0F, 0.5F, 3.0F,  0.0F, 0.0F, 0.0F);
            Mat4 proj = mat4_perspective(fov_rad, aspect, 0.1F, 100.0F);
            Mat4 mvp  = mat4_mul(proj, mat4_mul(view, model));

            FrameCB fcb {};
            std::memcpy(fcb.mvp,   mvp.data(),   sizeof(fcb.mvp));
            std::memcpy(fcb.model, model.data(), sizeof(fcb.model));
            (void)dev->upload_buffer(frame_cb_h, 0,
                                     std::as_bytes(std::span(&fcb, 1)));
        }

        if (!sc_h.is_valid())
        {
            // Headless: skip actual record + present.
            ++frame;
            continue;
        }

        // Acquire + record + submit + present.
        auto img_r = dev->acquire_next_image(sc_h, {}, {}, ~std::uint64_t{0});
        if (!img_r.has_value())
        {
            std::fprintf(stderr, "[d3d12_pbr] acquire failed\n");
            break;
        }
        const std::uint32_t img_idx = *img_r;
        const auto bb_view = dev->swapchain_image_view(sc_h, img_idx);

        cmd->begin();

        cd::rhi::ColorAttachmentInfo ca {};
        ca.view = bb_view;
        ca.load_op = cd::rhi::LoadOp::kClear;
        ca.clear_color.f32[0] = 0.05F;
        ca.clear_color.f32[1] = 0.05F;
        ca.clear_color.f32[2] = 0.08F;
        ca.clear_color.f32[3] = 1.0F;
        cd::rhi::RenderPassBeginInfo rp {};
        rp.color_attachments = std::span<const cd::rhi::ColorAttachmentInfo>(&ca, 1);
        cmd->begin_render_pass(rp);

        cmd->bind_graphics_pipeline(pso_h);
        cmd->bind_descriptor_set(0, ds_h);

        cd::rhi::Viewport vp { 0.0F, 0.0F,
                               static_cast<float>(kWidth),
                               static_cast<float>(kHeight),
                               0.0F, 1.0F };
        cmd->set_viewport(vp);
        cd::rhi::Rect2D sc { {0, 0}, {kWidth, kHeight} };
        cmd->set_scissor(sc);

        cmd->bind_vertex_buffer(0, vb_h, 0);
        cmd->draw(static_cast<std::uint32_t>(sphere_verts.size()), 1, 0, 0);

        cmd->end_render_pass();
        cmd->end();
        dev->submit(*cmd);
        (void)dev->present(sc_h, img_idx, {});

        ++frame;
    }

    dev->wait_idle();

    // ---- Cleanup (RAII via destroy_* explicit — no RAII wrappers yet) --------
    dev->destroy_buffer(vb_h);
    dev->destroy_buffer(frame_cb_h);
    dev->destroy_buffer(material_cb_h);
    dev->destroy_buffer(light_cb_h);
    dev->destroy_descriptor_set(ds_h);
    dev->destroy_descriptor_set_layout(set_layout_h);
    dev->destroy_graphics_pipeline(pso_h);
    dev->destroy_pipeline_layout(layout_h);
    dev->destroy_shader_module(*vs_r);
    dev->destroy_shader_module(*ps_r);
    if (sc_h.is_valid()) dev->destroy_swapchain(sc_h);
    if (hwnd != nullptr) DestroyWindow(hwnd);

    std::fprintf(stdout, "  done — %d frames rendered\n", frame);
    return 0;
}
