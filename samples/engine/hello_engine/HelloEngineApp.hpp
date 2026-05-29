// =============================================================================
// CHROMODYNAMIC -- samples/engine/hello_engine/HelloEngineApp.hpp
// M2C/M2D/M2E migration (phases 390-392).
//
// HelloEngineApp derives from cd::sample::App.  The three lifecycle overrides
// are declared here (non-inline); their bodies and the full EngineState
// definition live in main.cpp, defined after the anonymous-namespace types
// (SceneEntity, GizmoState, SelKind, LightRow, …) are visible.
//
// PIMPL note: EngineState is a forward-declared private nested struct.
// unique_ptr<EngineState> requires only an incomplete type at declaration
// time, but needs the complete type when the destructor is instantiated.
// The explicit out-of-line dtor (~HelloEngineApp() override) ensures the
// dtor is only instantiated in main.cpp where EngineState is complete.
// =============================================================================
#pragma once

#include <cd/core/Result.hpp>
#include <cd/sample/App.hpp>

#include <memory>

/// Sample application that owns the full hello_engine lifecycle.
class HelloEngineApp : public cd::sample::App
{
public:
    // Ctor + dtor both out-of-line so unique_ptr<EngineState> destructor
    // is only instantiated in main.cpp where EngineState is fully defined.
    explicit HelloEngineApp() noexcept;
    ~HelloEngineApp() override;

protected:
    // M2C: allocates all GPU + scene resources. Defined in main.cpp.
    [[nodiscard]] cd::core::Result<void> on_boot() override;

    // M2D: runs the real window event + GPU render loop; calls
    // request_shutdown() when the window closes. Defined in main.cpp.
    void on_frame(const cd::sample::FrameContext& fc) override;

    // M2E: releases all GPU resources. Defined in main.cpp.
    void on_shutdown() noexcept override;

private:
    // Complete definition in main.cpp (after anon-namespace types).
    struct EngineState;
    std::unique_ptr<EngineState> state_;
};
