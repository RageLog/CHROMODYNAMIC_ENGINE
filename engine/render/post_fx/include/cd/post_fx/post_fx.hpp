// =============================================================================
// CHROMODYNAMIC — cd/post_fx/post_fx.hpp
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

#include <cd/post_bloom/Bloom.hpp>
#include <cd/post_camera/Camera.hpp>
#include <cd/post_composite/Composite.hpp>
#include <cd/post_dof/Dof.hpp>
#include <cd/post_gtao/Gtao.hpp>
#include <cd/post_motion_blur/MotionBlur.hpp>
#include <cd/post_smaa/Smaa.hpp>
#include <cd/post_ssr/Ssr.hpp>
#include <cd/post_taa/Taa.hpp>
#include <cd/post_tonemap/Tonemap.hpp>
#include <cd/light_shafts/LightShafts.hpp>

namespace cd::post_fx
{
// Re-export tag — empty namespace so consumers can write
// `using namespace cd::post_fx;` to ADL across member namespaces.
}
