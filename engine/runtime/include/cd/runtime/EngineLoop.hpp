// =============================================================================
// CHROMODYNAMIC — cd/runtime/EngineLoop.hpp
// FAZ 6 Wave 6.2 & FAZ 7 Wave 7.3 — Deterministic EngineLoop Orchestrator &
// Play-In-Editor (PIE) Stage Snapshot / Restore.
//
// Standard Frame Order:
//   Input -> Pilot -> FixedTick (Vessel/Jolt/SensorVolume/NetReconcile)
//         -> Tick (Stage/Lua/AnimGraph/IK/SynthPilot/Cutscene)
//         -> Async Streaming & HotReload Drain
//         -> SceneRenderer FrameGraph
//         -> HRTF Audio & Binaural Evaluation
//         -> cd::ui HudLayer Evaluation
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/element_core/Element.hpp>
#include <cd/gameplay_framework/VesselPilotDirector.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/runtime/EngineContext.hpp>
#include <cd/scene_renderer/SurfaceAndPostFxPipeline.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace cd::runtime
{

struct FrameStepReport
{
    std::uint64_t frame_index { 0 };
    std::uint32_t fixed_steps_executed { 0 };
    std::uint32_t hot_reloads_drained { 0 };
    std::size_t   framegraph_passes_compiled { 0 };
    std::size_t   active_sound_sources { 0 };
    std::size_t   active_hud_layers { 0 };
};

class EngineLoop
{
public:
    explicit EngineLoop(EngineContext& ctx, float fixed_dt = 1.0F / 60.0F) noexcept;

    void set_stage(cd::element_core::Stage stage);
    [[nodiscard]] cd::element_core::Stage& stage() noexcept { return stage_; }
    [[nodiscard]] const cd::element_core::Stage& stage() const noexcept { return stage_; }

    [[nodiscard]] cd::scene_renderer::SceneRendererConfig& renderer_config() noexcept
    {
        return renderer_cfg_;
    }

    /// Execute one deterministic engine frame across all subsystems.
    FrameStepReport step_frame(
        float frame_dt,
        cd::rhi::IDevice* device = nullptr,
        cd::rhi::TextureHandle backbuffer = {}
    );

    // ---- Play-In-Editor (PIE) Lifecycle (FAZ 7 Wave 7.3) -------------------

    void begin_pie_session();
    void pause_pie_session(bool paused) noexcept { pie_paused_ = paused; }
    void stop_pie_session();

    [[nodiscard]] bool is_pie_active() const noexcept { return pie_active_; }
    [[nodiscard]] bool is_pie_paused() const noexcept { return pie_paused_; }
    [[nodiscard]] std::string_view pie_snapshot_cdstage() const noexcept { return pie_snapshot_; }

    [[nodiscard]] std::uint64_t total_frames() const noexcept { return frame_counter_; }

private:
    EngineContext* ctx_ { nullptr };
    cd::element_core::Stage stage_ { "DefaultStage" };
    cd::scene_renderer::SceneRendererConfig renderer_cfg_ {};
    float fixed_dt_ { 1.0F / 60.0F };
    float fixed_accum_ { 0.0F };
    std::uint64_t frame_counter_ { 0 };
    bool pie_active_ { false };
    bool pie_paused_ { false };
    std::string pie_snapshot_ {};
    std::string pre_pie_stage_name_ {};
};

}  // namespace cd::runtime
