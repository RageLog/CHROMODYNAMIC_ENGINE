// =============================================================================
// HelloEnginePalette.hpp
// -----------------------------------------------------------------------------
// hello_engine-local command-palette registration for the GI / RHI / Physics /
// Script / v2.0-milestone / engineering FX-toggle command bucket.  Lifted out
// of main() in Marathon Run 16 (FINAL) phase NF2.
//
// Scope rule: this function registers ONLY the 21 "engineering-status" and
// "FX-toggle" command IDs that close over the small surface of HelloEngineFx +
// the log_push closure + the post_gtao/post_bloom Settings.  Commands that
// close over entities / lights / scene / history / kSavePath / kind_name /
// kind_from_name stay in main() because they need the file-local SceneEntity
// + LightRow + PrimitiveKind types - lifting those is a larger refactor than
// this final-marathon extract takes on.
//
// Why a flat free function?  Same pattern as register_fx_palette_commands
// (HelloPalette.hpp): callers pass typed references; the helper hides the 21
// lambda blocks + boilerplate.  No new aggregate type needed.
//
// Rendering / behavioural delta: NONE.  Every register_command call here is a
// verbatim move from main() with identical IDs, titles, capture set, and
// log_push strings.
// =============================================================================
#pragma once

#include "HelloEngineFx.hpp"

#include <cd/editor/CommandPalette.hpp>
#include <cd/post/bloom/Bloom.hpp>
#include <cd/post/gtao/Gtao.hpp>

#include <functional>
#include <string>

