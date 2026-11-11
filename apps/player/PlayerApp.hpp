// =============================================================================
// CHROMODYNAMIC — apps/player/PlayerApp.hpp
//
// Concrete standalone game & showcase player application (ADR-20261112).
// Consumes cd::app_common (FrameCapture, FileTextIo) and cd::render2d::Canvas2D
// with zero duplicated PNG encoders, rasterizers, or bitmap font tables.
// =============================================================================
#pragma once

#include "PlayerConfig.hpp"

namespace cd::player
{

class PlayerApp
{
public:
    explicit PlayerApp(PlayerConfig config);

    [[nodiscard]] int run();

private:
    PlayerConfig config_;
};

}  // namespace cd::player
