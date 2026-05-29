// =============================================================================
// CHROMODYNAMIC -- samples/engine/hello_engine/HelloEngineApp.hpp
// M2B scaffolding (phase383).
//
// HelloEngineApp derives from cd::sample::App and declares the three pure-
// virtual overrides.  M2B ships these as no-ops; M2C/M2D/M2E progressively
// migrate boot / frame / shutdown from main_legacy() into the class.
// =============================================================================
#pragma once

#include <cd/core/Result.hpp>
#include <cd/sample/App.hpp>

/// Sample application that will own the full hello_engine lifecycle once
/// the M2B->M2E migration completes.
class HelloEngineApp : public cd::sample::App
{
public:
    explicit HelloEngineApp() noexcept
        : cd::sample::App(cd::sample::AppConfig {
              .title          = "CHROMODYNAMIC - hello_engine (mega-showcase)",
              .window_width   = 1600,
              .window_height  = 900,
              .start_maximized = false,
              .enable_validation = true,
              .max_frames     = 0u,  // run until window closes (M2C wires the real loop)
          })
    {
    }

protected:
    [[nodiscard]] cd::core::Result<void> on_boot() override
    {
        // M2B stub — boot code migrates here in M2C.
        return {};
    }

    void on_frame(const cd::sample::FrameContext& /*fc*/) override
    {
        // M2B stub — frame loop migrates here in M2D.
        request_shutdown();  // one synthetic frame then done (M2A semantics until M2D wires the real loop)
    }

    void on_shutdown() noexcept override
    {
        // M2B stub — cleanup migrates here in M2E.
    }
};
