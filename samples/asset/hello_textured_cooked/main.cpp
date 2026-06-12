// =============================================================================
// CHROMODYNAMIC — samples/hello_textured_cooked/main.cpp
//
// Closes the texture cook → load → render loop:
//   1. On first launch, the sample synthesises a 256x256 procedural RGBA
//      image, compresses it to BC7 via cd::asset::image::compress_bc7,
//      and writes the cook_texture .cdtex format to a temp file.
//   2. Loads the .cdtex back through cd::asset::cdtex::load.
//   3. Uploads the raw BC7 blocks into a VK_FORMAT_BC7_UNORM_BLOCK image
//      via staging buffer + copy_buffer_to_image.
//   4. Renders the textured quad with linear sampling.
//
// This is the runtime counterpart to tools/cook_texture: the in-process
// version writes its own .cdtex so CI smoke-test can validate the BC7
// pipeline without pre-checked-in binary assets.
//
// Usage:
//   hello_textured_cooked [<path.cdtex>]
//   hello_textured_cooked --headless 3
// =============================================================================
#include "SampleRuntime.hpp"

#include <cd/asset/cdtex/CdTex.hpp>
#include <cd/asset/image/Bc7.hpp>
#include <cd/material/Material.hpp>
#include <cd/platform/Window.hpp>
#include <cd/render/Renderer.hpp>
#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#include <cd/shader/Compiler.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{

struct Vertex
{
    float pos[2];
    float uv[2];
};

constexpr std::array<Vertex, 4> kVertices {
    {
     { { -0.7F, -0.7F }, { 0.0F, 0.0F } },
     { { 0.7F, -0.7F }, { 1.0F, 0.0F } },
     { { 0.7F, 0.7F }, { 1.0F, 1.0F } },
     { { -0.7F, 0.7F }, { 0.0F, 1.0F } },
     }
};
constexpr std::array<std::uint16_t, 6> kIndices { 0, 1, 2, 0, 2, 3 };

constexpr const char* kVS = R"glsl(
#version 450
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 0) out vec2 v_uv;
void main() {
  gl_Position = vec4(in_pos, 0.0, 1.0);
  v_uv = in_uv;
}
)glsl";

constexpr const char* kFS = R"glsl(
#version 450
layout(set = 0, binding = 0) uniform sampler2D u_tex;
layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 out_color;
void main() { out_color = texture(u_tex, v_uv); }
)glsl";

constexpr std::uint32_t kTexSize = 256;

/// Procedural 256x256 RGBA pattern — sweep + checker blend so BC7 has
/// real gradient + sharp-edge content to compress (vs. a solid block
/// that any encoder gets right trivially).
[[nodiscard]] std::vector<std::uint8_t> make_procedural_rgba()
{
    std::vector<std::uint8_t> px(static_cast<std::size_t>(kTexSize) * kTexSize * 4);
    for (std::uint32_t y = 0; y < kTexSize; ++y)
    {
        for (std::uint32_t x = 0; x < kTexSize; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(kTexSize - 1);
            const float v = static_cast<float>(y) / static_cast<float>(kTexSize - 1);
            const bool dark = ((x >> 5) ^ (y >> 5)) & 1u;
            const auto base = dark ? 50U : 200U;
            const auto i = (y * kTexSize + x) * 4U;
            px[i + 0] = static_cast<std::uint8_t>(base + static_cast<std::uint32_t>(u * 50.0F));
            px[i + 1] = static_cast<std::uint8_t>(base + static_cast<std::uint32_t>(v * 50.0F));
            px[i + 2] = static_cast<std::uint8_t>(base);
            px[i + 3] = 0xFF;
        }
    }
    return px;
}

