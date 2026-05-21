// =============================================================================
// CHROMODYNAMIC — samples/hello_mesh/main.cpp
//
// Builds on hello_triangle by adding real vertex + index buffers and a
// custom vertex input layout. Renders a single RGBA quad (two triangles)
// from explicit per-vertex {position, color} data — the gateway demo for
// any non-trivial geometry pipeline (mesh, glTF, scene graph).
// =============================================================================
#include <cd/material/Material.hpp>
#include <cd/platform/Window.hpp>
#include <cd/render/Renderer.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi_vulkan/VulkanDevice.hpp>
#include <cd/shader/Compiler.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <vector>

namespace
{

struct Vertex
{
    float pos[2];
    float color[3];
};

// Quad as two triangles sharing two vertices.
//   0 ─── 1
//   │  ╲  │
//   3 ─── 2
constexpr std::array<Vertex, 4> kVertices {
    {
     { { -0.6F, -0.6F }, { 1.0F, 0.0F, 0.0F } },  // 0 top-left      — red
        { { 0.6F, -0.6F }, { 0.0F, 1.0F, 0.0F } },   // 1 top-right     — green
        { { 0.6F, 0.6F }, { 0.0F, 0.0F, 1.0F } },    // 2 bottom-right  — blue
        { { -0.6F, 0.6F }, { 1.0F, 1.0F, 0.0F } },   // 3 bottom-left   — yellow
    }
};

constexpr std::array<std::uint16_t, 6> kIndices { 0, 1, 2, 0, 2, 3 };

constexpr const char* kVS = R"glsl(
#version 450
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec3 in_color;
layout(location = 0) out vec3 v_color;
void main() {
  gl_Position = vec4(in_pos, 0.0, 1.0);
  v_color = in_color;
}
)glsl";

constexpr const char* kFS = R"glsl(
#version 450
layout(location = 0) in  vec3 v_color;
layout(location = 0) out vec4 out_color;
void main() { out_color = vec4(v_color, 1.0); }
)glsl";

/// Upload a CPU-side blob into a host-visible RHI buffer in a single call.
/// Returns the buffer handle; the caller owns the destroy.
[[nodiscard]] cd::rhi::BufferHandle
make_upload_buffer(cd::rhi::IDevice& dev, std::span<const std::byte> bytes, cd::rhi::BufferUsage usage)
{
    cd::rhi::BufferDesc bd {};
    bd.size = bytes.size();
    bd.usage = usage;
    bd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto r = dev.create_buffer(bd);
    if (!r.has_value())
        return {};
    if (auto u = dev.upload_buffer(*r, 0, bytes); !u.has_value())
    {
        dev.destroy_buffer(*r);
        return {};
    }
    return *r;
}

}  // namespace

