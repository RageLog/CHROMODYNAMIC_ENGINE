// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/core/StudioApp.cpp
//
// Source 2 toolframework2-modeled StudioApp lifecycle implementation
// (ADR-20261112). Delegates rendering, scene authoring, physics/PIE, asset
// ingestion, 2D workspace, UI docks, camera math, file I/O, and swapchain PNG
// capture to compiled cd_editor_lib and cd::app_common modules.
// =============================================================================
#include "StudioApp.hpp"

#include "core/StudioState.hpp"
#include "physics/PhysicsPieSolver.hpp"
#include "pipeline/AssetIngest.hpp"
#include "render/WorldRenderPasses.hpp"
#include "scene/SceneOperations.hpp"
#include "ui/UnifiedTopBar.hpp"
#include "ui/ViewportHudAndGizmo.hpp"
#include "workspace_2d/Workspace2D.hpp"

#include "LineShader.hpp"
#include "Studio2DWorkspace.hpp"
#include "StudioBootBindless.hpp"
#include "StudioBootGpu.hpp"
#include "StudioBootPostFx.hpp"
#include "StudioBootProject.hpp"
#include "StudioBootScene.hpp"
#include "StudioCliOptions.hpp"
#include "StudioDiagnosticOverlays.hpp"
#include "StudioModifierClasses.hpp"
#include "StudioSelfTester.hpp"
#include "StudioSkinnedAnimator.hpp"
#include "StudioTlasBuilder.hpp"
#include "StudioViewportOverlays.hpp"
#include "StudioViewportPicker.hpp"

#include <cd/app_common/CameraMath3D.hpp>
#include <cd/app_common/FileTextIo.hpp>
#include <cd/app_common/FrameCapture.hpp>
#include <cd/core/Compat.hpp>
#include <cd/math/Transform.hpp>
#include <cd/post/bloom/Bloom.hpp>
#include <cd/render/MeshUpload.hpp>
#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#include <cd/ui/ChromaUi.hpp>
#include <cd/ui/theme/Theme.hpp>
#include <cd/world2d/PlaytestRunner2D.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{

constexpr std::uint32_t kAudioSampleRate = 48000U;
constexpr std::size_t   kAudioBufferLen  = 512U;

[[nodiscard]] float square_wave(std::uint64_t i, float hz) noexcept
{
    const float phase = static_cast<float>(i) * hz / static_cast<float>(kAudioSampleRate);
    const float frac  = phase - std::floor(phase);
    return (frac < 0.5F) ? 0.55F : -0.55F;
}

[[nodiscard]] float burst_noise(std::uint64_t i) noexcept
{
    const auto cycle     = static_cast<std::uint64_t>(kAudioSampleRate / 4U);
    const auto burst_len = static_cast<std::uint64_t>(kAudioSampleRate / 30U);
    if ((i % cycle) >= burst_len)
    {
        return 0.0F;
    }
    std::uint64_t x = i * 2654435761ULL + 0xC0FFEEULL;
    x ^= x << 13U;
    x ^= x >> 17U;
    x ^= x << 5U;
    const float n =
        (static_cast<float>(x & 0xFFFFFFFFU) / static_cast<float>(0xFFFFFFFFU)) * 2.0F - 1.0F;
    return n * 0.7F;
}

[[nodiscard]] cd::ui::widgets::Color to_widget_color(cd::ui::theme::ColorToken c) noexcept
{
    auto pack = [](float v) noexcept -> std::uint8_t
    {
        const float clamped = std::clamp(v, 0.0F, 1.0F);
        return static_cast<std::uint8_t>(std::lround(clamped * 255.0F));
    };
    return cd::ui::widgets::Color { pack(c.r), pack(c.g), pack(c.b), pack(c.a) };
}

[[nodiscard]] cd::ui::widgets::Theme build_dark_widget_theme()
{
    const auto dark = cd::ui::theme::k_dark_theme();
    cd::ui::widgets::Theme t {};
    t.background    = to_widget_color(dark.color(cd::ui::theme::PaletteSlot::kBackground));
    t.surface       = to_widget_color(dark.color(cd::ui::theme::PaletteSlot::kSurface));
    t.surface_hover = to_widget_color(dark.color(cd::ui::theme::PaletteSlot::kSurfaceVariant));
    t.surface_press = to_widget_color(dark.color(cd::ui::theme::PaletteSlot::kPrimaryContainer));
    t.accent        = to_widget_color(dark.color(cd::ui::theme::PaletteSlot::kPrimary));
    t.accent_hover  = to_widget_color(dark.color(cd::ui::theme::PaletteSlot::kPrimaryContainer));
    t.text          = to_widget_color(dark.color(cd::ui::theme::PaletteSlot::kOnSurface));
    t.text_dim      = to_widget_color(dark.color(cd::ui::theme::PaletteSlot::kOnSurfaceVariant));
    return t;
}

[[nodiscard]] std::vector<std::uint8_t> find_system_font_for_bd()
{
    static constexpr std::array<const char*, 6> kCandidates {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/calibri.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
    };
    for (const char* p : kCandidates)
    {
        auto bin = cd::app_common::read_binary_file(p);
        if (bin.has_value() && !bin->empty())
        {
            return std::move(*bin);
        }
    }
    return {};
}

[[nodiscard]] cd::ui::widgets::Rect uiw_to_widget_rect(const cd::ui::layout::Rect& r) noexcept
{
    return cd::ui::widgets::Rect { r.x, r.y, r.width, r.height };
}

void uiw_build_tree(cd::ui::layout::FlexTree& tree,
                    cd::ui::layout::NodeId& root,
                    cd::ui::layout::NodeId& label,
                    cd::ui::layout::NodeId& button,
                    cd::ui::layout::NodeId& slider)
{
    namespace ll = cd::ui::layout;
    ll::FlexStyle rs {};
    rs.direction   = ll::FlexDirection::kColumn;
    rs.align_items = ll::AlignItems::kStretch;
    rs.padding     = { 16.0F, 16.0F, 16.0F, 16.0F };
    rs.gap_main    = 12.0F;
    root           = tree.create_node(rs);

    ll::FlexStyle leaf {};
    leaf.flex_shrink      = 0.0F;
    leaf.intrinsic_height = 36.0F;
    label                 = tree.create_node(leaf);

    leaf.intrinsic_height = 40.0F;
    button                = tree.create_node(leaf);

    leaf.intrinsic_height = 28.0F;
    slider                = tree.create_node(leaf);

    tree.add_child(root, label);
    tree.add_child(root, button);
    tree.add_child(root, slider);
}

}  // namespace

StudioApp::StudioApp(cd::studio::StudioConfig cfg) noexcept
    : cd::app::App(cd::app::AppConfig {
          .title             = "CHROMODYNAMIC STUDIO",
          .window_width      = 1600U,
          .window_height     = 900U,
          .start_maximized   = false,
          .enable_validation = (cd::core::compat::read_env_var("CD_VK_VALIDATION") == "1"),
          .max_frames        = 0U,
      }),
      initial_cfg_(std::move(cfg))
{
}

StudioApp::~StudioApp() = default;

