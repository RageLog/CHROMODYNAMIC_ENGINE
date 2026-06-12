// =============================================================================
// CHROMODYNAMIC -- engine/world/sample_framework/src/run.cpp
// Mega-Marathon M2A skeleton (Run 29 / phase373).
//
// Translation unit reserved for non-template glue (e.g. SDL_main shim
// on iOS / Android, argument-vector normalization on Windows wide-char
// command lines). M2A keeps the surface header-only; this TU exists
// only so the CMake target has a non-empty translation unit when
// consumers link statically.
// =============================================================================
#include <cd/sample/run.hpp>

namespace cd::sample
{

// Anchor so the static library always has at least one symbol exposed.
// Avoids "library contained no objects" warnings on some toolchains
// when the header-only template path is the only consumer.
extern const char k_sample_framework_build_anchor[];
const char k_sample_framework_build_anchor[] = "cd::sample::sample_framework M2A";

}  // namespace cd::sample