int main()
{
    cd::platform::WindowDesc wd {};
    wd.title = "CHROMODYNAMIC — hello_mesh (quad from vertex+index buffers)";
    wd.width = 800;
    wd.height = 600;
    auto window_r = cd::platform::create_window(wd);
    if (!window_r.has_value())
    {
        std::fprintf(
            stderr,
            "create_window failed: %.*s\n",
            static_cast<int>(window_r.error().message.size()),
            window_r.error().message.data()
        );
        return 1;
    }
    auto& window = **window_r;

    cd::rhi_vulkan::VulkanCreateInfo vci {};
    auto device_r = cd::rhi_vulkan::create_vulkan_device(vci);
    if (!device_r.has_value())
    {
        std::fprintf(
            stderr,
            "create_vulkan_device failed: %.*s\n",
            static_cast<int>(device_r.error().message.size()),
            device_r.error().message.data()
        );
        return 2;
    }
    auto& device = **device_r;

    cd::render::RendererDesc rd {};
    rd.device = &device;
    rd.swapchain.window_handle = window.native_window_handle();
    rd.swapchain.display_handle = window.native_display_handle();
    rd.swapchain.extent = { window.width(), window.height() };
    rd.swapchain.format = cd::rhi::Format::kBGRA8Unorm;
    rd.frames_in_flight = 2;
    auto renderer_r = cd::render::Renderer::create(rd);
    if (!renderer_r.has_value())
    {
        std::fprintf(
            stderr,
            "Renderer::create failed: %.*s\n",
            static_cast<int>(renderer_r.error().message.size()),
            renderer_r.error().message.data()
        );
        return 3;
    }
    auto& renderer = *renderer_r;

    // ---- Buffers -----------------------------------------------------------
    const std::span<const std::byte> vb_bytes { reinterpret_cast<const std::byte*>(kVertices.data()),
                                                kVertices.size() * sizeof(Vertex) };
    const std::span<const std::byte> ib_bytes { reinterpret_cast<const std::byte*>(kIndices.data()),
                                                kIndices.size() * sizeof(std::uint16_t) };
    auto vb = make_upload_buffer(device, vb_bytes, cd::rhi::BufferUsage::kVertex);
    auto ib = make_upload_buffer(device, ib_bytes, cd::rhi::BufferUsage::kIndex);
    if (!vb.is_valid() || !ib.is_valid())
    {
        std::fprintf(stderr, "buffer upload failed\n");
        return 4;
    }

    // ---- Material with explicit vertex input ------------------------------
    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr)
    {
        std::fprintf(stderr, "engine built without CD_ENABLE_GLSLANG\n");
        return 5;
    }
    constexpr std::array<cd::rhi::VertexBinding, 1> kBindings {
        cd::rhi::VertexBinding { .binding = 0, .stride = sizeof(Vertex), .per_instance = false }
    };
    constexpr std::array<cd::rhi::VertexAttribute, 2> kAttrs {
        cd::rhi::VertexAttribute { .location = 0,
                                  .binding = 0,
                                  .format = cd::rhi::Format::kRG32Float,
                                  .offset = offsetof(Vertex, pos)   },
        cd::rhi::VertexAttribute { .location = 1,
                                  .binding = 0,
                                  .format = cd::rhi::Format::kRGB32Float,
                                  .offset = offsetof(Vertex, color) }
    };
    constexpr std::array<cd::rhi::Format, 1> kColorFormats { cd::rhi::Format::kBGRA8Unorm };

    cd::material::MaterialDesc md {};
    md.vertex_glsl = kVS;
    md.fragment_glsl = kFS;
    md.vertex_bindings = kBindings;
    md.vertex_attributes = kAttrs;
    md.color_attachment_formats = kColorFormats;
    md.topology = cd::rhi::PrimitiveTopology::kTriangleList;
    md.raster.cull = cd::rhi::CullMode::kNone;
    md.name = "mesh_quad";
    auto material_r = cd::material::Material::create(device, compiler.get(), md);
    if (!material_r.has_value())
    {
        std::fprintf(
            stderr,
            "Material::create failed: %.*s\n",
            static_cast<int>(material_r.error().message.size()),
            material_r.error().message.data()
        );
        return 6;
    }
    auto& material = *material_r;

    std::printf("hello_mesh: ready. ESC or close to exit.\n");
    std::fflush(stdout);

    // ---- Main loop ---------------------------------------------------------
    std::vector<cd::platform::OSEvent> events;
    events.reserve(64);
    bool needs_rebuild = false;
    auto rebuild = [&]
    {
        if (window.width() == 0 || window.height() == 0)
            return false;
        auto r = renderer.recreate_swapchain({ window.width(), window.height() });
        if (!r.has_value())
            return false;
        needs_rebuild = false;
        return true;
    };

    while (true)
    {
        events.clear();
        if (!window.pump_events(events))
            break;
        for (const auto& e : events)
        {
            if (e.kind == cd::platform::OSEventKind::kKeyDown && e.key == cd::platform::KeyCode::kEscape)
            {
                window.request_close();
            }
            else if (e.kind == cd::platform::OSEventKind::kResize)
            {
                needs_rebuild = true;
            }
        }
        if (needs_rebuild && !rebuild())
            continue;

        auto frame_r = renderer.begin_frame();
        if (!frame_r.has_value())
        {
            if (frame_r.error().code ==
                static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            {
                needs_rebuild = true;
                continue;
            }
            std::fprintf(
                stderr,
                "begin_frame: %.*s\n",
                static_cast<int>(frame_r.error().message.size()),
                frame_r.error().message.data()
            );
            return 7;
        }
        auto& frame = *frame_r;
        auto& cmd = *frame.command_buffer;

        std::array<cd::rhi::ColorAttachmentInfo, 1> color_attach {
            cd::rhi::ColorAttachmentInfo {
                                          .view = frame.swapchain_image_view,
                                          .load_op = cd::rhi::LoadOp::kClear,
                                          .store_op = cd::rhi::StoreOp::kStore,
                                          .clear_color = { .f32 = { 0.08F, 0.10F, 0.14F, 1.0F } },
                                          }
        };
        cd::rhi::RenderPassBeginInfo rp {};
        rp.render_area = cd::rhi::Rect2D {
            { 0, 0 },
            frame.extent
        };
        rp.color_attachments = color_attach;
        cmd.begin_render_pass(rp);
        cmd.set_viewport(
            cd::rhi::Viewport { 0.0F,
                                0.0F,
                                static_cast<float>(frame.extent.width),
                                static_cast<float>(frame.extent.height),
                                0.0F,
                                1.0F }
        );
        cmd.set_scissor(
            cd::rhi::Rect2D {
                { 0, 0 },
                frame.extent
        }
        );

        material.apply(cmd);
        cmd.bind_vertex_buffer(0, vb, 0);
        cmd.bind_index_buffer(ib, 0, cd::rhi::IndexType::kUInt16);
        cmd.draw_indexed(
            static_cast<std::uint32_t>(kIndices.size()),
            /*instance_count=*/1,
            /*first_index=*/0,
            /*vertex_offset=*/0,
            /*first_instance=*/0
        );
        cmd.end_render_pass();

        auto end_r = renderer.end_frame();
        if (!end_r.has_value())
        {
            if (end_r.error().code == static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            {
                needs_rebuild = true;
                continue;
            }
            std::fprintf(
                stderr,
                "end_frame: %.*s\n",
                static_cast<int>(end_r.error().message.size()),
                end_r.error().message.data()
            );
            return 8;
        }
    }

    renderer.wait_idle();
    device.destroy_buffer(ib);
    device.destroy_buffer(vb);
    std::printf("hello_mesh: clean exit.\n");
    return 0;
}
