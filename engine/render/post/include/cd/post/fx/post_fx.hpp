// =============================================================================
// CHROMODYNAMIC — cd/post/fx/post_fx.hpp
//
// Convenience aggregator — including this header pulls in every post-fx
// library cd::post_fx fronts. Consumers that want only one of them can
// keep including the specific header directly.
//
// Library namespaces are preserved:
//   cd::post_bloom       — Bloom.hpp
//   cd::post_camera      — Camera.hpp
//   cd::post_composite   — Composite.hpp
//   cd::post_dof         — Dof.hpp
//   cd::post_gtao        — Gtao.hpp
//   cd::post_motion_blur — MotionBlur.hpp
//   cd::post_smaa        — Smaa.hpp
//   cd::post_ssr         — Ssr.hpp
//   cd::post_taa         — Taa.hpp
//   cd::post_tonemap     — Tonemap.hpp
//   cd::light_shafts     — LightShafts.hpp
// =============================================================================
#pragma once

#include <cd/post/bloom/Bloom.hpp>
#include <cd/post/camera/Camera.hpp>
#include <cd/post/composite/Composite.hpp>
#include <cd/post/dof/Dof.hpp>
#include <cd/post/gtao/Gtao.hpp>
#include <cd/post/motion_blur/MotionBlur.hpp>
#include <cd/post/smaa/Smaa.hpp>
#include <cd/post/ssr/Ssr.hpp>
#include <cd/post/taa/Taa.hpp>
#include <cd/post/tonemap/Tonemap.hpp>
#include <cd/light_shafts/LightShafts.hpp>

namespace cd::post::fx
{
// Re-export tag — empty namespace so consumers can write
// `using namespace cd::post::fx;` to ADL across member namespaces.
}
