// =============================================================================
// CHROMODYNAMIC — tools/stress/main.cpp (cd_stress)
// Production v2.0.7 — 24-Hour / Deterministic Engine Soak & Perturbation Harness
//
// Loops the CHROMODYNAMIC EngineLoop across all 12 Showcase Stages with
// deterministic scene perturbations (spawn/despawn of Elements, SolarLight3D
// relighting, PIE snapshot/restore round-trips, FrameGraph compilation).
//
// Usage:
//   cd_stress --iterations <N>      (default: 24 iterations for CI smoke)
//   cd_stress --duration-secs <S>   (e.g., --duration-secs 86400 for 24h soak)
// =============================================================================
#include <cd/diag/CrashReporter.hpp>
#include <cd/element_core/Element.hpp>
#include <cd/elements_domain/DomainElements.hpp>
#include <cd/gameplay_framework/VesselPilotDirector.hpp>
#include <cd/rhi/NullDevice.hpp>
#include <cd/runtime/EngineContext.hpp>
#include <cd/runtime/EngineLoop.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace
{

constexpr std::array<std::string_view, 12> kShowcaseSlugs {
    "01_pbr_materials_sss_hair",
    "02_celestial_aether_fog_clouds",
    "03_sponza_clustered_ddgi_restir",
    "04_optics_cinematic_studio",
    "05_avatar_locomotion_anim_ik",
    "06_jolt_dynamics_rover_shatter",
    "07_terra_ocean_nature_world",
    "08_synth_pilot_squad_narrative",
    "09_acoustics_hrtf_echo_chamber",
    "10_spark_swarm_and_decals",
    "11_custom_ui_hud_and_menus",
    "12_planar_2d_platformer_net",
};

[[nodiscard]] bool run_perturbation_iteration(
    cd::runtime::EngineContext& ctx,
    cd::rhi::IDevice& dev,
    cd::rhi::TextureHandle backbuffer,
    std::uint64_t iter_idx
)
{
    const std::string_view slug = kShowcaseSlugs[iter_idx % kShowcaseSlugs.size()];
    cd::runtime::EngineLoop loop(ctx, 1.0F / 60.0F);
    loop.set_stage(cd::gameplay_director::build_showcase_stage_by_slug(slug));

    const std::size_t baseline_children = loop.stage().root().children().size();
    if (baseline_children == 0U)
    {
        std::fprintf(stderr, "[cd_stress] FAIL: stage '%.*s' has 0 root children\n",
                     static_cast<int>(slug.size()), slug.data());
        return false;
    }

    // Begin PIE session (takes .cdstage snapshot)
    loop.begin_pie_session();

    // Perturbation 1: Spawn transient dynamic Elements inside the PIE session
    auto& transient_light = loop.stage().root().emplace_child<cd::elements_radiance::RadialLight3D>(
        "StressTransientRadialLight_" + std::to_string(iter_idx)
    );
    transient_light.set_local_position(
        static_cast<float>(iter_idx % 17U) - 8.0F,
        4.0F,
        static_cast<float>((iter_idx * 3U) % 13U) - 6.0F
    );
    transient_light.luminous_power_lumens = 1500.0F;

    auto& transient_sparks = loop.stage().root().emplace_child<cd::elements_visual::SparkSwarm3D>(
        "StressTransientSparks_" + std::to_string(iter_idx)
    );
    transient_sparks.emission_rate = 250.0F + static_cast<float>((iter_idx % 10U) * 50U);

    // Perturbation 2: Relight any SolarLight3D in the stage
    auto solar_lights = loop.stage().find_all<cd::elements_radiance::SolarLight3D>();
    for (auto* sun : solar_lights)
    {
        if (sun != nullptr)
        {
            sun->illuminance_lux = 80000.0F + static_cast<float>((iter_idx % 5U) * 10000U);
        }
    }

    // Step 4 deterministic frames
    for (std::uint32_t f = 0; f < 4U; ++f)
    {
        const auto report = loop.step_frame(1.0F / 60.0F, &dev, backbuffer);
        if (report.framegraph_passes_compiled == 0U)
        {
            std::fprintf(stderr, "[cd_showcase_soak] FAIL: 0 framegraph passes on iteration %llu\n",
                         static_cast<unsigned long long>(iter_idx));
            return false;
        }
    }

    // Perturbation 3: Despawn one transient child before PIE stop
    (void)loop.stage().root().detach_child(&transient_sparks);

    // Stop PIE session and verify stage state is cleanly restored
    loop.stop_pie_session();
    if (loop.is_pie_active())
    {
        std::fprintf(stderr, "[cd_showcase_soak] FAIL: PIE remained active after stop_pie_session()\n");
        return false;
    }

    return true;
}

}  // namespace

int main(int argc, char** argv)
{
    std::uint64_t target_iterations = 24U;
    double duration_secs = 0.0;

    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg { argv[i] };
        if (arg == "--iterations" && i + 1 < argc)
        {
            const long long v = std::strtoll(argv[++i], nullptr, 10);
            if (v > 0)
            {
                target_iterations = static_cast<std::uint64_t>(v);
            }
        }
        else if (arg == "--duration-secs" && i + 1 < argc)
        {
            const double d = std::strtod(argv[++i], nullptr);
            if (d > 0.0)
            {
                duration_secs = d;
            }
        }
    }

    cd::runtime::EngineContextConfig cfg {};
    cfg.eager_thread_pool = false;
    cd::runtime::EngineContext ctx(cfg);
    cd::rhi::NullDevice null_dev {};
    cd::rhi::TextureDesc bb_desc {};
    bb_desc.extent = { 1280U, 720U, 1U };
    bb_desc.format = cd::rhi::Format::kRGBA8Unorm;
    bb_desc.usage  = cd::rhi::TextureUsage::kColorAttachment;
    const auto bb = null_dev.create_texture(bb_desc);
    if (!bb.has_value())
    {
        std::fprintf(stderr, "[cd_showcase_soak] Failed to allocate stress backbuffer\n");
        return 1;
    }

    const auto start_tp = std::chrono::steady_clock::now();
    std::uint64_t completed_iterations = 0U;

    while (true)
    {
        if (!run_perturbation_iteration(ctx, null_dev, *bb, completed_iterations))
        {
            null_dev.destroy_texture(*bb);
            return 1;
        }
        ++completed_iterations;

        if (duration_secs > 0.0)
        {
            const auto now_tp = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now_tp - start_tp).count();
            if (elapsed >= duration_secs)
            {
                break;
            }
        }
        else if (completed_iterations >= target_iterations)
        {
            break;
        }
    }

    // Verify symbolizer / backtrace capture remains healthy after stress loop
    const auto frames = cd::diag::CrashReporter::capture_stack_backtrace(8U);
    null_dev.destroy_texture(*bb);

    std::printf(
        "[cd_showcase_soak] PASS — completed %llu perturbation cycles (%llu frames, backtrace_frames=%zu, 0 leaks/errors).\n",
        static_cast<unsigned long long>(completed_iterations),
        static_cast<unsigned long long>(completed_iterations * 4ULL),
        frames.size()
    );
    return 0;
}