[[nodiscard]] std::filesystem::path make_self_cooked_texture()
{
    static std::atomic<std::uint64_t> seq { 0 };
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() /  // non-const: returned by move
                      ("cd_textured_cooked_" + std::to_string(static_cast<std::uint64_t>(stamp)) + "_" +
                       std::to_string(seq.fetch_add(1)) + ".cdtex");

    auto rgba = make_procedural_rgba();
    auto bc7 = cd::asset::image::compress_bc7(rgba, kTexSize, kTexSize, cd::asset::image::Bc7Quality::kBalanced);
    if (!bc7.has_value())
        return {};

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.is_open())
        return {};
    auto wu32 = [&](std::uint32_t v)
    {
        const std::uint8_t b[4] { static_cast<std::uint8_t>(v),
                                  static_cast<std::uint8_t>(v >> 8),
                                  static_cast<std::uint8_t>(v >> 16),
                                  static_cast<std::uint8_t>(v >> 24) };
        f.write(reinterpret_cast<const char*>(b), 4);
    };
    auto wu16 = [&](std::uint16_t v)
    {
        const std::uint8_t b[2] { static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v >> 8) };
        f.write(reinterpret_cast<const char*>(b), 2);
    };
    f.write("CDBC7", 5);
    const std::uint8_t ver = 1;
    f.write(reinterpret_cast<const char*>(&ver), 1);
    wu32(kTexSize);
    wu32(kTexSize);
    wu16(static_cast<std::uint16_t>(bc7->block_w));
    wu16(static_cast<std::uint16_t>(bc7->block_h));
    f.write(reinterpret_cast<const char*>(bc7->data.data()), static_cast<std::streamsize>(bc7->data.size()));
    if (!f.good())
        return {};
    return path;
}

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

[[nodiscard]] bool upload_bc7_2d(
    cd::rhi::IDevice& dev,
    cd::rhi::TextureHandle image,
    cd::rhi::BufferHandle staging,
    std::uint32_t w,
    std::uint32_t h
)
{
    auto cmd = dev.create_command_buffer(cd::rhi::QueueType::kGraphics);
    if (cmd == nullptr)
        return false;
    cmd->begin();
    std::array<cd::rhi::TextureBarrier, 1> to_dst {
        cd::rhi::TextureBarrier {
                                 .texture = image,
                                 .from = cd::rhi::ResourceState::kUndefined,
                                 .to = cd::rhi::ResourceState::kTransferDst,
                                 .range = { .base_mip = 0, .mip_count = 1, .base_layer = 0, .layer_count = 1 },
                                 }
    };
    cmd->barrier({}, to_dst);
    std::array<cd::rhi::BufferImageCopyRegion, 1> regions {
        cd::rhi::BufferImageCopyRegion {
                                        .buffer_offset = 0,
                                        .mip_level = 0,
                                        .base_layer = 0,
                                        .layer_count = 1,
                                        .image_offset = { 0, 0, 0 },
                                        .image_extent = { w, h, 1 },
                                        }
    };
    cmd->copy_buffer_to_image(staging, image, regions);
    std::array<cd::rhi::TextureBarrier, 1> to_read {
        cd::rhi::TextureBarrier {
                                 .texture = image,
                                 .from = cd::rhi::ResourceState::kTransferDst,
                                 .to = cd::rhi::ResourceState::kShaderResource,
                                 .range = { .base_mip = 0, .mip_count = 1, .base_layer = 0, .layer_count = 1 },
                                 }
    };
    cmd->barrier({}, to_read);
    cmd->end();
    dev.submit(*cmd);
    dev.wait_idle();
    return true;
}

}  // namespace