cd::core::Result<void> StudioApp::on_boot()
{
    state_  = std::make_unique<cd::studio::StudioEngineState>(std::move(initial_cfg_));
    auto& s = *state_;

    cd::platform::WindowDesc wd {};
    wd.title     = "CHROMODYNAMIC STUDIO";
    wd.width     = 1600U;
    wd.height    = 900U;
    wd.frameless = true;
    auto window_r = cd::platform::create_window(wd);
    if (!window_r.has_value())
    {
        return std::unexpected(window_r.error());
    }
    s.window     = std::move(*window_r);
    auto& window = *s.window;

    cd::rhi::vulkan::VulkanCreateInfo vci {};
    vci.enable_validation = (cd::core::compat::read_env_var("CD_VK_VALIDATION") == "1");
    auto dev_r            = cd::rhi::vulkan::create_vulkan_device(vci);
    if (!dev_r.has_value())
    {
        return std::unexpected(dev_r.error());
    }
    s.device_owner = std::move(*dev_r);
    auto& device   = *s.device_owner;

    cd::render::RendererDesc rd {};
    rd.device                   = &device;
    rd.swapchain.window_handle  = window.native_window_handle();
    rd.swapchain.display_handle = window.native_display_handle();
    rd.swapchain.extent         = { window.width(), window.height() };
    rd.swapchain.format         = cd::rhi::Format::kBGRA8Unorm;
    rd.swapchain.image_count    = 3U;
    rd.swapchain.vsync          = (cd::core::compat::read_env_var("CD_VSYNC") == "1");
    rd.frames_in_flight         = 1U;
    auto renderer_r             = cd::render::Renderer::create(rd);
    if (!renderer_r.has_value())
    {
        return std::unexpected(renderer_r.error());
    }
    s.renderer = std::move(*renderer_r);

    cd::imgui::InitDesc imgui_id {};
    imgui_id.window           = &window;
    imgui_id.device           = &device;
    imgui_id.color_format     = cd::rhi::Format::kBGRA8Unorm;
    imgui_id.frames_in_flight = 1U;
    auto ctx_r                = cd::imgui::Context::create(imgui_id);
    if (!ctx_r.has_value())
    {
        return std::unexpected(ctx_r.error());
    }
    s.imgui_ctx = std::move(*ctx_r);

    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Valve Source 2 Hammer 5 + Godot 4 Unified Single 32px Top Bar (ADR-20261112)
    cd::ui::chroma::state().top_bar_height  = 32.0F;
    cd::ui::chroma::state().menu_bar_height = 32.0F;

    {
        const auto ttf = find_system_font_for_bd();
        if (!ttf.empty() &&
            s.ui_font.load_ttf_in_memory(
                std::span<const std::uint8_t>(ttf.data(), ttf.size())))
        {
            static constexpr float         kPixelSize = 15.0F;
            static constexpr std::uint32_t kAtlasDim  = 1024U;
            (void)s.ui_font.rasterize_range(0x0020U, 0x00FFU, kPixelSize, kAtlasDim);
        }
        s.ui_widget_theme = build_dark_widget_theme();

        uiw_build_tree(s.uiw_tree, s.uiw_root, s.uiw_label, s.uiw_button, s.uiw_slider);
        s.uiw_tree_built = true;
        s.uiw_btn.set_on_click([&s] { ++s.uiw_click_count; });
        s.uiw_sld.set_on_change([&s](float v) { s.uiw_slider_val = v; });

        cd::ui::renderer_rhi::SubmitterCreateInfo sci {};
        sci.color_format = cd::rhi::Format::kBGRA8Unorm;
        sci.max_vertices = 65532U;
        sci.max_indices  = 196608U;
        auto sub_r =
            cd::ui::renderer_rhi::Submitter::create_with_inline_shader(device, sci);
        if (sub_r.has_value())
        {
            s.ui_submitter = std::make_unique<cd::ui::renderer_rhi::Submitter>(
                std::move(*sub_r));
            s.bd_panel.set_tree_root(1U);
            s.bd_panel.set_selected(2U);
            s.bd_panel.set_pan_offset(20.0F, 10.0F);
        }
    }

    s.compiler = cd::shader::make_glslang_compiler();
    if (!s.compiler)
    {
        return std::unexpected(cd::core::ErrorCode { 0U, 5U, "shader compiler" });
    }

    if (const int rc = cd_studio::create_render_targets(
            device, { window.width(), window.height() }, s.rts);
        rc != 0)
    {
        return std::unexpected(
            cd::core::ErrorCode { 0U, static_cast<std::uint32_t>(rc), "render targets" });
    }

    auto mat_r = cd_studio::spawn_materials(device, s.compiler.get());
    if (!mat_r.has_value())
    {
        return std::unexpected(
            cd::core::ErrorCode { 0U,
                static_cast<std::uint32_t>(mat_r.error().exit_code), "materials" });
    }
    s.materials = std::move(*mat_r);

    {
        constexpr std::array<cd::rhi::Format, 4> kLineColorFmts {
            cd::rhi::Format::kRGBA16Float,
            cd::rhi::Format::kRGBA16Float,
            cd::rhi::Format::kRGBA8Unorm,
            cd::rhi::Format::kRG8Unorm
        };
        cd::debug_draw::RendererDesc dd {};
        dd.color_attachment_formats = kLineColorFmts;
        dd.depth_attachment_format  = cd::rhi::Format::kD32Float;
        dd.vertex_glsl              = cd::studio_rt::kLineVS;
        dd.fragment_glsl            = cd::studio_rt::kLineFS;
#if STUDIO_USE_ON_DISK_SHADERS
        const std::string line_vert_path =
            cd_studio::resolve_shader_path(cd_studio::kLineVertGlslPath);
        const std::string line_frag_path =
            cd_studio::resolve_shader_path(cd_studio::kLineFragGlslPath);
        dd.vertex_glsl_path   = line_vert_path;
        dd.fragment_glsl_path = line_frag_path;
#endif
        dd.name   = "chromodynamic_studio/debug_line";
        auto dd_r = cd::debug_draw::Renderer::create(device, s.compiler.get(), dd);
        if (!dd_r.has_value())
        {
            return std::unexpected(cd::core::ErrorCode { 0U, 11U, "debug_line" });
        }
        s.debug_draw = std::move(*dd_r);
#if STUDIO_USE_ON_DISK_SHADERS
        s.shader_watch.add_entry(
            nullptr,
            std::vector<std::string> {
                cd_studio::resolve_shader_path(cd_studio::kLineVertGlslPath),
                cd_studio::resolve_shader_path(cd_studio::kLineFragGlslPath) },
            [this](cd::rhi::IDevice& d, cd::shader::ICompiler* c) -> bool
            {
                static constexpr std::array<cd::rhi::Format, 4> kFmts {
                    cd::rhi::Format::kRGBA16Float,
                    cd::rhi::Format::kRGBA16Float,
                    cd::rhi::Format::kRGBA8Unorm,
                    cd::rhi::Format::kRG8Unorm
                };
                cd::debug_draw::RendererDesc rd {};
                rd.color_attachment_formats = kFmts;
                rd.depth_attachment_format  = cd::rhi::Format::kD32Float;
                rd.vertex_glsl              = cd::studio_rt::kLineVS;
                rd.fragment_glsl            = cd::studio_rt::kLineFS;
                const std::string rl_vert_path =
                    cd_studio::resolve_shader_path(cd_studio::kLineVertGlslPath);
                const std::string rl_frag_path =
                    cd_studio::resolve_shader_path(cd_studio::kLineFragGlslPath);
                rd.vertex_glsl_path   = rl_vert_path;
                rd.fragment_glsl_path = rl_frag_path;
                rd.name               = "chromodynamic_studio/debug_line";
                return state_->debug_draw.recreate_pipeline(d, c, rd);
            },
            "debug_line");
#endif
    }

    s.shader_watch.add_entry(
        &s.materials.prim,
        std::vector<std::string> {
            cd_studio::resolve_shader_path(cd_studio::kPrimVertGlslPath),
            cd_studio::resolve_shader_path(cd_studio::kPrimFragGlslPath) },
        [this](cd::rhi::IDevice& d, cd::shader::ICompiler* c) -> bool
        {
            return cd_studio::prim_recreate(d, c, &state_->materials.prim,
                                            state_->materials.prim_bindless_layout);
        },
        "prim");
    s.shader_watch.add_entry(
        &s.materials.shadow,
        std::vector<std::string> {
            cd_studio::resolve_shader_path(cd_studio::kShadowVertGlslPath),
            cd_studio::resolve_shader_path(cd_studio::kShadowFragGlslPath) },
        [this](cd::rhi::IDevice& d, cd::shader::ICompiler* c) -> bool
        {
            return cd_studio::shadow_recreate(d, c, &state_->materials.shadow);
        },
        "shadow");

    if (auto gb = cd_studio::setup_boot_gpu_buffers(s, device); !gb.has_value())
    {
        return std::unexpected(gb.error());
    }
    constexpr std::uint32_t kLightUboBytes = cd_studio::kBootLightUboBytes;

    if (auto pt = cd_studio::setup_boot_procedural_textures(s, device); !pt.has_value())
    {
        return std::unexpected(pt.error());
    }

    if (auto pr_r = cd_studio::init_prim_inst_descriptors(
            s, device, kLightUboBytes, cd::studio_rt::kInstMatBytes);
        !pr_r.has_value())
    {
        return std::unexpected(pr_r.error());
    }

    if (auto pf = cd_studio::setup_boot_postfx_instances(
            s, device, { window.width(), window.height() });
        !pf.has_value())
    {
        return std::unexpected(pf.error());
    }
    cd_studio::bind_bloom_descriptors(s);
    cd_studio::bind_composite_hdr_descriptors(s);

    auto upload_alb_fn = [&device_ref = device](
        const std::uint8_t* rgba, std::uint32_t w, std::uint32_t h)
        -> std::pair<cd::rhi::TextureHandle, cd::rhi::TextureViewHandle>
    {
        auto t = cd_studio::create_texture_rgba8(device_ref, rgba, w, h);
        return { t.image, t.view };
    };
    cd_studio::AlbedoSlot alb_slot {
        .image_io = &s.albedo_tex.image,
        .view_io  = &s.albedo_tex.view,
        .sampler  = s.albedo_sampler,
        .upload   = upload_alb_fn,
    };
    const bool eager_golden_boot = cd::studio_rt::golden::enabled();
    s.meshes = cd_studio::boot_meshes(device, alb_slot, s.prim_inst, s.materials.prim,
                                      s.normal_tex.view, s.mr_tex.view,
                                      eager_golden_boot);
    if (s.meshes.build_exit_code != 0)
    {
        return std::unexpected(
            cd::core::ErrorCode { 0U,
                static_cast<std::uint32_t>(s.meshes.build_exit_code), "meshes" });
    }
    if (s.meshes.has_gltf_texture || s.meshes.has_cesium_texture)
    {
        s.has_gltf_texture = true;
    }

    cd_studio::wire_w8be_bindless_and_meta(
        s, device, kLightUboBytes, cd::studio_rt::kInstMatBytes);

    cd_studio::setup_boot_world_and_project(s);
    if (eager_golden_boot)
    {
        const bool chrome_probe_mode =
            cd::studio_rt::golden::enabled() &&
            cd::studio_rt::golden::options().fixture_index == 5;
        cd::studio::render::spawn_golden_fixture_entities(s, chrome_probe_mode);
        cd::studio::pipeline::scan_and_ingest_generic_gltf_folder(s);
    }

    auto log_push_fn = [&s](std::string msg) { s.push_log(std::move(msg)); };
    s.push_log("[boot] " + std::to_string(s.entities.size()) +
               " ECS entities spawned (Empty_Stage -- templates & scenes load on demand)");

    s.outline.style = cd::editor::OutlineStyle::kWireframe;
    cd_studio::setup_boot_camera(s, log_push_fn);
    cd_studio::init_audio(s.audio_state, square_wave, burst_noise);
    cd_studio::setup_boot_lights_and_clusters(s);
    cd_studio::register_boot_palette_commands(s, log_push_fn);

    constexpr const char* kSavePath = "studio_scene.cdscene.json";
    s.palette.register_command(80U, "Scene: Save",
        [&s]()
        {
            (void)cd::studio::scene_ops::save_scene(s, kSavePath);
        });
    s.palette.register_command(95U, "Gizmo: Toggle Visibility",
        [&s]()
        {
            s.gizmo_state.visible = !s.gizmo_state.visible;
            s.push_log("[gizmo] visible=" +
                       std::string(s.gizmo_state.visible ? "true" : "false"));
        });
    s.palette.register_command(81U, "Scene: Load (replace world)",
        [&s]()
        {
            if (cd::studio::scene_ops::load_scene(s, kSavePath))
            {
                s.history.clear();
            }
        });

    return {};
}

