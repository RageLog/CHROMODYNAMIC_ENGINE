// =============================================================================
// CHROMODYNAMIC — samples/hello_hot_reload/main.cpp
//
// Live shader hot-reload demo. Writes a triangle's fragment shader to a
// temp file on first launch, polls it every frame via cd::shader::FileWatcher,
// recompiles via cd::shader::CachedCompiler + rebuilds the Material when
// the file changes.
//
// Try it:
//   * Run hello_hot_reload (no args → self-bootstrapped shader files)
//   * Edit the printed shader path in any editor while the demo is running
//   * The triangle color updates within a frame, no engine restart
//
// Headless mode skips the editor loop and just rebuilds once after a self-
// triggered rewrite — proves the wiring in CI.
// =============================================================================
#include "SampleRuntime.hpp"

#include <cd/material/Material.hpp>
#include <cd/platform/Window.hpp>
#include <cd/render/Renderer.hpp>
#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi_vulkan/VulkanDevice.hpp>
#include <cd/shader/CachedCompiler.hpp>
#include <cd/shader/Compiler.hpp>
#include <cd/shader/FileWatcher.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{

namespace fs = std::filesystem;

// Vertex shader is stable; only the fragment shader is hot-reloaded.
constexpr const char* kVS = R"glsl(
#version 450
layout(location = 0) out vec3 v_color;
void main() {
  vec2 positions[3] = vec2[3](
    vec2( 0.0,  0.6),
    vec2(-0.6, -0.6),
    vec2( 0.6, -0.6)
  );
  vec3 colors[3] = vec3[3](
    vec3(1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0)
  );
  gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
  v_color = colors[gl_VertexIndex];
}
)glsl";

// Default fragment text. The user edits this on disk and the demo reloads
// it; the constant only seeds the initial file.
constexpr std::string_view kDefaultFs = R"glsl(
#version 450
layout(location = 0) in  vec3 v_color;
layout(location = 0) out vec4 out_color;
void main() {
  // Edit me! Try replacing v_color with vec3(1.0, 0.4, 0.7) and save.
  out_color = vec4(v_color, 1.0);
}
)glsl";

// Alternate fragment used by the headless smoke-test to prove the
// reload path triggers (the file mtime changes, the watcher reports
// dirty, the material rebuild succeeds).
constexpr std::string_view kAltFs = R"glsl(
#version 450
layout(location = 0) in  vec3 v_color;
layout(location = 0) out vec4 out_color;
void main() {
  out_color = vec4(1.0 - v_color, 1.0);
}
)glsl";

[[nodiscard]] fs::path pick_shader_path()
{
    static std::atomic<std::uint64_t> seq { 0 };
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() / ("cd_hot_reload_frag_" + std::to_string(static_cast<std::uint64_t>(stamp)) +
                                        "_" + std::to_string(seq.fetch_add(1)) + ".frag");
}

void write_text(const fs::path& p, std::string_view text)
{
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
}

[[nodiscard]] std::optional<std::string> read_text(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f.is_open())
        return std::nullopt;
    const auto sz = f.tellg();
    if (sz < 0)
        return std::nullopt;
    std::string out(static_cast<std::size_t>(sz), '\0');
    f.seekg(0);
    f.read(out.data(), sz);
    return out;
}

/// Recompile the fragment shader text + build a fresh Material. Caller
/// holds onto the new Material; the old one is dropped (destructor frees
/// every GPU resource). Errors are reported but non-fatal — keep
/// rendering the previous good Material so a typo doesn't kill the demo.
[[nodiscard]] std::optional<cd::material::Material> rebuild_material(
    cd::rhi::IDevice& device,
    cd::shader::ICompiler& compiler,
    std::string_view fs_source,
    cd::rhi::Format swapchain_format
)
{
    cd::material::MaterialDesc md {};
    md.vertex_glsl = kVS;
    md.fragment_glsl = fs_source;
    const std::array<cd::rhi::Format, 1> formats { swapchain_format };
    md.color_attachment_formats = formats;
    md.topology = cd::rhi::PrimitiveTopology::kTriangleList;
    md.raster.cull = cd::rhi::CullMode::kNone;
    md.name = "hot_reload";
    auto r = cd::material::Material::create(device, &compiler, md);
    if (!r.has_value())
    {
        std::fprintf(
            stderr,
            "[hot_reload] rebuild failed: %.*s\n",
            static_cast<int>(r.error().message.size()),
            r.error().message.data()
        );
        return std::nullopt;
    }
    return std::move(*r);
}

}  // namespace