namespace cd_sample {

/// Register the 21 engineering / FX-toggle palette commands.  Caller still
/// owns the palette + every reference passed in; this helper just emits the
/// palette.register_command(id, title, lambda) calls.
inline void
register_engine_gi_rhi_fx_palette_commands(cd::editor::CommandPalette&             palette,
                                           HelloEngineFx&                          fx,
                                           const std::function<void(std::string)>& log_push,
                                           cd::post::gtao::Settings&                fx_gtao,
                                           cd::post::bloom::Settings&               fx_bloom)
{
    palette.register_command(
        110,
        "GI: Toggle ReSTIR DI (queued v1.7)",
        [&]
        {
            fx.restir_di_on = !fx.restir_di_on;
            log_push(fx.restir_di_on ? "[gi] ReSTIR DI queued (v1.7 needs RT compute pipe)" : "[gi] ReSTIR DI off");
        }
    );
    palette.register_command(
        111,
        "GI: Toggle ReSTIR GI (queued v1.7)",
        [&]
        {
            fx.restir_gi_on = !fx.restir_gi_on;
            log_push(fx.restir_gi_on ? "[gi] ReSTIR GI queued (v1.7 needs RT compute pipe)" : "[gi] ReSTIR GI off");
        }
    );
    palette.register_command(
        112,
        "GI: Toggle DDGI probe update (queued v1.7)",
        [&]
        {
            fx.ddgi_on = !fx.ddgi_on;
            log_push(fx.ddgi_on ? "[gi] DDGI queued (v1.7 needs probe-volume RT)" : "[gi] DDGI off");
        }
    );
    palette.register_command(
        113,
        "GI: Toggle NRC (TinyCudaNN backend, queued v1.7)",
        [&]
        {
            fx.nrc_on = !fx.nrc_on;
            log_push(fx.nrc_on ? "[gi] NRC queued (v1.7 needs CUDA inference path)" : "[gi] NRC off");
        }
    );
    palette.register_command(
        120,
        "RHI: Status (active backend + parity)",
        [&]
        {
            log_push("[rhi] active = Vulkan (production)");
            log_push("[rhi] D3D12 partial - PSO/desc/shader stubs (v1.8.1)");
            log_push("[rhi] OpenGL partial - no RT support (v1.8.2)");
            log_push("[rhi] Metal skeleton - Apple-only stub (v1.8.3)");
            log_push("[rhi] WebGPU not started (v1.8.4 via Dawn)");
            log_push("[rhi] see docs/RHI_PARITY_STATUS.md");
        }
    );
    palette.register_command(
        130,
        "Physics: Status (Jolt + cloth roadmap)",
        [&]
        {
            log_push("[phys] primitives: Aabb/Sphere/Capsule/Obb/Ray ready");
            log_push("[phys] IPhysicsWorld interface ready; Jolt impl v1.9.1");
            log_push("[phys] cloth (PBD) v1.9.3; character controller v1.9.4");
            log_push("[phys] see docs/PHYSICS_V19_PLAN.md");
        }
    );
    palette.register_command(
        131,
        "Script: Status (Lua 5.4 + AI BT roadmap)",
        [&]
        {
            log_push("[script] cd::script::Engine skeleton present");
            log_push("[script] Lua 5.4 integration v1.9.5");
            log_push("[script] behaviour-tree nodes v1.9.5");
        }
    );
    palette.register_command(
        140,
        "v2.0: Production Milestone Status",
        [&]
        {
            log_push("[v2.0] cooker (assetc):     v2.0.1 - pending");
            log_push("[v2.0] profiler:            cd::profile sinks ready (v2.0.2)");
            log_push("[v2.0] crash reporter:      cd::diag::CrashReporter ready (v2.0.3)");
            log_push("[v2.0] HRTF audio:          cd::audio core ready (v2.0.4)");
            log_push("[v2.0] i18n + a11y:         ICU integration pending (v2.0.5)");
            log_push("[v2.0] hot-reload:          vfs + shader Compiler ready (v2.0.6)");
            log_push("[v2.0] 24h stress harness:  pending CI hardware (v2.0.7)");
            log_push("[v2.0] ENGINE_GUIDE.md:     pending (v2.0.8)");
            log_push("[v2.0] see docs/PRODUCTION_V20_PLAN.md");
        }
    );
    palette.register_command(
        80,
        "FX: Toggle GTAO (inline approx)",
        [&]
        {
            fx.gtao_strength = (fx.gtao_strength > 0.001F) ? 0.0F : 0.65F;
            log_push(fx.gtao_strength > 0.001F ? "[fx] GTAO on" : "[fx] GTAO off");
        }
    );
    palette.register_command(
        81,
        "FX: Toggle Bloom (inline approx)",
        [&]
        {
            fx.bloom_strength = (fx.bloom_strength > 0.001F) ? 0.0F : 0.55F;
            log_push(fx.bloom_strength > 0.001F ? "[fx] Bloom on" : "[fx] Bloom off");
        }
    );
    palette.register_command(
        82,
        "FX: GTAO Settings (radius=1m, dirs=4)",
        [&]
        {
            fx_gtao.radius = 1.0F;
            fx_gtao.direction_count = 4;
            log_push("[fx] GTAO settings reset to defaults");
        }
    );
    palette.register_command(
        83,
        "FX: Bloom Settings (threshold=3.0, intensity=0.02)",
        [&]
        {
            fx_bloom.threshold = 3.0F;
            fx_bloom.intensity = 0.02F;
            log_push("[fx] Bloom settings reset to defaults (phase 449)");
        }
    );
    palette.register_command(
        84,
        "FX: Toggle SMAA (inline luma-edge blur)",
        [&]
        {
            fx.smaa_strength = (fx.smaa_strength > 0.001F) ? 0.0F : 0.55F;
            log_push(fx.smaa_strength > 0.001F ? "[fx] SMAA on" : "[fx] SMAA off");
        }
    );
    palette.register_command(
        85,
        "FX: Toggle Motion Blur",
        [&]
        {
            fx.motion_blur = (fx.motion_blur > 0.001F) ? 0.0F : 0.5F;
            log_push(
                fx.motion_blur > 0.001F ? "[fx] MotionBlur on (composite camera-velocity)" : "[fx] MotionBlur off"
            );
        }
    );
    palette.register_command(
        86,
        "FX: Toggle TAA",
        [&]
        {
            fx.taa_amount = (fx.taa_amount > 0.001F) ? 0.0F : 0.85F;
            log_push(fx.taa_amount > 0.001F ? "[fx] TAA on (history + Halton jitter)" : "[fx] TAA off");
        }
    );
    palette.register_command(
        87,
        "FX: Toggle DOF",
        [&]
        {
            fx.dof_strength = (fx.dof_strength > 0.001F) ? 0.0F : 0.5F;
            log_push(fx.dof_strength > 0.001F ? "[fx] DOF on (composite bokeh)" : "[fx] DOF off");
        }
    );
    palette.register_command(
        88,
        "FX: Toggle HDR10 (queued)",
        [&]
        {
            fx.hdr10_request = !fx.hdr10_request;
            log_push(fx.hdr10_request ? "[fx] HDR10 request queued (swapchain rework)" : "[fx] HDR10 off");
        }
    );
    palette.register_command(
        90,
        "FX: Toggle Height Fog (inline exp)",
        [&]
        {
            fx.fog_density = (fx.fog_density > 0.001F) ? 0.0F : 0.6F;
            log_push(fx.fog_density > 0.001F ? "[fx] Height fog on" : "[fx] Height fog off");
        }
    );
    palette.register_command(
        91,
        "FX: Toggle Aerial Perspective (inline)",
        [&]
        {
            fx.aerial_perspective = (fx.aerial_perspective > 0.001F) ? 0.0F : 0.7F;
            log_push(fx.aerial_perspective > 0.001F ? "[fx] Aerial perspective on" : "[fx] Aerial perspective off");
        }
    );
    palette.register_command(
        92,
        "FX: Toggle Clouds",
        [&]
        {
            fx.clouds_coverage = (fx.clouds_coverage > 0.001F) ? 0.0F : 0.55F;
            log_push(fx.clouds_coverage > 0.001F ? "[fx] Clouds on (composite fBm sky overlay)" : "[fx] Clouds off");
        }
    );
    palette.register_command(
        93,
        "FX: Toggle Light Shafts",
        [&]
        {
            fx.shafts_strength = (fx.shafts_strength > 0.001F) ? 0.0F : 0.5F;
            log_push(
                fx.shafts_strength > 0.001F ? "[fx] Light shafts on (Mitchell 2007 god rays)" : "[fx] Light shafts off"
            );
        }
    );
}

} // namespace cd_sample