int main(int argc, char** argv)
{
    const cd::sample::Runtime runtime = cd::sample::parse_runtime(argc, argv);

    // Pick path: first non-flag argv > self-cooked
    std::string path;
    bool self_cooked = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view a { argv[i] };
        if (a.size() >= 2 && a[0] == '-' && a[1] == '-')
        {
            if (a == "--headless" && i + 1 < argc)
            {
                const char* next = argv[i + 1];
                bool numeric = (next[0] != '\0');
                for (std::size_t k = 0; next[k] != '\0' && numeric; ++k)
                    numeric = (next[k] >= '0' && next[k] <= '9');
                if (numeric)
                    ++i;
            }
            continue;
        }
        path = argv[i];
        break;
    }
    if (path.empty())
    {
        const auto p = make_self_cooked_texture();
        if (p.empty())
        {
            std::fprintf(stderr, "hello_textured_cooked: self-cook failed\n");
            return 1;
        }
        path = p.string();
        self_cooked = true;
        std::printf("hello_textured_cooked: self-cooked %s\n", path.c_str());
    }

    auto tex = cd::asset::cdtex::load(path);
    if (!tex.has_value())
    {
        std::fprintf(
            stderr,
            "hello_textured_cooked: load failed: %.*s\n",
            static_cast<int>(tex.error().message.size()),
            tex.error().message.data()
        );
        return 2;
    }
    std::printf(
        "hello_textured_cooked: loaded %u x %u BC7 (%u x %u blocks, %zu bytes)\n",
        tex->width,
        tex->height,
        tex->block_w,
        tex->block_h,
        tex->blocks.size()
    );
    std::fflush(stdout);

    // ---- Window + device + renderer ---------------------------------------
    cd::platform::WindowDesc wd {};
    wd.title = "CHROMODYNAMIC — hello_textured_cooked (BC7 .cdtex roundtrip)";
    wd.width = 800;
    wd.height = 800;
    auto window_r = cd::platform::create_window(wd);
    if (!window_r.has_value())
        return 3;
    auto& window = **window_r;

    cd::rhi::vulkan::VulkanCreateInfo vci {};
    auto device_r = cd::rhi::vulkan::create_vulkan_device(vci);
    if (!device_r.has_value())
        return 4;
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
        return 5;
    auto& renderer = *renderer_r;

    // ---- Create the BC7 texture + view ------------------------------------
    cd::rhi::TextureDesc td {};
    td.type = cd::rhi::TextureType::k2D;
    td.format = cd::rhi::Format::kBC7Unorm;
    td.extent = { tex->width, tex->height, 1 };
    td.mip_levels = 1;
    td.array_layers = 1;
    td.usage = cd::rhi::TextureUsage::kSampled | cd::rhi::TextureUsage::kTransferDst;
    td.memory = cd::rhi::MemoryUsage::kGpuOnly;
    auto tex_h = device.create_texture(td);
    if (!tex_h.has_value())
    {
        std::fprintf(
            stderr,
            "texture create: %.*s\n",
            static_cast<int>(tex_h.error().message.size()),
            tex_h.error().message.data()
        );
        return 6;
    }
    cd::rhi::TextureViewDesc tvd {};
    tvd.texture = *tex_h;
    tvd.type = cd::rhi::TextureType::k2D;
    tvd.format = cd::rhi::Format::kBC7Unorm;
    tvd.base_mip = 0;
    tvd.mip_count = 1;
    tvd.base_layer = 0;
    tvd.layer_count = 1;
    auto view_h = device.create_texture_view(tvd);
    if (!view_h.has_value())
        return 7;

    cd::rhi::SamplerDesc sd {};
    sd.mag_filter = cd::rhi::SamplerFilter::kLinear;
    sd.min_filter = cd::rhi::SamplerFilter::kLinear;
    sd.mipmap_mode = cd::rhi::SamplerMipmapMode::kLinear;
    sd.address_u = cd::rhi::SamplerAddressMode::kRepeat;
    sd.address_v = cd::rhi::SamplerAddressMode::kRepeat;
    sd.address_w = cd::rhi::SamplerAddressMode::kRepeat;
    auto samp_h = device.create_sampler(sd);
    if (!samp_h.has_value())
        return 8;

    // Staging buffer with the raw BC7 block stream — same layout the GPU
    // wants because Vulkan expects tightly packed compressed blocks.
    const std::span<const std::byte> blk_bytes { reinterpret_cast<const std::byte*>(tex->blocks.data()),
                                                 tex->blocks.size() };
    const auto staging = make_upload_buffer(device, blk_bytes, cd::rhi::BufferUsage::kTransferSrc);
    if (!staging.is_valid())
        return 9;
    if (!upload_bc7_2d(device, *tex_h, staging, tex->width, tex->height))
        return 10;
    device.destroy_buffer(staging);

    // ---- Geometry + material ---------------------------------------------
    const std::span<const std::byte> vb_bytes { reinterpret_cast<const std::byte*>(kVertices.data()),
                                                kVertices.size() * sizeof(Vertex) };
    const std::span<const std::byte> ib_bytes { reinterpret_cast<const std::byte*>(kIndices.data()),
                                                kIndices.size() * sizeof(std::uint16_t) };
    const auto vb = make_upload_buffer(device, vb_bytes, cd::rhi::BufferUsage::kVertex);
    const auto ib = make_upload_buffer(device, ib_bytes, cd::rhi::BufferUsage::kIndex);

    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr)
        return 11;

    constexpr std::array<cd::rhi::VertexBinding, 1> kBindings {
        cd::rhi::VertexBinding { 0, sizeof(Vertex), false }
    };
    constexpr std::array<cd::rhi::VertexAttribute, 2> kAttrs {
        cd::rhi::VertexAttribute { 0, 0, cd::rhi::Format::kRG32Float, offsetof(Vertex, pos) },
        cd::rhi::VertexAttribute { 1, 0, cd::rhi::Format::kRG32Float, offsetof(Vertex, uv)  },
    };
    constexpr std::array<cd::rhi::Format, 1> kColorFormats { cd::rhi::Format::kBGRA8Unorm };
    constexpr std::array<cd::rhi::DescriptorSetLayoutBinding, 1> kDescBindings {
        cd::rhi::DescriptorSetLayoutBinding { .binding = 0,
                                             .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                             .count = 1,
                                             .stages = cd::rhi::ShaderStage::kFragment }
    };

    cd::material::MaterialDesc md {};
    md.vertex_glsl = kVS;
    md.fragment_glsl = kFS;
    md.vertex_bindings = kBindings;
    md.vertex_attributes = kAttrs;
    md.descriptor_bindings = kDescBindings;
    md.color_attachment_formats = kColorFormats;
    md.topology = cd::rhi::PrimitiveTopology::kTriangleList;
    md.raster.cull = cd::rhi::CullMode::kNone;
    md.name = "bc7_quad";
    auto material_r = cd::material::Material::create(device, compiler.get(), md);
    if (!material_r.has_value())
        return 12;
    auto& material = *material_r;

    auto inst_r = cd::material::MaterialInstance::create(device, material);
    if (!inst_r.has_value())
        return 13;
    auto& instance = *inst_r;

    std::array<cd::rhi::DescriptorWrite, 1> writes {
        cd::rhi::DescriptorWrite { .binding = 0,
                                  .array_element = 0,
                                  .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                  .buffer = {},
                                  .buffer_offset = 0,
                                  .buffer_range = 0,
                                  .view = *view_h,
                                  .sampler = *samp_h }
    };
    if (!instance.update(writes).has_value())
        return 14;

    std::printf("hello_textured_cooked: ready. ESC to exit.\n");
    std::fflush(stdout);

    std::vector<cd::platform::OSEvent> events;
    events.reserve(64);
    bool needs_rebuild = false;
    std::uint32_t frame_idx = 0;
    while (true)
    {
        if (!runtime.should_continue(frame_idx))
            window.request_close();
        events.clear();
        if (!window.pump_events(events))
            break;
        for (const auto& e : events)
        {
            if (e.kind == cd::platform::OSEventKind::kKeyDown && e.key == cd::platform::KeyCode::kEscape)
                window.request_close();
            else if (e.kind == cd::platform::OSEventKind::kResize)
                needs_rebuild = true;
        }
        if (needs_rebuild)
        {
            if (window.width() == 0 || window.height() == 0)
                continue;
            if (!renderer.recreate_swapchain({ window.width(), window.height() }).has_value())
                continue;
            needs_rebuild = false;
        }

        auto frame_r = renderer.begin_frame();
        if (!frame_r.has_value())
        {
            if (frame_r.error().code ==
                static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            {
                needs_rebuild = true;
                continue;
            }
            return 15;
        }
        auto& frame = *frame_r;
        auto& cmd = *frame.command_buffer;

        std::array<cd::rhi::ColorAttachmentInfo, 1> color_attach {
            cd::rhi::ColorAttachmentInfo { .view = frame.swapchain_image_view,
                                          .load_op = cd::rhi::LoadOp::kClear,
                                          .store_op = cd::rhi::StoreOp::kStore,
                                          .clear_color = { .f32 = { 0.05F, 0.07F, 0.10F, 1.0F } } }
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
        instance.bind(cmd, 0);
        cmd.bind_vertex_buffer(0, vb, 0);
        cmd.bind_index_buffer(ib, 0, cd::rhi::IndexType::kUInt16);
        cmd.draw_indexed(static_cast<std::uint32_t>(kIndices.size()), 1, 0, 0, 0);
        cmd.end_render_pass();

        auto end_r = renderer.end_frame();
        if (!end_r.has_value())
        {
            if (end_r.error().code == static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            {
                needs_rebuild = true;
                continue;
            }
            return 16;
        }
        ++frame_idx;
    }

    renderer.wait_idle();
    device.destroy_buffer(ib);
    device.destroy_buffer(vb);
    device.destroy_sampler(*samp_h);
    device.destroy_texture_view(*view_h);
    device.destroy_texture(*tex_h);
    if (self_cooked)
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    std::printf("hello_textured_cooked: clean exit.\n");
    return 0;
}