void StudioApp::on_frame(const cd::app::FrameContext& /*fc*/)
{
    auto& s      = *state_;
    auto& window = *s.window;
    auto& device = *s.device_owner;
    auto& ctx    = *s.imgui_ctx;

    constexpr cd::rhi::Extent2D kShadowMapSize { 2048U, 2048U };
    constexpr float             kFloorY     = -0.55F;
    constexpr float             kShadowLift = 0.01F;
    using cd_studio::kAudioRingFrames;

    using clock                  = std::chrono::steady_clock;
    const auto frame_loop_start  = clock::now();
    auto       last_tick         = frame_loop_start;
    std::vector<cd::platform::OSEvent> events;
    events.reserve(64U);

    if (s.config.demo_all_phases)
    {
        cd::studio::scene_ops::load_interactive_template(s, 1);
        cd::studio::studio_world().environment_modifiers.add_or_replace(
            std::make_unique<cd::studio::FogModifier>());
        (void)cd::studio::scene_ops::export_standalone_package(
            s, "build/export/ChromodynamicGame");
    }

    while (true)
    {
        events.clear();
        if (!window.pump_events(events))
        {
            break;
        }

        if ((s.frame_idx & 63U) == 0U)
        {
            (void)s.shader_watch.poll_and_reload(device, s.compiler.get());
        }

        for (const auto& ev : events)
        {
            ctx.handle_event(ev);
            if (ev.kind == cd::platform::OSEventKind::kKeyDown &&
                ev.key == cd::platform::KeyCode::kEscape)
            {
                if (s.pie_playing)
                {
                    cd::studio::physics_pie::exit_pie(s);
                }
                else if (s.gizmo.is_dragging())
                {
                    (void)s.gizmo.end_drag();
                    s.push_log("[esc] gizmo drag cancelled");
                }
                else if (s.palette_visible)
                {
                    s.palette_visible = false;
                    s.palette_query.clear();
                    s.push_log("[esc] palette closed");
                }
                else if (s.selected >= 0)
                {
                    s.selected = -1;
                    s.push_log("[esc] selection cleared");
                }
            }
            else if (ev.kind == cd::platform::OSEventKind::kResize)
            {
                s.needs_rebuild = true;
            }
            else if (ev.kind == cd::platform::OSEventKind::kKeyDown &&
                     ev.key == cd::platform::KeyCode::kF1)
            {
                s.palette_visible = !s.palette_visible;
                if (s.palette_visible)
                {
                    s.palette_query.clear();
                }
            }
            else if (ev.kind == cd::platform::OSEventKind::kKeyDown)
            {
                if (ev.key == cd::platform::KeyCode::kLCtrl ||
                    ev.key == cd::platform::KeyCode::kRCtrl)
                {
                    s.mod_ctrl = true;
                }
                if (ev.key == cd::platform::KeyCode::kLShift ||
                    ev.key == cd::platform::KeyCode::kRShift)
                {
                    s.mod_shift = true;
                }
                if (ev.key == cd::platform::KeyCode::kP && s.mod_ctrl && s.mod_shift)
                {
                    s.palette_visible = !s.palette_visible;
                    if (s.palette_visible)
                    {
                        s.palette_query.clear();
                    }
                }
            }
            else if (ev.kind == cd::platform::OSEventKind::kKeyUp)
            {
                if (ev.key == cd::platform::KeyCode::kLCtrl ||
                    ev.key == cd::platform::KeyCode::kRCtrl)
                {
                    s.mod_ctrl = false;
                }
                if (ev.key == cd::platform::KeyCode::kLShift ||
                    ev.key == cd::platform::KeyCode::kRShift)
                {
                    s.mod_shift = false;
                }
            }

            const bool key_dn         = (ev.kind == cd::platform::OSEventKind::kKeyDown);
            const bool key_up         = (ev.kind == cd::platform::OSEventKind::kKeyUp);
            const bool ui_captures_kb = ImGui::GetIO().WantCaptureKeyboard;
            auto&      fl             = s.app_state.free_look;
            if (ui_captures_kb || s.pie_playing)
            {
                fl.key_w = fl.key_a = fl.key_s = fl.key_d = fl.key_q = fl.key_e = false;
            }
            else if (key_dn || key_up)
            {
                const bool v = key_dn;
                if (ev.key == cd::platform::KeyCode::kW) fl.key_w = v;
                if (ev.key == cd::platform::KeyCode::kA) fl.key_a = v;
                if (ev.key == cd::platform::KeyCode::kS) fl.key_s = v;
                if (ev.key == cd::platform::KeyCode::kD) fl.key_d = v;
                if (ev.key == cd::platform::KeyCode::kQ) fl.key_q = v;
                if (ev.key == cd::platform::KeyCode::kE) fl.key_e = v;
                if (ev.key == cd::platform::KeyCode::kLShift ||
                    ev.key == cd::platform::KeyCode::kRShift)
                {
                    fl.key_shift = v;
                }
                if (ev.key == cd::platform::KeyCode::kLCtrl ||
                    ev.key == cd::platform::KeyCode::kRCtrl)
                {
                    fl.key_ctrl = v;
                }
                if (key_dn && (ev.key == cd::platform::KeyCode::kW ||
                               ev.key == cd::platform::KeyCode::kA ||
                               ev.key == cd::platform::KeyCode::kS ||
                               ev.key == cd::platform::KeyCode::kD ||
                               ev.key == cd::platform::KeyCode::kQ ||
                               ev.key == cd::platform::KeyCode::kE))
                {
                    fl.manual_mode = true;
                    s.scene_cam.set_auto_spin(false);
                }
            }
            if (key_dn && ev.key == cd::platform::KeyCode::kF &&
                !ui_captures_kb && !s.pie_playing)
            {
                cd::studio::scene_ops::focus_selected_node(s);
            }
            if (key_dn && ev.key == cd::platform::KeyCode::kSpace &&
                !ui_captures_kb && !s.pie_playing)
            {
                s.gizmo_state.mode = static_cast<cd::studio::GizmoMode>(
                    (static_cast<std::uint8_t>(s.gizmo_state.mode) + 1U) % 3U);
                const char* ms = "SCALE";
                if (s.gizmo_state.mode == cd::studio::GizmoMode::kTranslate)
                {
                    ms = "TRANSLATE";
                }
                else if (s.gizmo_state.mode == cd::studio::GizmoMode::kRotate)
                {
                    ms = "ROTATE";
                }
                s.push_log(std::string { "[gizmo] mode: " } + ms);
            }
            if (key_dn && ev.key == cd::platform::KeyCode::kDelete &&
                !ui_captures_kb && !s.pie_playing && s.selected >= 0 &&
                cd::studio::studio_world().inline_rename_entity_idx < 0 &&
                cd::studio::studio_world().inline_rename_light_idx < 0 &&
                (s.selected_kind == cd::studio::SelKind::kEntity ||
                 s.selected_kind == cd::studio::SelKind::kLight))
            {
                cd::studio::scene_ops::delete_selected_node(s);
            }
            if (ev.kind == cd::platform::OSEventKind::kMouseButtonDown)
            {
                if (ev.mouse_button == cd::platform::MouseButton::kRight)
                {
                    fl.right_drag     = true;
                    fl.manual_mode    = true;
                    fl.has_last_mouse = false;
                    s.scene_cam.set_auto_spin(false);
                    const float dx   = s.cam.target.x - s.cam.eye.x;
                    const float dy   = s.cam.target.y - s.cam.eye.y;
                    const float dz   = s.cam.target.z - s.cam.eye.z;
                    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                    if (dist > 1e-3F)
                    {
                        fl.dist  = dist;
                        fl.pitch = std::asin(std::clamp(dy / dist, -1.0F, 1.0F));
                        fl.yaw   = std::atan2(dx, -dz);
                    }
                }
                else if (ev.mouse_button == cd::platform::MouseButton::kMiddle &&
                         !ImGui::GetIO().WantCaptureMouse)
                {
                    fl.middle_drag    = true;
                    fl.manual_mode    = true;
                    fl.has_last_mouse = false;
                    s.scene_cam.set_auto_spin(false);
                    const float dx   = s.cam.target.x - s.cam.eye.x;
                    const float dy   = s.cam.target.y - s.cam.eye.y;
                    const float dz   = s.cam.target.z - s.cam.eye.z;
                    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                    if (dist > 1e-3F)
                    {
                        fl.dist  = dist;
                        fl.pitch = std::asin(std::clamp(dy / dist, -1.0F, 1.0F));
                        fl.yaw   = std::atan2(dx, -dz);
                    }
                }
                else if (ev.mouse_button == cd::platform::MouseButton::kLeft &&
                         !ImGui::GetIO().WantCaptureMouse)
                {
                    s.app_state.pick.pending = true;
                    s.app_state.pick.x       = ev.mouse_x;
                    s.app_state.pick.y       = ev.mouse_y;
                }
                if (ev.mouse_button == cd::platform::MouseButton::kLeft)
                {
                    s.ui_lmb_down = true;
                }
            }
            if (ev.kind == cd::platform::OSEventKind::kMouseButtonUp &&
                ev.mouse_button == cd::platform::MouseButton::kRight)
            {
                fl.right_drag = false;
            }
            if (ev.kind == cd::platform::OSEventKind::kMouseButtonUp &&
                ev.mouse_button == cd::platform::MouseButton::kMiddle)
            {
                fl.middle_drag = false;
            }
            if (ev.kind == cd::platform::OSEventKind::kMouseButtonUp &&
                ev.mouse_button == cd::platform::MouseButton::kLeft)
            {
                s.ui_lmb_down = false;
            }
            if (ev.kind == cd::platform::OSEventKind::kMouseMove)
            {
                if (fl.right_drag && fl.has_last_mouse)
                {
                    const float ddx = ev.mouse_x - fl.last_mouse_x;
                    const float ddy = ev.mouse_y - fl.last_mouse_y;
                    fl.yaw   += ddx * cd_studio::kCamLookSpeed;
                    fl.pitch -= ddy * cd_studio::kCamLookSpeed;
                    constexpr float kHalfPi = 1.5707963F;
                    fl.pitch = std::clamp(fl.pitch, -kHalfPi + 0.05F, kHalfPi - 0.05F);
                }
                if (fl.middle_drag && fl.has_last_mouse && !s.pie_playing)
                {
                    const float ddx       = ev.mouse_x - fl.last_mouse_x;
                    const float ddy       = ev.mouse_y - fl.last_mouse_y;
                    const auto  basis     = cd::app_common::compute_camera_basis(s.cam.eye, s.cam.target);
                    const float pan_speed = std::max(0.8F, fl.dist) * 0.0018F;
                    const cd::math::Vec3f pan_delta {
                        (-basis.right.x * ddx + basis.up.x * ddy) * pan_speed,
                        (-basis.right.y * ddx + basis.up.y * ddy) * pan_speed,
                        (-basis.right.z * ddx + basis.up.z * ddy) * pan_speed
                    };
                    s.cam.eye    = s.cam.eye + pan_delta;
                    s.cam.target = s.cam.target + pan_delta;
                }
                fl.last_mouse_x   = ev.mouse_x;
                fl.last_mouse_y   = ev.mouse_y;
                fl.has_last_mouse = true;
                s.ui_mouse_x      = ev.mouse_x;
                s.ui_mouse_y      = ev.mouse_y;
            }
            if (ev.kind == cd::platform::OSEventKind::kMouseWheel &&
                !ImGui::GetIO().WantCaptureMouse && !s.pie_playing)
            {
                fl.dist *= (ev.wheel > 0.0F) ? 0.9F : 1.1F;
                fl.dist = std::clamp(fl.dist, 1.0F, 100.0F);
                const float dx    = s.cam.eye.x - s.cam.target.x;
                const float dy    = s.cam.eye.y - s.cam.target.y;
                const float dz    = s.cam.eye.z - s.cam.target.z;
                const float cur_d = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (cur_d > 1e-4F)
                {
                    const float scale = fl.dist / cur_d;
                    s.cam.eye.x       = s.cam.target.x + dx * scale;
                    s.cam.eye.y       = s.cam.target.y + dy * scale;
                    s.cam.eye.z       = s.cam.target.z + dz * scale;
                }
            }
        }

        if (s.needs_rebuild)
        {
            if (window.width() == 0U || window.height() == 0U)
            {
                continue;
            }
            device.wait_idle();
            if (!s.renderer.recreate_swapchain({ window.width(), window.height() }).has_value())
            {
                continue;
            }
            s.rts.destroy(device);
            if (cd_studio::create_render_targets(
                    device, { window.width(), window.height() }, s.rts) != 0)
            {
                continue;
            }
            s.history_states[0] = cd::rhi::ResourceState::kUndefined;
            s.history_states[1] = cd::rhi::ResourceState::kUndefined;
            if (!cd::post::bloom::create_bloom_chain(
                    device, { window.width(), window.height() }, s.bloom_chain))
            {
                continue;
            }
            cd_studio::bind_bloom_descriptors(s);
            cd_studio::bind_composite_hdr_descriptors(s);
            s.ddgi.bound                  = false;
            s.depth_initialised_on_gpu    = false;
            s.velocity_initialised_on_gpu = false;
            s.bloom_initialised_on_gpu    = false;
            s.last_tlas_scene_hash        = 0U;
            s.needs_rebuild               = false;
        }

        const auto  now = clock::now();
        const float dt  = std::chrono::duration<float>(now - last_tick).count();
        last_tick       = now;

        auto& as = s.audio_state;
        if (!as.muted && (s.frame_idx == 0U || s.fx.audio_show_wave_3d))
        {
            float peak         = 0.0F;
            float comp_db_min  = 0.0F;
            float lim_gain_min = 1.0F;
            for (std::size_t si = 0U; si < kAudioBufferLen; ++si, ++as.sample_t)
            {
                as.bus.mix(0U, square_wave(as.sample_t, 440.0F));
                as.bus.mix(1U, burst_noise(as.sample_t));
                float x     = as.bus.pull();
                x           = as.comp.process(x);
                comp_db_min = std::min(as.comp.gain_db(), comp_db_min);
                const float wet = as.reverb.process(x);
                x               = 0.75F * x + 0.20F * wet;
                x               = as.lowpass.process(x);
                x               = as.limiter.process(x);
                lim_gain_min    = std::min(as.limiter.current_gain(), lim_gain_min);
                peak            = std::max(std::fabs(x), peak);
                x               = std::clamp(x, -1.0F, 1.0F);
                as.ring[as.ring_write] = static_cast<std::int16_t>(x * 32760.0F);
                if (++as.ring_write >= kAudioRingFrames)
                {
                    as.ring_write = 0U;
                }
                ++as.total_written;
            }
            as.peak_window      = peak;
            as.comp_db_window   = comp_db_min;
            as.limiter_gain_min = lim_gain_min;
            as.meter_history.push_back(peak);
            while (as.meter_history.size() > 120U)
            {
                as.meter_history.pop_front();
            }
            s.counters.increment("audio_ticks");
        }

        if (s.frame_idx == 0U)
        {
            const bool had_cli_template     = (s.config.initial_template >= 0);
            const bool had_cli_scene        = !s.config.initial_scene.empty();
            const bool had_cli_context_demo = s.config.load_context_demo;
            if (s.config.autopilot_e2e)
            {
                cd::studio::scene_ops::init_clean_workspace(s);
                cd::studio::scene_ops::build_and_save_custom_game_level(s);
                if (s.config.capture_gameplay_dir.empty())
                {
                    cd::studio::physics_pie::enter_pie(s);
                    cd::studio::studio_world().autopilot_play_active = true;
                }
            }
            else if (had_cli_template)
            {
                cd::studio::scene_ops::init_clean_workspace(s);
                cd::studio::scene_ops::load_interactive_template(s, s.config.initial_template);
                s.config.initial_template = -1;
            }
            if (had_cli_scene)
            {
                cd::studio::scene_ops::init_clean_workspace(s);
                (void)cd::studio::scene_ops::load_scene(s, s.config.initial_scene.c_str());
                s.config.initial_scene.clear();
            }
            if (s.config.load_context_demo)
            {
                cd::studio::scene_ops::seed_demo_context_and_scene(s);
                s.config.load_context_demo = false;
            }
            else if (!s.config.autopilot_e2e && !had_cli_template &&
                     !had_cli_scene && !cd::studio_rt::golden::enabled())
            {
                cd::studio::scene_ops::init_clean_workspace(s);
            }
            if (s.config.start_pie && !s.pie_playing && !s.config.start_2d_studio)
            {
                cd::studio::scene_ops::init_clean_workspace(s);
                cd::studio::physics_pie::enter_pie(s);
            }
            if (s.config.start_2d_studio)
            {
                cd::ui::chroma::state().workspace_mode          = 3;
                cd::studio::studio_world().show_project_manager = false;
                auto& s2d                                       = cd::studio::studio_2d();
                if (!s.config.exec_2d_cmds.empty())
                {
                    std::string payload = s.config.exec_2d_cmds;
                    if (auto rd = cd::app_common::read_text_file_utf8(s.config.exec_2d_cmds);
                        rd.has_value())
                    {
                        payload = std::move(*rd);
                    }
                    (void)s2d.command_bus.execute_json(s2d.stage, payload);
                }
                if (s.config.playtest_2d_frames > 0U)
                {
                    s2d.last_playtest = cd::world2d::run_playtest_2d(
                        s2d.stage, s.config.playtest_2d_frames, true, false);
                    s2d.has_playtest_report = true;
                    s2d.autopilot_in_pie    = true;
                    if (!s.config.telemetry_out_path.empty())
                    {
                        (void)cd::app_common::write_text_file_utf8(
                            s.config.telemetry_out_path, s2d.last_playtest.to_json_string());
                    }
                }
                else if (s.config.start_pie)
                {
                    s2d.stage.enter_play();
                    s2d.autopilot_in_pie = s.config.autopilot_e2e;
                }
            }
            if (s.config.has_cli_cam_eye)
            {
                s.cam.eye = s.config.cli_cam_eye;
            }
            if (s.config.has_cli_cam_target)
            {
                s.cam.target = s.config.cli_cam_target;
            }
            if (s.config.has_cli_cam_eye || s.config.has_cli_cam_target)
            {
                cd::studio::scene_ops::sync_camera_and_invalidate_history(s);
            }
            if (s.config.force_project_manager ||
                (!s.config.self_test_all && !s.config.autopilot_e2e &&
                 !had_cli_template && !had_cli_scene && !had_cli_context_demo &&
                 !s.config.start_pie && !s.config.start_2d_studio &&
                 s.config.capture_png.empty() && !cd::studio_rt::golden::enabled()))
            {
                cd::studio::studio_world().show_project_manager = true;
            }
        }

        if (s.config.self_test_all)
        {
            if (cd::studio::run_studio_autonomous_self_test_step(s, s.frame_idx))
            {
                request_shutdown();
            }
        }
        if (s.config.autopilot_e2e && !s.config.capture_gameplay_dir.empty() && s.frame_idx == 13U)
        {
            cd::studio::physics_pie::enter_pie(s);
            cd::studio::studio_world().autopilot_play_active = true;
        }

        if (!cd::studio_rt::golden::enabled())
        {
            cd::studio::physics_pie::tick_modifiers_and_physics(s, dt);
        }

        if (!s.pie_playing)
        {
            cd_studio::update_free_look_camera(s.app_state.free_look, s.cam, s.scene_cam, dt);
        }

        if (!cd::studio_rt::golden::enabled())
        {
            auto&       w_rt               = cd::studio::studio_world();
            std::size_t active_light_count = 0U;
            bool        has_active_sun     = false;
            for (const auto& lrow : s.lights)
            {
                if (lrow.enabled && lrow.light.intensity > 0.001F)
                {
                    ++active_light_count;
                    if (lrow.light.type == cd::light::LightType::kDirectional)
                    {
                        has_active_sun = true;
                    }
                }
            }

            bool  has_cloud_entity         = false;
            float entity_cloud_cov         = s.fx.clouds_coverage;
            int   scene_cam_entity_idx     = -1;
            bool  has_player_vessel        = false;
            bool  has_visible_ground_floor = false;
            bool  has_sponza_entity_vis    = false;

            for (std::size_t ei = 0U; ei < s.entities.size(); ++ei)
            {
                const auto& ent = s.entities[ei];
                auto&       ems = w_rt.entity_modifiers[ent.handle.id];
                if (ent.visible && ent.name.find("Ground_Floor") != std::string::npos)
                {
                    has_visible_ground_floor = true;
                }
                if (ent.visible && ent.kind == cd::studio::PrimitiveKind::kSponza)
                {
                    has_sponza_entity_vis = true;
                }
                const auto* cld_mod = ems.find<cd::studio::CloudLayerModifier>();
                const auto* sky_mod = ems.find<cd::studio::SkyAtmosphereModifier>();
                const bool  is_cloud_ent =
                    (cld_mod != nullptr) || (sky_mod != nullptr) ||
                    (ent.name.find("Cloud") != std::string::npos) ||
                    (ent.name.find("cloud") != std::string::npos);
                if (is_cloud_ent && ent.visible)
                {
                    if (cld_mod != nullptr)
                    {
                        if (cld_mod->enabled)
                        {
                            has_cloud_entity = true;
                            entity_cloud_cov = cld_mod->clouds_coverage;
                        }
                    }
                    else if (sky_mod != nullptr)
                    {
                        if (sky_mod->enabled)
                        {
                            has_cloud_entity = true;
                            entity_cloud_cov = sky_mod->clouds_coverage;
                        }
                    }
                    else
                    {
                        has_cloud_entity = true;
                        if (entity_cloud_cov <= 0.001F)
                        {
                            entity_cloud_cov = 0.45F;
                        }
                    }
                }

                if (!ent.visible)
                {
                    continue;
                }

                if (const auto* le_mod = ems.find<cd::studio::LightEmitterModifier>();
                    le_mod && le_mod->enabled && le_mod->intensity > 0.001F)
                {
                    ++active_light_count;
                    if (le_mod->light_type == cd::light::LightType::kDirectional)
                    {
                        has_active_sun = true;
                    }
                }

                if (const auto* chr_mod = ems.find<cd::studio::CharacterControllerModifier>();
                    chr_mod && chr_mod->enabled)
                {
                    has_player_vessel = true;
                }

                const auto* cam_mod    = ems.find<cd::studio::CameraLensModifier>();
                const bool  is_cam_ent =
                    (cam_mod != nullptr && cam_mod->enabled) ||
                    (ent.name.find("Camera") != std::string::npos) ||
                    (ent.name.find("camera") != std::string::npos);
                if (is_cam_ent)
                {
                    if (scene_cam_entity_idx < 0 || std::cmp_equal(s.selected, ei))
                    {
                        scene_cam_entity_idx = static_cast<int>(ei);
                    }
                }
            }

            s.show_editor_floor = has_visible_ground_floor && !has_sponza_entity_vis;

            if (active_light_count == 0U || !has_active_sun)
            {
                s.fx.clouds_coverage    = 0.0F;
                s.fx.shafts_strength    = 0.0F;
                s.fx.light_shafts       = 0.0F;
                s.fx.aerial_perspective = 0.0F;
            }
            else if (has_cloud_entity)
            {
                s.fx.clouds_coverage = entity_cloud_cov;
            }
            else
            {
                s.fx.clouds_coverage = 0.0F;
            }

            if (w_rt.pilot_scene_camera_idx >= 0 &&
                (!std::cmp_less(w_rt.pilot_scene_camera_idx, s.entities.size()) ||
                 !s.entities[static_cast<std::size_t>(w_rt.pilot_scene_camera_idx)].visible))
            {
                w_rt.pilot_scene_camera_idx = -1;
            }
            const int active_cam_idx =
                (w_rt.pilot_scene_camera_idx >= 0)
                    ? w_rt.pilot_scene_camera_idx
                    : (s.pie_playing ? scene_cam_entity_idx : -1);
            if (active_cam_idx >= 0 && std::cmp_less(active_cam_idx, s.entities.size()))
            {
                auto&       cam_ent = s.entities[static_cast<std::size_t>(active_cam_idx)];
                auto&       cam_ems = w_rt.entity_modifiers[cam_ent.handle.id];
                const auto* cam_mod = cam_ems.find<cd::studio::CameraLensModifier>();
                if (auto* cam_lt = s.scene.local(cam_ent.handle))
                {
                    if (cam_mod != nullptr && cam_mod->enabled)
                    {
                        s.cam.fov_y = std::clamp(cam_mod->fov_deg, 15.0F, 120.0F) *
                                      (3.1415926535F / 180.0F);
                    }
                    const bool follow_player_active =
                        s.pie_playing && has_player_vessel &&
                        (cam_mod == nullptr || cam_mod->follow_player_in_pie) &&
                        (w_rt.pilot_scene_camera_idx < 0);
                    if (!follow_player_active)
                    {
                        if (!s.pie_playing &&
                            (s.app_state.free_look.right_drag || s.app_state.free_look.middle_drag))
                        {
                            cam_lt->value.position = s.cam.eye;
                            s.scene.update_transforms();
                        }
                        else
                        {
                            const cd::math::Vec3f cam_pos = cam_lt->value.position;
                            cd::math::Vec3f       cam_fwd = cd::math::rotate(
                                cam_lt->value.rotation, cd::math::Vec3f { 0.0F, 0.0F, -1.0F });
                            const float fwd_len = cd::math::length(cam_fwd);
                            if (fwd_len > 1e-4F)
                            {
                                cam_fwd = cam_fwd * (1.0F / fwd_len);
                            }
                            else
                            {
                                cam_fwd = { 0.0F, 0.0F, -1.0F };
                            }
                            s.cam.eye    = cam_pos;
                            s.cam.target = cam_pos + cam_fwd * 5.0F;
                        }
                    }
                }
            }
        }

        if (s.app_state.pick.pending && s.gizmo_state.was_hovered)
        {
            s.app_state.pick.pending = false;
        }
        if (s.app_state.pick.pending)
        {
            s.app_state.pick.pending = false;
            const auto vw = static_cast<float>(window.width());
            const auto vh = static_cast<float>(window.height());
            if (vw > 0.0F && vh > 0.0F)
            {
                const bool wasd_active =
                    s.app_state.free_look.key_w || s.app_state.free_look.key_a ||
                    s.app_state.free_look.key_s || s.app_state.free_look.key_d ||
                    s.app_state.free_look.key_q || s.app_state.free_look.key_e;
                std::vector<cd_studio::EntityHit> ehits;
                ehits.reserve(s.entities.size());
                for (const auto& ent : s.entities)
                {
                    const bool skip_pick =
                        !ent.visible || ent.locked ||
                        ent.name.find("Ground_Floor") != std::string::npos ||
                        ent.name == "Ocean_Surface_Plane";
                    ehits.push_back({ skip_pick ? cd::ecs::Entity {} : ent.handle, ent.name });
                }
                std::vector<cd_studio::LightHit> lhits;
                lhits.reserve(s.lights.size());
                for (const auto& l : s.lights)
                {
                    lhits.push_back({ l.light.position, l.light.type, l.name });
                }
                cd_studio::PickInputs pin {
                    .ndc_x          = (2.0F * s.app_state.pick.x / vw) - 1.0F,
                    .ndc_y          = 1.0F - (2.0F * s.app_state.pick.y / vh),
                    .aspect         = vw / vh,
                    .cam            = s.cam,
                    .cam_pitch      = s.app_state.free_look.pitch,
                    .cam_yaw        = s.app_state.free_look.yaw,
                    .wasd_active    = wasd_active,
                    .cam_right_drag = s.app_state.free_look.right_drag,
                    .gizmo_hovered  = false,
                };
                const auto pr = cd_studio::pick_entity_or_light(pin, ehits, lhits, s.scene);
                if (pr.kind == cd_studio::PickKind::kLight)
                {
                    s.selected      = pr.index;
                    s.selected_kind = cd::studio::SelKind::kLight;
                    s.push_log(pr.log);
                }
                else if (pr.kind == cd_studio::PickKind::kEntity)
                {
                    s.selected      = pr.index;
                    s.selected_kind = cd::studio::SelKind::kEntity;
                    s.push_log(pr.log);
                }
                else
                {
                    int ground_hit_idx = -1;
                    if (!cd::studio_rt::golden::enabled())
                    {
                        const auto basis = cd::app_common::compute_camera_basis(s.cam.eye, s.cam.target);
                        const auto rdir  = cd::app_common::unproject_ndc_ray_dir(
                            basis, s.cam.fov_y, pin.aspect, pin.ndc_x, pin.ndc_y);
                        for (std::size_t gi = 0U; gi < s.entities.size(); ++gi)
                        {
                            const auto& gent = s.entities[gi];
                            if (!gent.visible || gent.locked ||
                                gent.name.find("Ground_Floor") == std::string::npos)
                            {
                                continue;
                            }
                            if (const auto* glt = s.scene.local(gent.handle))
                            {
                                const float top_y = glt->value.position.y + 0.5F * glt->value.scale.y;
                                if (rdir.y < -1e-4F && s.cam.eye.y > top_y)
                                {
                                    const float t_plane = (top_y - s.cam.eye.y) / rdir.y;
                                    const float hx = std::fabs((s.cam.eye.x + rdir.x * t_plane) - glt->value.position.x);
                                    const float hz = std::fabs((s.cam.eye.z + rdir.z * t_plane) - glt->value.position.z);
                                    if (hx <= 0.5F * glt->value.scale.x &&
                                        hz <= 0.5F * glt->value.scale.z)
                                    {
                                        ground_hit_idx = static_cast<int>(gi);
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    if (ground_hit_idx >= 0)
                    {
                        s.selected      = ground_hit_idx;
                        s.selected_kind = cd::studio::SelKind::kEntity;
                        s.push_log("[pick] entity: " +
                                   s.entities[static_cast<std::size_t>(ground_hit_idx)].name);
                    }
                    else if (s.selected >= 0)
                    {
                        s.push_log("[pick] cleared selection");
                        s.selected = -1;
                    }
                }
            }
        }

        auto frame_r = s.renderer.begin_frame();
        if (!frame_r.has_value())
        {
            if (frame_r.error().code ==
                static_cast<std::uint32_t>(
                    cd::render::render_errors::Code::kSwapchainOutOfDate))
            {
                s.needs_rebuild = true;
                continue;
            }
            break;
        }
        auto& frame = *frame_r;
        auto& cmd   = *frame.command_buffer;

        ctx.new_frame();
        const bool kHideEditorUiForGolden = cd::studio_rt::golden::enabled();

        if (!kHideEditorUiForGolden &&
            (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P) ||
             ImGui::IsKeyChordPressed(ImGuiKey_GraveAccent)))
        {
            s.palette_visible = !s.palette_visible;
            if (s.palette_visible)
            {
                s.palette_query.clear();
            }
        }

        if (!kHideEditorUiForGolden)
        {
            cd::studio::ui::draw_unified_top_bar(s, dt);

            const bool is_2d_pie_playing =
                (cd::ui::chroma::state().workspace_mode == 3 &&
                 cd::studio::studio_2d().stage.is_playing());
            if (!s.pie_playing && !is_2d_pie_playing &&
                !cd::studio::studio_world().show_project_manager)
            {
                const ImGuiViewport* mv = ImGui::GetMainViewport();
                ImGui::SetNextWindowPos(mv->WorkPos);
                ImGui::SetNextWindowSize(mv->WorkSize);
                ImGui::SetNextWindowViewport(mv->ID);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2 { 0.0F, 0.0F });
                ImGui::Begin("##cd_dockhost", nullptr,
                    ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                    ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground);
                ImGui::PopStyleVar(3);
                const ImGuiID dock_id = ImGui::GetID("cd_engine_dock");
                if (!s.dock_initialised && ImGui::DockBuilderGetNode(dock_id) == nullptr)
                {
                    ImGui::DockBuilderRemoveNode(dock_id);
                    const int flags =
                        static_cast<int>(ImGuiDockNodeFlags_DockSpace) |
                        static_cast<int>(ImGuiDockNodeFlags_PassthruCentralNode);
                    ImGui::DockBuilderAddNode(dock_id, static_cast<ImGuiDockNodeFlags>(flags));
                    ImGui::DockBuilderSetNodeSize(dock_id, mv->WorkSize);
                    ImGuiID m  = dock_id;
                    ImGuiID dl = ImGui::DockBuilderSplitNode(m, ImGuiDir_Left,  0.205F, nullptr, &m);
                    ImGuiID dr = ImGui::DockBuilderSplitNode(m, ImGuiDir_Right, 0.245F, nullptr, &m);
                    ImGuiID db = ImGui::DockBuilderSplitNode(m, ImGuiDir_Down,  0.235F, nullptr, &m);
                    ImGui::DockBuilderDockWindow("Scene Outliner",  dl);
                    ImGui::DockBuilderDockWindow("Inspector",       dr);
                    ImGui::DockBuilderDockWindow("Project & Tools", db);
                    ImGui::DockBuilderFinish(dock_id);
                    s.dock_initialised = true;
                }
                if (cd::ui::chroma::state().workspace_mode == 0 ||
                    cd::ui::chroma::state().workspace_mode == 3)
                {
                    ImGui::DockSpace(dock_id, ImVec2 { 0.0F, 0.0F },
                                     ImGuiDockNodeFlags_PassthruCentralNode);
                }
                ImGui::End();
            }

            const auto ui_hdr =
                cd::studio::render::make_hdr_frame(s.frame_idx, frame.extent, s.cam, s.fx.taa_amount);
            cd::studio::ui::draw_workspace_shell(
                s, dt, ui_hdr.vp, frame.extent.width, frame.extent.height);
            const auto post_hdr =
                cd::studio::render::make_hdr_frame(s.frame_idx, frame.extent, s.cam, s.fx.taa_amount);
            if (!s.pie_playing && !cd::studio::studio_world().show_project_manager &&
                cd::ui::chroma::state().workspace_mode == 0)
            {
                cd::studio::ui::draw_selection_and_light_overlays(s, post_hdr.vp, frame.extent);
                cd::studio::ui::update_and_draw_transform_gizmo(s, post_hdr.vp, frame.extent);
            }
            cd::studio::ui::draw_command_palette(s, frame.extent);
        }

        bool has_gltf_entity   = false;
        bool has_sponza_entity = false;
        for (const auto& ent : s.entities)
        {
            if (ent.kind == cd::studio::PrimitiveKind::kGltf)   has_gltf_entity   = true;
            if (ent.kind == cd::studio::PrimitiveKind::kSponza) has_sponza_entity = true;
        }
        if ((has_sponza_entity && !s.meshes.gltf.vb.is_valid()) ||
            (has_gltf_entity   && !s.meshes.gltf_cesium.vb.is_valid()))
        {
            auto upload_alb_lazy = [&device_ref = device](
                const std::uint8_t* rgba, std::uint32_t w, std::uint32_t h)
                -> std::pair<cd::rhi::TextureHandle, cd::rhi::TextureViewHandle>
            {
                auto t = cd_studio::create_texture_rgba8(device_ref, rgba, w, h);
                return { t.image, t.view };
            };
            cd_studio::AlbedoSlot lazy_alb_slot {
                .image_io = &s.albedo_tex.image,
                .view_io  = &s.albedo_tex.view,
                .sampler  = s.albedo_sampler,
                .upload   = upload_alb_lazy,
            };
            bool loaded_any = false;
            if (has_sponza_entity && !s.meshes.gltf.vb.is_valid())
            {
                loaded_any |= cd_studio::ensure_sponza_loaded_in_registry(
                    s.meshes, device, lazy_alb_slot, s.prim_inst, s.materials.prim,
                    s.normal_tex.view, s.mr_tex.view);
            }
            if (has_gltf_entity && !s.meshes.gltf_cesium.vb.is_valid())
            {
                loaded_any |= cd_studio::ensure_cesium_loaded_in_registry(
                    s.meshes, device, lazy_alb_slot, s.prim_inst, s.materials.prim,
                    s.normal_tex.view, s.mr_tex.view);
            }
            if (loaded_any)
            {
                if (s.meshes.has_gltf_texture || s.meshes.has_cesium_texture)
                {
                    s.has_gltf_texture = true;
                }
                cd_studio::wire_w8be_bindless_and_meta(
                    s, device, cd_studio::kBootLightUboBytes, cd::studio_rt::kInstMatBytes);
                s.last_tlas_scene_hash = 0U;
            }
        }
        const float pie_speed_scale = s.pie_playing
            ? std::clamp(cd::studio::studio_world().player_horiz_speed / 3.2F, 0.14F, 1.55F)
            : 1.0F;
        const float anim_dt = s.anim_paused
            ? 0.0F
            : (std::max(dt, 1.0F / 120.0F) * s.anim_speed * pie_speed_scale);
        if (has_gltf_entity &&
            !cd_studio::update_skinned_animation(s.meshes.cesium_skinned, device,
                                                 s.meshes.gltf_cesium.vb, anim_dt))
        {
            static float cesium_yaw_t = 0.0F;
            cesium_yaw_t += dt * 0.5F;
            for (auto& ent : s.entities)
            {
                if (ent.kind != cd::studio::PrimitiveKind::kGltf)
                {
                    continue;
                }
                auto* lt = s.scene.local(ent.handle);
                if (!lt)
                {
                    continue;
                }
                const float half = cesium_yaw_t * 0.5F;
                const float sy   = std::sin(half);
                const float cy   = std::cos(half);
                const float qx2  = -0.7071068F;
                const float qw2  = 0.7071068F;
                lt->value.rotation = {
                    cy * qx2,
                    sy * qw2,
                    sy * qx2,
                    cy * qw2
                };
                break;
            }
        }

        std::uint64_t tlas_scene_hash = 14695981039346656037ULL;
        auto hash_mix = [&](std::uint64_t v) noexcept
        {
            tlas_scene_hash ^= v;
            tlas_scene_hash *= 1099511628211ULL;
        };
        auto hash_f32 = [&](float f) noexcept
        {
            hash_mix(static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(f)));
        };
        const float effective_floor_y =
            (cd::studio_rt::golden::enabled() || !s.show_editor_floor) ? kFloorY : -0.002F;
        const auto sun = cd::studio::render::resolve_sun_light(s.lights);
        hash_mix(s.entities.size());
        hash_mix(s.show_editor_floor ? 1U : 0U);
        hash_f32(effective_floor_y);
        hash_mix(sun.has_sun ? 1U : 0U);
        hash_f32(sun.dir.x);
        hash_f32(sun.dir.y);
        hash_f32(sun.dir.z);
        if (has_gltf_entity && !s.anim_paused)
        {
            hash_mix(static_cast<std::uint64_t>(s.frame_idx));
        }
        for (const auto& ent : s.entities)
        {
            hash_mix(static_cast<std::uint64_t>(ent.handle.id));
            hash_mix(static_cast<std::uint64_t>(ent.kind));
            hash_mix(ent.visible ? 1U : 0U);
            hash_mix(ent.is_pbr ? 1U : 0U);
            hash_mix(ent.use_texture ? 1U : 0U);
            hash_mix(static_cast<std::uint64_t>(ent.custom_texture_id));
            hash_f32(ent.tint.x);
            hash_f32(ent.tint.y);
            hash_f32(ent.tint.z);
            hash_f32(ent.metallic);
            hash_f32(ent.roughness);
            hash_f32(ent.multiscatter_comp);
            hash_f32(ent.iridescence_nm);
            if (const auto* lt = s.scene.local(ent.handle))
            {
                hash_f32(lt->value.position.x);
                hash_f32(lt->value.position.y);
                hash_f32(lt->value.position.z);
                hash_f32(lt->value.rotation.x);
                hash_f32(lt->value.rotation.y);
                hash_f32(lt->value.rotation.z);
                hash_f32(lt->value.rotation.w);
                hash_f32(lt->value.scale.x);
                hash_f32(lt->value.scale.y);
                hash_f32(lt->value.scale.z);
            }
        }
        const bool tlas_dirty =
            !s.current_tlas.is_valid() || (tlas_scene_hash != s.last_tlas_scene_hash);
        if (tlas_dirty)
        {
            s.last_tlas_scene_hash = tlas_scene_hash;
        }

        cd_studio::rebuild_tlas_and_transition_depth(
            device, cmd, s.frame_idx, s.tlas_destroy_queue, s.current_tlas,
            s.prim_inst, std::span<const cd::studio::SceneEntity>(s.entities),
            [&s](cd::studio::PrimitiveKind k) { return s.blas_for_kind(k); },
            [](const cd::studio::SceneEntity& e) -> cd::math::Vec3f
            {
                if (e.kind == cd::studio::PrimitiveKind::kSponza)
                {
                    return { 0.72F, 0.60F, 0.48F };
                }
                return e.tint;
            },
            [](const cd::studio::SceneEntity& e) -> cd::studio::PrimitiveKind { return e.kind; },
            [&s](const cd::studio::SceneEntity& e) -> std::optional<cd::math::Mat4f>
            {
                if (!e.visible ||
                    e.name.find("Ground_Floor") != std::string::npos ||
                    e.name.find("Volumetric_Clouds") != std::string::npos ||
                    e.name.find("Volumetric_Fog") != std::string::npos ||
                    e.name.find("Camera") != std::string::npos)
                {
                    return std::nullopt;
                }
                auto* lt = s.scene.local(e.handle);
                if (!lt)
                {
                    return std::nullopt;
                }
                return cd::math::to_mat4(lt->value);
            },
            [&s](const cd::studio::SceneEntity& e) -> std::span<const cd::math::Vec3f>
            {
                if (e.kind == cd::studio::PrimitiveKind::kSponza)
                {
                    return { s.sponza_geom_albedos };
                }
                if (e.kind == cd::studio::PrimitiveKind::kGltf)
                {
                    return { s.cesium_geom_albedos };
                }
                return {};
            },
            [&s](const cd::studio::SceneEntity& e) -> std::span<const cd_studio::W8BEGeomMeta>
            {
                if (e.kind == cd::studio::PrimitiveKind::kSponza)
                {
                    return { s.sponza_w8be_meta };
                }
                if (e.kind == cd::studio::PrimitiveKind::kGltf)
                {
                    return { s.cesium_w8be_meta };
                }
                if (e.kind == cd::studio::PrimitiveKind::kSphere && e.is_pbr && e.use_texture)
                {
                    return { s.pbr_sphere_textured_meta };
                }
                return {};
            },
            (cd::studio_rt::golden::enabled() && s.show_editor_floor)
                ? s.meshes.blas_floor
                : cd::rhi::AccelStructureHandle {},
            s.meshes.blas_cesium,
            has_gltf_entity && s.meshes.cesium_skinned.valid,
            s.inst_mat_ssbo,
            s.rts.depth.image,
            s.depth_initialised_on_gpu,
            [](const cd::studio::SceneEntity& e) -> bool
            {
                return e.kind == cd::studio::PrimitiveKind::kSphere;
            },
            tlas_dirty,
            effective_floor_y);

        if (tlas_dirty && s.current_tlas.is_valid())
        {
            if (has_sponza_entity)
            {
                cd_studio::sync_perprim_tlas_and_ssbo(
                    s.meshes.gltf_prim_ranges,
                    s.current_tlas, s.inst_mat_ssbo,
                    cd::studio_rt::kInstMatBytes);
            }
            if (has_gltf_entity)
            {
                cd_studio::sync_perprim_tlas_and_ssbo(
                    s.meshes.gltf_cesium_prim_ranges,
                    s.current_tlas, s.inst_mat_ssbo,
                    cd::studio_rt::kInstMatBytes);
            }
        }

        const bool ddgi_wanted = (s.fx.ddgi_on || cd_studio::g_ddgi_force_on) &&
                                 s.current_tlas.is_valid() &&
                                 device.features().ray_query;
        if (ddgi_wanted)
        {
            if (!s.ddgi.inited)
            {
                (void)s.ddgi.boot(device);
            }
            if (s.ddgi.inited &&
                (!s.ddgi.bound ||
                 s.ddgi.vp_w != frame.extent.width ||
                 s.ddgi.vp_h != frame.extent.height))
            {
                (void)s.ddgi.bind(device, s.rts.hdr.view, s.rts.depth.view,
                                  s.rts.gbuf_normal.view,
                                  frame.extent.width, frame.extent.height);
            }
        }

        cd::studio::render::draw_shadow_map_pass(s, cmd, sun, kShadowMapSize, tlas_dirty);

        constexpr std::uint32_t kHdrLaneCount = 3U;
        std::unique_ptr<cd::rhi::IParallelPassRecorder> hdr_rec;
        cd::studio::HdrSceneFrame hdr_frame {};
        if (s.fx.hdr_parallel_lanes)
        {
            auto opened = cd::studio::render::begin_hdr_scene_pass_lanes(
                s, cmd, frame.extent, kHdrLaneCount);
            hdr_frame = opened.first;
            hdr_rec   = std::move(opened.second);
        }
        else
        {
            hdr_frame = cd::studio::render::begin_hdr_scene_pass(s, cmd, frame.extent);
        }
        const float           aspect = hdr_frame.aspect;
        const cd::math::Mat4f vp_unj = hdr_frame.vp_unjittered;
        const cd::math::Mat4f vp     = hdr_frame.vp;

        auto& hdr_lane0 = hdr_rec ? hdr_rec->lane(0U)
                                  : static_cast<cd::rhi::IDrawRecorder&>(cmd);
        auto& hdr_lane1 = hdr_rec ? hdr_rec->lane(1U)
                                  : static_cast<cd::rhi::IDrawRecorder&>(cmd);
        auto& hdr_lane2 = hdr_rec ? hdr_rec->lane(2U)
                                  : static_cast<cd::rhi::IDrawRecorder&>(cmd);
        if (hdr_rec)
        {
            cd::studio::render::prep_hdr_lane(hdr_lane0, frame.extent);
        }

        cd::studio::render::draw_sky_pass(hdr_lane0, s.cam, aspect, sun, s.materials.sky);
        cd::studio::render::upload_multi_light_ubo(device, s.lights_ubo, s.lights, s.counters);
        s.materials.prim.apply(hdr_lane0);
        s.prim_inst.bind(hdr_lane0, 0U);
        if (s.prim_bindless_set.is_valid())
        {
            hdr_lane0.bind_descriptor_set(1U, s.prim_bindless_set);
        }

        cd::studio::render::draw_floor_and_entities(
            s, hdr_lane0, effective_floor_y, vp, sun,
            s.show_editor_floor && !s.pie_playing);
        if (cd::studio_rt::golden::enabled() && s.show_editor_floor && s.entities.size() > 1U)
        {
            cd::studio::render::draw_planar_shadows(
                s, hdr_lane0, sun, effective_floor_y, kShadowLift, vp);
        }

        if (hdr_rec)
        {
            cd::studio::render::prep_hdr_lane(hdr_lane1, frame.extent);
            s.materials.prim.apply(hdr_lane1);
            s.prim_inst.bind(hdr_lane1, 0U);
            if (s.prim_bindless_set.is_valid())
            {
                hdr_lane1.bind_descriptor_set(1U, s.prim_bindless_set);
            }
        }
        if (cd::studio::studio_world().show_diagnostic_probes || cd::studio_rt::golden::enabled())
        {
            cd_studio::draw_ddgi_probe_overlay_3d(s, hdr_lane1, vp, sun, cd::studio::render::fill_prim_push_shared);
            cd_studio::draw_frustum_cull_overlay_3d(s, hdr_lane1, vp, sun, cd::studio::render::fill_prim_push_shared, aspect);
            cd_studio::draw_light_gizmo_overlay_3d(s, hdr_lane1, vp, sun, cd::studio::render::fill_prim_push_shared);
            cd_studio::draw_decal_obb_overlay_3d(s, hdr_lane1, vp, sun, cd::studio::render::fill_prim_push_shared);
            cd_studio::draw_csm_cascade_overlay_3d(s, hdr_lane1, vp, sun, cd::studio::render::fill_prim_push_shared);
            cd_studio::draw_cluster_heatmap_overlay_3d(s, hdr_lane1, vp, sun, cd::studio::render::fill_prim_push_shared);
            cd_studio::draw_bezier_curve_overlay_3d(s, hdr_lane1, vp, sun, cd::studio::render::fill_prim_push_shared);
            cd_studio::draw_gpu_particles_overlay_3d(s, hdr_lane1, vp, sun, cd::studio::render::fill_prim_push_shared);
            cd_studio::draw_quat_slerp_overlay_3d(s, hdr_lane1, vp, sun, cd::studio::render::fill_prim_push_shared);
            cd_studio::draw_motion_vector_overlay_3d(s, hdr_lane1, vp, sun, cd::studio::render::fill_prim_push_shared);
            cd_studio::draw_cubemap_globe_overlay_3d(s, hdr_lane1, vp, sun, cd::studio::render::fill_prim_push_shared);
            cd_studio::draw_vg_lod_overlay_3d(s, hdr_lane1, vp, sun, cd::studio::render::fill_prim_push_shared);
            cd_studio::draw_noise_heightfield_overlay_3d(s, hdr_lane1, vp, sun, cd::studio::render::fill_prim_push_shared);
            cd_studio::draw_restir_reservoir_overlay_3d(s, hdr_lane1, vp, sun, cd::studio::render::fill_prim_push_shared);
            cd_studio::draw_earth_globe_overlay_3d(s, hdr_lane1, vp, sun, cd::studio::render::fill_prim_push_shared);
            cd_studio::draw_camera_basis_overlay_3d(s, hdr_lane1, vp, sun, cd::studio::render::fill_prim_push_shared);
            cd_studio::draw_cct_sweep_overlay_3d(s, hdr_lane1, vp, sun, cd::studio::render::fill_prim_push_shared);
            cd_studio::draw_attenuation_rail_overlay_3d(s, hdr_lane1, vp, sun, cd::studio::render::fill_prim_push_shared);
            cd_studio::append_pure_line_overlays(s.fx, s.debug_lines);
            if (s.fx.ms_show_meshlets_3d)
            {
                cd_studio::append_meshlet_overlay(s.fx, s.debug_lines);
            }
        }
        if (!s.debug_lines.empty())
        {
            s.counters.increment("draws_debug_lines");
        }
        if (hdr_rec)
        {
            cd::studio::render::prep_hdr_lane(hdr_lane2, frame.extent);
        }
        s.debug_draw.flush(device, hdr_lane2, s.debug_lines, vp, s.frame_idx);
        s.debug_lines.clear();

        if (hdr_rec)
        {
            hdr_rec->finish();
            hdr_rec.reset();
        }
        else
        {
            cmd.end_render_pass();
        }
        {
            std::array<cd::rhi::TextureBarrier, 4> hb {
                cd::rhi::TextureBarrier { .texture = s.rts.hdr.image,
                    .from  = cd::rhi::ResourceState::kColorAttachment,
                    .to    = cd::rhi::ResourceState::kShaderResource,
                    .range = { 0U, 1U, 0U, 1U } },
                cd::rhi::TextureBarrier { .texture = s.rts.gbuf_normal.image,
                    .from  = cd::rhi::ResourceState::kColorAttachment,
                    .to    = cd::rhi::ResourceState::kShaderResource,
                    .range = { 0U, 1U, 0U, 1U } },
                cd::rhi::TextureBarrier { .texture = s.rts.gbuf_albedo.image,
                    .from  = cd::rhi::ResourceState::kColorAttachment,
                    .to    = cd::rhi::ResourceState::kShaderResource,
                    .range = { 0U, 1U, 0U, 1U } },
                cd::rhi::TextureBarrier { .texture = s.rts.gbuf_mr.image,
                    .from  = cd::rhi::ResourceState::kColorAttachment,
                    .to    = cd::rhi::ResourceState::kShaderResource,
                    .range = { 0U, 1U, 0U, 1U } }
            };
            cmd.barrier({}, hb);
        }

        if (ddgi_wanted && s.ddgi.inited && s.ddgi.bound)
        {
            {
                std::array<cd::rhi::TextureBarrier, 1> dsr {
                    cd::rhi::TextureBarrier { .texture = s.rts.depth.image,
                        .from  = cd::rhi::ResourceState::kDepthWrite,
                        .to    = cd::rhi::ResourceState::kShaderResource,
                        .range = { 0U, 1U, 0U, 1U } }
                };
                cmd.barrier({}, dsr);
            }
            const cd::math::Vec3f ddgi_sun_dir { -sun.dir.x, -sun.dir.y, -sun.dir.z };
            const float           ddgi_sun_k = (sun.strength > 0.0F) ? sun.strength : 1.0F;
            const cd::math::Vec3f ddgi_sun_col {
                sun.col.x * ddgi_sun_k, sun.col.y * ddgi_sun_k, sun.col.z * ddgi_sun_k
            };
            s.ddgi.execute_frame(
                device, cmd, s.rts.hdr.image, s.rts.gbuf_normal.image,
                s.current_tlas, vp, ddgi_sun_dir, ddgi_sun_col, s.frame_idx);
            {
                std::array<cd::rhi::TextureBarrier, 1> dre {
                    cd::rhi::TextureBarrier { .texture = s.rts.depth.image,
                        .from  = cd::rhi::ResourceState::kShaderResource,
                        .to    = cd::rhi::ResourceState::kDepthWrite,
                        .range = { 0U, 1U, 0U, 1U } }
                };
                cmd.barrier({}, dre);
            }
        }

        if (!s.velocity_initialised_on_gpu || s.fx.taa_amount > 0.001F || s.fx.motion_blur > 0.001F)
        {
            cd::studio::render::velocity_gbuffer_pass(
                s, cmd, frame.extent, vp_unj, !s.velocity_initialised_on_gpu);
            s.velocity_initialised_on_gpu = true;
        }
        else
        {
            std::array<cd::rhi::TextureBarrier, 1> db_to_sr {
                cd::rhi::TextureBarrier { .texture = s.rts.depth.image,
                    .from  = cd::rhi::ResourceState::kDepthWrite,
                    .to    = cd::rhi::ResourceState::kShaderResource,
                    .range = { 0U, 1U, 0U, 1U } }
            };
            cmd.barrier({}, db_to_sr);
        }

        const float scene_ev_stops = s.auto_exposure.current_ev();

        if (!s.bloom_initialised_on_gpu || s.fx.bloom_post > 0.001F)
        {
            cd::studio::render::run_bloom_chain(
                s, cmd, scene_ev_stops, !s.bloom_initialised_on_gpu);
            s.bloom_initialised_on_gpu = true;
        }

        cd::studio::render::begin_composite_pass(
            s, cmd, frame.swapchain_image_view, frame.extent, frame_loop_start, vp_unj);

        if (!kHideEditorUiForGolden &&
            cd::ui::chroma::state().workspace_mode == 3 &&
            !cd::studio::studio_world().show_project_manager &&
            s.ui_submitter && s.ui_submitter->is_valid())
        {
            cd::studio::workspace_2d::record_2d_canvas(s, cmd, frame.extent);
        }
        else if (!kHideEditorUiForGolden &&
                 (s.show_behavior_designer || s.show_ui_widgets) &&
                 s.ui_submitter && s.ui_submitter->is_valid())
        {
            const cd::ui::widgets::Rect panel_rect {
                0.0F, 0.0F,
                static_cast<float>(frame.extent.width),
                static_cast<float>(frame.extent.height)
            };
            s.ui_batcher.begin_frame();
            if (s.show_behavior_designer)
            {
                s.bd_panel.draw(s.ui_batcher, s.ui_widget_theme, panel_rect);
            }
            if (s.show_ui_widgets && s.uiw_tree_built)
            {
                constexpr float kPanelW = 320.0F;
                constexpr float kPanelH = 160.0F;
                const float     panel_x = 24.0F;
                const float     panel_y =
                    static_cast<float>(frame.extent.height) - kPanelH - 24.0F;

                s.uiw_tree.solve(s.uiw_root, kPanelW, kPanelH);
                auto shift = [&](cd::ui::layout::NodeId n)
                {
                    cd::ui::layout::Rect r = s.uiw_tree.layout(n);
                    r.x += panel_x;
                    r.y += panel_y;
                    return r;
                };
                const cd::ui::layout::Rect lr = shift(s.uiw_label);
                s.uiw_btn.set_rect(uiw_to_widget_rect(shift(s.uiw_button)));
                s.uiw_sld.set_rect(uiw_to_widget_rect(shift(s.uiw_slider)));

                cd::ui::widgets::InputState ui_input {};
                ui_input.pointer.mouse_x       = s.ui_mouse_x;
                ui_input.pointer.mouse_y       = s.ui_mouse_y;
                ui_input.pointer.left_down     = s.ui_lmb_down;
                ui_input.pointer.left_pressed  = s.ui_lmb_down && !s.ui_lmb_prev_down;
                ui_input.pointer.left_released = !s.ui_lmb_down && s.ui_lmb_prev_down;
                ui_input.focused               = false;
                (void)s.uiw_btn.tick(ui_input);
                (void)s.uiw_sld.tick(ui_input);

                s.ui_batcher.quad(panel_x, panel_y, kPanelW, kPanelH,
                                  cd::ui::renderer::Color {
                                      s.ui_widget_theme.background.r,
                                      s.ui_widget_theme.background.g,
                                      s.ui_widget_theme.background.b,
                                      s.ui_widget_theme.background.a });
                s.ui_batcher.quad(lr.x, lr.y, lr.width, lr.height,
                                  cd::ui::renderer::Color {
                                      s.ui_widget_theme.surface.r,
                                      s.ui_widget_theme.surface.g,
                                      s.ui_widget_theme.surface.b,
                                      s.ui_widget_theme.surface.a });
                cd::ui::font::Font* fp = s.ui_font.is_loaded() ? &s.ui_font : nullptr;
                s.uiw_btn.draw(s.ui_batcher, fp, s.ui_widget_theme);
                s.uiw_sld.draw(s.ui_batcher, fp, s.ui_widget_theme);
            }
            if (s.ui_submitter->upload(s.ui_batcher))
            {
                s.ui_submitter->record(cmd, frame.extent);
            }
        }

        ctx.render(cmd);
        cmd.end_render_pass();

        const bool golden_active = cd::studio_rt::golden::enabled() &&
            !cd::studio_rt::golden::options().out_png_path.empty();
        const bool  studio_capture_active = !s.config.capture_png.empty();
        std::string gameplay_seq_path {};
        bool        gameplay_seq_final = false;
        if (!s.config.capture_gameplay_dir.empty())
        {
            std::error_code ec {};
            std::filesystem::create_directories(s.config.capture_gameplay_dir, ec);
            if (s.frame_idx == 12U)
            {
                gameplay_seq_path = s.config.capture_gameplay_dir +
                                    "/01_editor_built_and_loaded_scene.png";
            }
            else if (s.frame_idx == 32U)
            {
                gameplay_seq_path = s.config.capture_gameplay_dir +
                                    "/02_pie_combat_and_jumppad.png";
            }
            else if (s.frame_idx == 58U)
            {
                gameplay_seq_path = s.config.capture_gameplay_dir +
                                    "/03_pie_moving_platforms_and_cores.png";
            }
            else if (s.frame_idx == 95U)
            {
                gameplay_seq_path  = s.config.capture_gameplay_dir +
                                     "/04_pie_victory_goal_complete.png";
                gameplay_seq_final = true;
            }
        }
        const bool capture_this_frame =
            (golden_active && s.frame_idx == cd::studio_rt::golden::options().capture_at_frame) ||
            (studio_capture_active && s.frame_idx >= s.config.capture_frame) ||
            !gameplay_seq_path.empty();
        if (capture_this_frame)
        {
            if (!cd::app_common::schedule_swapchain_readback(
                    device, cmd, frame.swapchain_image, frame.extent, s.capture_readback))
            {
                std::fprintf(stderr, "[studio] staging buffer create failed; capture aborted\n");
            }
        }

        auto end_r = s.renderer.end_frame();
        if (!end_r.has_value())
        {
            if (end_r.error().code ==
                static_cast<std::uint32_t>(
                    cd::render::render_errors::Code::kSwapchainOutOfDate))
            {
                s.needs_rebuild = true;
                continue;
            }
            break;
        }

        if (capture_this_frame && s.capture_readback.armed)
        {
            s.renderer.wait_idle();
            const auto& out_path = !gameplay_seq_path.empty()
                ? gameplay_seq_path
                : (studio_capture_active
                    ? s.config.capture_png
                    : cd::studio_rt::golden::options().out_png_path);
            const std::uint32_t cap_w = s.capture_readback.width;
            const std::uint32_t cap_h = s.capture_readback.height;
            auto wr = cd::app_common::finish_swapchain_readback_to_png(
                device, s.capture_readback, out_path);
            if (!wr.has_value())
            {
                std::fprintf(stderr, "[studio] PNG write failed: %s\n", out_path.c_str());
                request_shutdown();
                break;
            }
            const auto& w = cd::studio::studio_world();
            std::fprintf(stdout,
                "[studio] captured swapchain PNG -> %s (%ux%u | frame=%u | PIE=%d | Score=%d | Cores=%d/%d | Bots=%d | Victory=%d)\n",
                out_path.c_str(), cap_w, cap_h, s.frame_idx,
                s.pie_playing ? 1 : 0, w.game_score, w.collected_cores,
                w.total_cores_in_level, w.defeated_enemies, w.level_goal_reached ? 1 : 0);
            if (gameplay_seq_path.empty() || gameplay_seq_final)
            {
                request_shutdown();
                break;
            }
        }

        s.ui_lmb_prev_down = s.ui_lmb_down;
        ++s.frame_idx;
        s.counters.set("frame", s.frame_idx);
        if (s.config.max_frames > 0U && s.frame_idx >= s.config.max_frames)
        {
            request_shutdown();
            break;
        }
    }

    request_shutdown();
}

void StudioApp::on_shutdown() noexcept
{
    if (!state_)
    {
        return;
    }
    auto& s      = *state_;
    auto& device = *s.device_owner;

    s.renderer.wait_idle();
    cd::studio::pipeline::destroy_ingested_assets(s);

    cd::render::destroy_mesh(device, s.meshes.floor);
    s.rts.destroy(device);
    s.auto_exposure.destroy();
    s.bloom_chain.destroy(device);
    s.shadow_target.destroy(device);
    device.destroy_sampler(s.shadow_sampler);
    device.destroy_buffer(s.shadow_ubo);
    device.destroy_buffer(s.lights_ubo);
    device.destroy_buffer(s.inst_mat_ssbo);
    s.debug_draw.destroy(device);

    if (s.albedo_tex.view.is_valid())  device.destroy_texture_view(s.albedo_tex.view);
    if (s.albedo_tex.image.is_valid()) device.destroy_texture(s.albedo_tex.image);
    device.destroy_sampler(s.albedo_sampler);
    device.destroy_sampler(s.ibl_gpu.ibl_sampler);

    if (s.ibl_gpu.gpu_spec_cube.view.is_valid())
    {
        device.destroy_texture_view(s.ibl_gpu.gpu_spec_cube.view);
    }
    if (s.ibl_gpu.gpu_spec_cube.image.is_valid())
    {
        device.destroy_texture(s.ibl_gpu.gpu_spec_cube.image);
    }
    if (s.ibl_gpu.gpu_diff_cube.view.is_valid())
    {
        device.destroy_texture_view(s.ibl_gpu.gpu_diff_cube.view);
    }
    if (s.ibl_gpu.gpu_diff_cube.image.is_valid())
    {
        device.destroy_texture(s.ibl_gpu.gpu_diff_cube.image);
    }
    if (s.ibl_gpu.gpu_brdf_lut.view.is_valid())
    {
        device.destroy_texture_view(s.ibl_gpu.gpu_brdf_lut.view);
    }
    if (s.ibl_gpu.gpu_brdf_lut.image.is_valid())
    {
        device.destroy_texture(s.ibl_gpu.gpu_brdf_lut.image);
    }

    if (s.normal_tex.view.is_valid())  device.destroy_texture_view(s.normal_tex.view);
    if (s.normal_tex.image.is_valid()) device.destroy_texture(s.normal_tex.image);
    if (s.mr_tex.view.is_valid())      device.destroy_texture_view(s.mr_tex.view);
    if (s.mr_tex.image.is_valid())     device.destroy_texture(s.mr_tex.image);

    device.wait_idle();
    s.ddgi.shutdown(device);
    if (s.current_tlas.is_valid())
    {
        device.destroy_acceleration_structure(s.current_tlas);
    }
    while (!s.tlas_destroy_queue.empty())
    {
        device.destroy_acceleration_structure(s.tlas_destroy_queue.front().h);
        s.tlas_destroy_queue.pop_front();
    }
    cd_studio::destroy_meshes(device, s.meshes);

    if (s.ui_submitter)
    {
        s.ui_submitter->destroy();
    }
    s.ui_submitter.reset();
    if (s.imgui_ctx)
    {
        s.imgui_ctx->destroy();
    }
    s.imgui_ctx.reset();

    std::printf("chromodynamic_studio: clean exit (%u frames).\n", s.frame_idx);
    state_.reset();
}
