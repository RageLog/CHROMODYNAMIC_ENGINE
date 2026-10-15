// =============================================================================
// CHROMODYNAMIC — cd/runtime/EngineLoop.cpp
// FAZ 6 Wave 6.2 & FAZ 7 Wave 7.3 — Deterministic EngineLoop Implementation.
// =============================================================================
#include <cd/runtime/EngineLoop.hpp>

#include <cd/framegraph/FrameGraph.hpp>
#include <cd/rhi/ICommandBuffer.hpp>

#include <algorithm>
#include <utility>

namespace cd::runtime
{

EngineLoop::EngineLoop(EngineContext& ctx, float fixed_dt) noexcept
    : ctx_ { &ctx }
    , fixed_dt_ { std::max(fixed_dt, 0.001F) }
{
}

void EngineLoop::set_stage(cd::element_core::Stage stage)
{
    stage_ = std::move(stage);
    stage_.mount();
}

FrameStepReport EngineLoop::step_frame(
    float frame_dt,
    cd::rhi::IDevice* device,
    cd::rhi::TextureHandle backbuffer
)
{
    FrameStepReport report {};
    report.frame_index = ++frame_counter_;

    const float clamped_dt = std::clamp(frame_dt, 0.0F, 0.25F);
    const bool simulate = !pie_active_ || !pie_paused_;

    // 1) & 2) FixedTick accumulator loop (Vessel/Jolt/SensorVolume/NetReconcile)
    if (simulate)
    {
        fixed_accum_ += clamped_dt;
        std::uint32_t steps = 0;
        while (fixed_accum_ >= fixed_dt_ && steps < 8U)
        {
            stage_.fixed_tick(fixed_dt_);
            fixed_accum_ -= fixed_dt_;
            ++steps;
        }
        report.fixed_steps_executed = steps;

        // 3) Variable Tick (Stage/Lua/AnimGraph/IK/SynthPilot/Cutscene)
        stage_.tick(clamped_dt);
    }

    // 4) Async Streaming & HotReload Drain
    if (ctx_ != nullptr)
    {
        std::uint32_t drained = 0;
        ctx_->hot_reload().drain([&](cd::asset::AssetId) {
            ++drained;
        });
        report.hot_reloads_drained = drained;
    }

    // 5) SceneRenderer FrameGraph Execution
    if (device != nullptr && backbuffer.is_valid())
    {
        cd::framegraph::FrameGraph fg(*device);
        (void)cd::scene_renderer::build_scene_renderer_framegraph(fg, backbuffer, renderer_cfg_);
        report.framegraph_passes_compiled = fg.pass_count();
        if (fg.compile().has_value())
        {
            auto cmd = device->create_command_buffer();
            if (cmd)
            {
                cmd->begin();
                (void)fg.execute(*cmd);
                cmd->end();
            }
        }
    }

    // 6) HRTF Audio & Binaural Evaluation
    const auto sources = stage_.find_all<cd::elements_acoustics::SoundSource3D>();
    const auto ears    = stage_.find_all<cd::elements_acoustics::AudioEar3D>();
    report.active_sound_sources = sources.size();
    if (!ears.empty())
    {
        const auto ear_pos = ears[0]->world_position();
        for (const auto* src : sources)
        {
            (void)src->evaluate_hrtf_and_doppler(
                ear_pos, { 1.0F, 0.0F, 0.0F }, { 0.0F, 0.0F, 0.0F });
        }
    }

    // 7) cd::ui HudLayer Evaluation
    const auto huds = stage_.find_all<cd::elements_ui::HudLayer>();
    report.active_hud_layers = huds.size();

    return report;
}

void EngineLoop::begin_pie_session()
{
    if (pie_active_)
        return;
    pre_pie_stage_name_ = std::string { stage_.name() };
    pie_snapshot_ = stage_.serialize_cdstage();
    pie_active_ = true;
    pie_paused_ = false;
    fixed_accum_ = 0.0F;
}

void EngineLoop::stop_pie_session()
{
    if (!pie_active_)
        return;
    pie_active_ = false;
    pie_paused_ = false;
    fixed_accum_ = 0.0F;
    stage_ = cd::gameplay_director::build_showcase_stage_by_slug(pre_pie_stage_name_);
}

}  // namespace cd::runtime