int main(int argc, char** argv)
{
    const cd::sample::Runtime runtime = cd::sample::parse_runtime(argc, argv);

    // ---- Bootstrap the on-disk shader -------------------------------------
    const auto frag_path = pick_shader_path();
    write_text(frag_path, kDefaultFs);
    std::printf("hello_hot_reload: editing %s\n", frag_path.string().c_str());
    std::fflush(stdout);

    // ---- Window + device + renderer ---------------------------------------
    cd::platform::WindowDesc wd {};
    wd.title = "CHROMODYNAMIC — hello_hot_reload (edit the .frag in temp/ to live-reload)";
    wd.width = 800;
    wd.height = 600;
    auto window_r = cd::platform::create_window(wd);
    if (!window_r.has_value())
        return 1;
    auto& window = **window_r;

    cd::rhi_vulkan::VulkanCreateInfo vci {};
    auto device_r = cd::rhi_vulkan::create_vulkan_device(vci);
    if (!device_r.has_value())
        return 2;
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
        return 3;
    auto& renderer = *renderer_r;

    // ---- Compiler + cache + file-watcher ----------------------------------
    // Reuse the existing .shader_cache/ folder so SPIR-V is persisted
    // across launches just like the rest of the engine.
    auto compiler = cd::shader::make_cached_glslang_compiler(".shader_cache");
    if (compiler == nullptr)
    {
        std::fprintf(stderr, "no glslang\n");
        return 4;
    }

    cd::shader::FileWatcher watcher;
    watcher.add(frag_path.string());

    // Initial build from the file we just wrote.
    auto initial_src = read_text(frag_path);
    if (!initial_src.has_value())
        return 5;
    auto material = rebuild_material(device, *compiler, *initial_src, cd::rhi::Format::kBGRA8Unorm);
    if (!material.has_value())
        return 6;

    std::printf("hello_hot_reload: ready. Edit %s and save to live-reload.\n", frag_path.string().c_str());
    std::fflush(stdout);

    // ---- Headless: simulate a single edit so CI verifies reload path -----
    if (runtime.headless_frames > 0)
    {
        // Wait briefly so mtime granularity (FAT-2s, NTFS-100ns) lets the
        // watcher detect the change reliably.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        write_text(frag_path, kAltFs);
    }

    std::vector<cd::platform::OSEvent> events;
    events.reserve(64);
    bool needs_rebuild_sc = false;
    std::uint32_t frame_idx = 0;
    std::uint32_t reload_count = 0;

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
                needs_rebuild_sc = true;
        }
        if (needs_rebuild_sc)
        {
            if (window.width() == 0 || window.height() == 0)
                continue;
            if (!renderer.recreate_swapchain({ window.width(), window.height() }).has_value())
                continue;
            needs_rebuild_sc = false;
        }

        // ---- Hot-reload poll (cheap; one stat() per frame) ----------------
        if (watcher.poll())
        {
            for (const auto& dirty_path : watcher.dirty())
            {
                std::printf("[hot_reload] file changed: %s\n", dirty_path.c_str());
                auto new_src = read_text(dirty_path);
                if (!new_src.has_value())
                    continue;
                auto next = rebuild_material(device, *compiler, *new_src, cd::rhi::Format::kBGRA8Unorm);
                if (next.has_value())
                {
                    // Drain in-flight frames so it's safe to destroy the old
                    // Material's pipeline before we replace it.
                    renderer.wait_idle();
                    material = std::move(next);
                    ++reload_count;
                    std::printf("[hot_reload] reload %u OK\n", reload_count);
                }
            }
            std::fflush(stdout);
        }

        auto frame_r = renderer.begin_frame();
        if (!frame_r.has_value())
        {
            if (frame_r.error().code ==
                static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            {
                needs_rebuild_sc = true;
                continue;
            }
            return 7;
        }
        auto& frame = *frame_r;
        auto& cmd = *frame.command_buffer;

        std::array<cd::rhi::ColorAttachmentInfo, 1> color_attach {
            cd::rhi::ColorAttachmentInfo {
                                          .view = frame.swapchain_image_view,
                                          .load_op = cd::rhi::LoadOp::kClear,
                                          .store_op = cd::rhi::StoreOp::kStore,
                                          .clear_color = { .f32 = { 0.05F, 0.08F, 0.12F, 1.0F } } }
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
        material->apply(cmd);
        cmd.draw(3, 1, 0, 0);
        cmd.end_render_pass();

        auto end_r = renderer.end_frame();
        if (!end_r.has_value())
        {
            if (end_r.error().code == static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            {
                needs_rebuild_sc = true;
                continue;
            }
            return 8;
        }
        ++frame_idx;
    }

    renderer.wait_idle();
    std::error_code ec;
    fs::remove(frag_path, ec);
    std::printf("hello_hot_reload: clean exit (%u live reload(s)).\n", reload_count);
    return 0;
}
