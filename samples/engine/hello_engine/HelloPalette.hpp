// =============================================================================
// HelloPalette.hpp
// -----------------------------------------------------------------------------
// hello_engine-local command-palette command registrar. Lifted out of main()
// in Marathon Run 15 phase N19.
//
// Bundles every command whose closure only touches the HelloEngineFx aggregate
// and a log-push functor.  Covers the tonemap selectors (5 commands) and the
// BRDF / FX queued toggles (6 commands).  The remaining palette commands that
// touch scene / lights / camera / streamer / palette-load stay in main() until
// a future run promotes them via larger PaletteDeps aggregates.
// =============================================================================
#pragma once

#include "HelloEngineFx.hpp"

#include <cd/editor/CommandPalette.hpp>

#include <functional>
#include <string>

namespace cd_sample {

inline void
register_fx_palette_commands(cd::editor::CommandPalette&           palette,
                             HelloEngineFx&                        fx,
                             std::function<void(std::string)>      log_push)
{
    // ---- Tonemap selectors (5) --------------------------------------------
    palette.register_command(70, "Tonemap: AGX (Sobotka 2022)",
        [&fx, log_push] { fx.tonemap_op = 3; log_push("[fx] tonemap = AGX"); });
    palette.register_command(71, "Tonemap: Hill ACES (Filament fit)",
        [&fx, log_push] { fx.tonemap_op = 1; log_push("[fx] tonemap = Hill ACES"); });
    palette.register_command(72, "Tonemap: Hable / Uncharted 2",
        [&fx, log_push] { fx.tonemap_op = 2; log_push("[fx] tonemap = Hable"); });
    palette.register_command(73, "Tonemap: Narkowicz ACES",
        [&fx, log_push] { fx.tonemap_op = 0; log_push("[fx] tonemap = Narkowicz"); });
    palette.register_command(74, "Tonemap: HDR10 PQ (ST.2084, Rec.2020)",
        [&fx, log_push] { fx.tonemap_op = 4; log_push("[fx] tonemap = HDR10 PQ (use only on HDR display)"); });

    // ---- BRDF / FX queued toggles (6) -------------------------------------
    palette.register_command(100, "BRDF: Toggle LTC-GGX area-light specular (queued v1.7)",
        [&fx, log_push]
        {
            fx.ltc_ggx_strength = (fx.ltc_ggx_strength > 0.001F) ? 0.0F : 1.0F;
            log_push(fx.ltc_ggx_strength > 0.001F
                ? "[brdf] LTC-GGX queued (v1.7 material rework)"
                : "[brdf] LTC-GGX off");
        });
    palette.register_command(101, "BRDF: Toggle Sheen (queued v1.7)",
        [&fx, log_push]
        {
            fx.sheen_strength = (fx.sheen_strength > 0.001F) ? 0.0F : 0.5F;
            log_push(fx.sheen_strength > 0.001F
                ? "[brdf] Sheen queued (v1.7 material rework)"
                : "[brdf] Sheen off");
        });
    palette.register_command(102, "BRDF: Toggle Clearcoat (queued v1.7)",
        [&fx, log_push]
        {
            fx.clearcoat_strength = (fx.clearcoat_strength > 0.001F) ? 0.0F : 0.6F;
            log_push(fx.clearcoat_strength > 0.001F
                ? "[brdf] Clearcoat queued (v1.7 material rework)"
                : "[brdf] Clearcoat off");
        });
    palette.register_command(103, "BRDF: Toggle SSS / Burley diffusion (queued v1.7)",
        [&fx, log_push]
        {
            fx.sss_strength = (fx.sss_strength > 0.001F) ? 0.0F : 0.6F;
            log_push(fx.sss_strength > 0.001F
                ? "[brdf] SSS queued (v1.7 needs neighbourhood pass)"
                : "[brdf] SSS off");
        });
    palette.register_command(104, "FX: Spawn Decal (queued v1.7)",
        [&fx, log_push]
        {
            fx.decal_count += 1.0F;
            log_push("[fx] Decal queued (v1.7 needs projector volume + GBuffer)");
        });
    palette.register_command(105, "FX: Toggle GPU Particles 10k/sec (queued v1.7)",
        [&fx, log_push]
        {
            fx.particle_emit_rate = (fx.particle_emit_rate > 0.001F) ? 0.0F : 10000.0F;
            log_push(fx.particle_emit_rate > 0.001F
                ? "[fx] GPU particles queued (v1.7 needs compute pipe)"
                : "[fx] GPU particles off");
        });
}

} // namespace cd_sample
