// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/core/StudioState.hpp
//
// Concrete, non-templated StudioEngineState shared across cd_editor_lib
// subsystems (ADR-20261112). Toy scaffolding (PCG32/Box-Muller histograms,
// fake net_throttle/net_snapbuf, sleep-based AsyncStreamer dummy worker,
// vt_table {4,4} dummy probe, and g_studio_* globals) has been purged.
// =============================================================================
#pragma once

#include "core/StudioConfig.hpp"
#include "core/StudioTypes.hpp"

#include "StudioAppState.hpp"
#include "StudioAudio.hpp"
#include "StudioDdgiPipeline.hpp"
#include "StudioIblBaker.hpp"
#include "StudioMaterialPipeline.hpp"
#include "StudioMeshRegistry.hpp"
#include "StudioRayTracingTypes.hpp"
#include "StudioRenderFxState.hpp"
#include "StudioRenderTargets.hpp"
#include "StudioSceneAssetIngest.hpp"
#include "StudioShaderWatch.hpp"
#include "StudioTlasBuilder.hpp"
#include "StudioTlasRing.hpp"

#include <cd/app_common/FrameCapture.hpp>
#include <cd/atmosphere/Atmosphere.hpp>
#include <cd/camera/Camera.hpp>
#include <cd/core/CounterTable.hpp>
#include <cd/ddgi/Ddgi.hpp>
#include <cd/debug_draw/DebugDraw.hpp>
#include <cd/debug_line/DebugLine.hpp>
#include <cd/ecs/World.hpp>
#include <cd/editor/AxisGizmo.hpp>
#include <cd/editor/CommandPalette.hpp>
#include <cd/editor/EditHistory.hpp>
#include <cd/editor/SelectionOutline.hpp>
#include <cd/editor/panel_behavior_designer/BehaviorDesigner.hpp>
#include <cd/framegraph/Targets.hpp>
#include <cd/light/ClusterGrid.hpp>
#include <cd/light_shafts/LightShafts.hpp>
#include <cd/material/Material.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Vector.hpp>
#include <cd/platform/Window.hpp>
#include <cd/post/bloom/Bloom.hpp>
#include <cd/post/dof/Dof.hpp>
#include <cd/post/exposure/Setup.hpp>
#include <cd/post/gtao/Gtao.hpp>
#include <cd/post/motion_blur/MotionBlur.hpp>
#include <cd/post/smaa/Smaa.hpp>
#include <cd/post/ssr/Ssr.hpp>
#include <cd/post/taa/Taa.hpp>
#include <cd/render/MeshUpload.hpp>
#include <cd/render/Renderer.hpp>
#include <cd/restir_di/Reservoir.hpp>
#include <cd/restir_gi/GiReservoir.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/scene/Scene.hpp>
#include <cd/scene/SceneCameraController.hpp>
#include <cd/shader/Compiler.hpp>
#include <cd/ui/ChromaUi.hpp>
#include <cd/ui/font/Font.hpp>
#include <cd/ui/layout/Flex.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/renderer_rhi/Submitter.hpp>
#include <cd/ui/widgets/Widgets.hpp>
#include <cd/volumetric/clouds/Clouds.hpp>
#include <cd/volumetric/fog/Fog.hpp>
#include <cd/world_container/World.hpp>

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace cd::studio
{

struct StudioEngineState
{
    explicit StudioEngineState(StudioConfig cfg = {});
    ~StudioEngineState();

    StudioEngineState(const StudioEngineState&)            = delete;
    StudioEngineState& operator=(const StudioEngineState&) = delete;
    StudioEngineState(StudioEngineState&&)                 = delete;
    StudioEngineState& operator=(StudioEngineState&&)      = delete;

    void push_log(std::string msg);

    [[nodiscard]] const cd::render::GpuMesh& mesh_for(PrimitiveKind k) const noexcept;
    [[nodiscard]] cd::rhi::AccelStructureHandle blas_for_kind(PrimitiveKind k) const noexcept;
    [[nodiscard]] std::span<const cd_studio::GltfPrimRange> prim_ranges_for(PrimitiveKind k) const noexcept;

    // CLI / launch configuration + swapchain readback state
    StudioConfig                            config {};
    cd::app_common::SwapchainReadbackState  capture_readback {};

    // Window / device / renderer / UI context
    std::unique_ptr<cd::platform::IWindow>  window;
    std::unique_ptr<cd::rhi::IDevice>       device_owner;
    cd::render::Renderer                    renderer;
    std::unique_ptr<cd::imgui::Context>     imgui_ctx;

    // Shader compiler
    std::unique_ptr<cd::shader::ICompiler>  compiler;

    // Render targets
    cd_studio::RenderTargets                rts;
    bool                                    depth_initialised_on_gpu  { false };
    bool                                    shadow_initialised_on_gpu { false };

    // Materials & shader hot-reload
    cd_studio::MaterialBundle               materials;
    cd_studio::StudioShaderWatch            shader_watch;

    // Shadow-map resources
    cd::framegraph::DepthTarget             shadow_target;
    cd::rhi::SamplerHandle                  shadow_sampler  {};
    cd::rhi::BufferHandle                   shadow_ubo      {};

    // Multi-light UBO + inst-mat SSBO
    cd::rhi::BufferHandle                   lights_ubo      {};
    cd::rhi::BufferHandle                   inst_mat_ssbo   {};

    // IBL
    cd_studio::IblBakeCpu                   ibl_cpu;
    cd_studio::IblBakeGpu                   ibl_gpu;

    // Procedural & fallback textures
    cd_studio::GpuTexture2D                 albedo_tex;
    cd_studio::GpuTexture2D                 normal_tex;
    cd_studio::GpuTexture2D                 mr_tex;
    cd::rhi::SamplerHandle                  albedo_sampler   {};
    bool                                    has_gltf_texture { false };
    bool                                    show_editor_floor{ true };

    // Material instances
    cd::material::MaterialInstance          prim_inst;
    cd::rhi::DescriptorSetHandle            prim_bindless_set {};
    cd::debug_line::LineBatch               debug_lines;
    cd::debug_draw::Renderer                debug_draw;
    std::array<cd::material::MaterialInstance, 2> composite_insts;
    cd::material::MaterialInstance          bloom_prefilter_inst;
    std::array<cd::material::MaterialInstance, 3> bloom_down_insts;
    std::array<cd::material::MaterialInstance, 3> bloom_up_insts;

    // Bloom mip chain
    cd::post::bloom::BloomMipChain          bloom_chain;

    // Meshes + BLAS / TLAS + DDGI
    cd_studio::StudioMeshRegistry           meshes;
    cd::rhi::AccelStructureHandle           current_tlas {};
    std::deque<cd_studio::DeferredTlas>     tlas_destroy_queue;
    cd_studio::StudioDdgiPipeline           ddgi;
    std::vector<cd::math::Vec3f>            sponza_geom_albedos;
    std::vector<cd_studio::W8BEGeomMeta>    sponza_w8be_meta;
    std::vector<cd::math::Vec3f>            cesium_geom_albedos;
    std::vector<cd_studio::W8BEGeomMeta>    cesium_w8be_meta;
    std::uint32_t                           cesium_bindless_slot_base { 0U };
    std::vector<cd_studio::W8BEGeomMeta>    pbr_sphere_textured_meta;
    std::vector<cd_studio::GpuTexture2D>    custom_disk_textures;
    std::unordered_map<std::string, int>    custom_disk_tex_slots;
    cd::render::GpuMesh                     custom_gltf_mesh {};
    cd::rhi::AccelStructureHandle           blas_custom_gltf {};

    // World / scene / undo history / log
    cd::ecs::World                          ecs_world;
    cd::scene::Scene                        scene   { ecs_world };
    cd::editor::EditHistory                 history;
    std::deque<std::string>                 log;
    cd::world_container::World              cd_world;
    std::vector<SceneEntity>                entities;
    std::vector<cd_studio::GenericIngest>   generic_ingests;
    int                                     selected      { 0 };
    SelKind                                 selected_kind { SelKind::kEntity };

    // Selection outline + gizmo
    cd::editor::SelectionOutline            outline;
    cd::editor::AxisGizmo                   gizmo;
    GizmoState                              gizmo_state;

    // Camera
    cd::camera::Camera                      cam;
    cd::scene::SceneCameraController        scene_cam;
    cd_studio::SampleAppState               app_state;

    // Audio
    cd_studio::AudioState                   audio_state;

    // Lights
    std::vector<LightRow>                   lights;
    cd::light::ClusterGrid                  cluster_grid;
    cd::light::ClusterGridDesc              cluster_desc;

    // Telemetry / command palette / render FX
    cd::core::CounterTable                  counters;
    cd::editor::CommandPalette              palette;
    bool                                    palette_visible { false };
    std::string                             palette_query;
    cd_studio::StudioRenderFx               fx;

    // cd::ui overlay stack (2D canvas + BehaviorDesigner + Flex widgets)
    std::unique_ptr<cd::ui::renderer_rhi::Submitter> ui_submitter;
    cd::ui::renderer::DrawBatcher           ui_batcher;
    cd::ui::widgets::Theme                  ui_widget_theme;
    cd::ui::font::Font                      ui_font;
    cd::editor::panel::behavior_designer::BehaviorDesigner bd_panel;
    bool                                    show_behavior_designer { false };
    bool                                    show_ui_widgets        { false };
    cd::ui::layout::FlexTree                uiw_tree;
    cd::ui::layout::NodeId                  uiw_root   { cd::ui::layout::kInvalidNode };
    cd::ui::layout::NodeId                  uiw_label  { cd::ui::layout::kInvalidNode };
    cd::ui::layout::NodeId                  uiw_button { cd::ui::layout::kInvalidNode };
    cd::ui::layout::NodeId                  uiw_slider { cd::ui::layout::kInvalidNode };
    cd::ui::widgets::Button                 uiw_btn { "Click me" };
    cd::ui::widgets::Slider                 uiw_sld { 0.5F };
    int                                     uiw_click_count { 0 };
    float                                   uiw_slider_val  { 0.5F };
    bool                                    uiw_tree_built  { false };

    // Post-FX settings & auto-exposure
    cd::post::gtao::Settings                fx_gtao;
    cd::post::bloom::Settings               fx_bloom;
    cd::post::ssr::Settings                 fx_ssr;
    cd::post::dof::CameraSettings           fx_dof;
    cd::post::motion_blur::Settings         fx_mblur;
    cd::post::taa::Settings                 fx_taa;
    cd::post::smaa::Settings                fx_smaa;
    cd::post::exposure::Setup               auto_exposure;

    // GI reservoirs / grid configs
    cd::restir_di::Reservoir                fx_restir_di_reservoir;
    cd::restir_gi::Reservoir                fx_restir_gi_reservoir;
    cd::ddgi::GridConfig                    fx_ddgi_grid;

    // Camera feedback / TAA history state
    PrevCamBasis                            prev_cam_basis;
    cd::math::Mat4f                         prev_vp_unjittered { cd::math::Mat4f::identity() };
    bool                                    prev_vp_valid      { false };
    std::array<cd::rhi::ResourceState, 2>   history_states
        { cd::rhi::ResourceState::kUndefined, cd::rhi::ResourceState::kUndefined };

    // Atmosphere / volumetrics settings
    cd::atmosphere::Parameters              fx_atmosphere;
    cd::light_shafts::Settings              fx_lshafts;
    cd::volumetric::clouds::Settings        fx_clouds;
    cd::volumetric::fog::GridConfig         fx_vfog;

    // Frame-loop flags
    bool                                    dock_initialised            { false };
    bool                                    mod_ctrl                    { false };
    bool                                    mod_shift                   { false };
    bool                                    needs_rebuild               { false };
    bool                                    velocity_initialised_on_gpu { false };
    bool                                    bloom_initialised_on_gpu    { false };
    std::uint64_t                           last_tlas_scene_hash        { 0 };
    std::uint32_t                           frame_idx                   { 0 };
    bool                                    pie_playing                 { false };
    bool                                    anim_paused                 { false };
    float                                   anim_speed                  { 1.0F };

    // Pointer state for cd::ui overlay widgets
    float                                   ui_mouse_x                  { 0.0F };
    float                                   ui_mouse_y                  { 0.0F };
    bool                                    ui_lmb_down                 { false };
    bool                                    ui_lmb_prev_down            { false };
};

}  // namespace cd::studio
