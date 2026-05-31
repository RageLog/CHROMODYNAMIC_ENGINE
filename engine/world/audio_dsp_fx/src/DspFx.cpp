// =============================================================================
// CHROMODYNAMIC — cd/audio/dsp_fx/DspFx.cpp
// Phase 562 — cd::audio::dsp_fx implementation TU.
//
// All classes in DspFx.hpp are header-only for Sprint-1 (coefficient maths
// + span loops). This TU exists as the mandatory non-empty compilation unit
// for the cd_audio_dsp_fx STATIC library target, and will hold Sprint-2+
// implementation detail that should not live in headers (e.g. Schroeder FDN
// tuning tables, SIMD-accelerated filter banks).
// =============================================================================
#include <cd/audio/dsp_fx/DspFx.hpp>

// Explicit instantiation anchors to avoid LTO-stripping the vtable-less
// classes in aggressive whole-program-optimization builds.  These are
// deliberately empty — the classes have no virtual methods; the TU just
// ensures the object file is non-trivial.

namespace cd::audio::dsp_fx
{

// Sprint-2 implementation will live here.

}  // namespace cd::audio::dsp_fx
