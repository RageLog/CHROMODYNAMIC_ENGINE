// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/StudioApp.hpp
//
// Source 2 toolframework2-modeled StudioApp lifecycle owner (ADR-20261112).
// Owns std::unique_ptr<cd::studio::StudioEngineState> and delegates rendering,
// scene management, physics/PIE, asset ingestion, and UI docks to compiled
// cd_editor_lib translation units.
// =============================================================================
#pragma once

#include "core/StudioConfig.hpp"

#include <cd/app/App.hpp>
#include <cd/core/Result.hpp>

#include <memory>

namespace cd::studio
{
struct StudioEngineState;
}

class StudioApp : public cd::app::App
{
public:
    explicit StudioApp(cd::studio::StudioConfig cfg = {}) noexcept;
    ~StudioApp() override;

protected:
    [[nodiscard]] cd::core::Result<void> on_boot() override;
    void on_frame(const cd::app::FrameContext& fc) override;
    void on_shutdown() noexcept override;

private:
    cd::studio::StudioConfig                       initial_cfg_ {};
    std::unique_ptr<cd::studio::StudioEngineState> state_;
};
