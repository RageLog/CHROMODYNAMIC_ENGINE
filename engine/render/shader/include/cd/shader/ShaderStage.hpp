// =============================================================================
// CHROMODYNAMIC — cd/shader/ShaderStageDesc.hpp
// Phase 51.A / Wave 219 — typed shader stage entry-point descriptor.
//
// A ShaderStageDesc is the trio (stage_kind, entry_point, defines) needed
// to compile a single GLSL/HLSL/MSL stage. Wraps the otherwise-
// stringly-typed call sites:
//
//   ShaderStageDesc vs { Stage::kVertex,   "main", {{"USE_PBR", "1"}} };
//   ShaderStageDesc fs { Stage::kFragment, "main" };
//
// Defines are stored as (key, value) string pairs; the compiler joins
// them into a preamble.
//
// The `from_glsl` / `from_hlsl` factories pre-set the language tag for
// downstream pipeline compilation.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace cd::shader
{

enum class StageKind : std::uint8_t
{
    kVertex    = 0,
    kFragment  = 1,
    kCompute   = 2,
    kGeometry  = 3,
    kTessCtrl  = 4,
    kTessEval  = 5,
    kRayGen    = 6,
    kClosestHit = 7,
    kAnyHit    = 8,
    kMiss      = 9,
};

enum class Language : std::uint8_t
{
    kGlsl,
    kHlsl,
    kMsl,
};

struct ShaderDefine
{
    std::string key;
    std::string value;
};

struct ShaderStageDesc
{
    StageKind                  stage { StageKind::kVertex };
    Language                   language { Language::kGlsl };
    std::string                entry_point { "main" };
    std::vector<ShaderDefine>  defines;

    [[nodiscard]] static ShaderStageDesc from_glsl(StageKind s, std::string entry = "main")
    {
        return ShaderStageDesc { s, Language::kGlsl, std::move(entry), {} };
    }

    [[nodiscard]] static ShaderStageDesc from_hlsl(StageKind s, std::string entry = "main")
    {
        return ShaderStageDesc { s, Language::kHlsl, std::move(entry), {} };
    }

    void define(std::string key, std::string value = "1")
    {
        defines.push_back(ShaderDefine { std::move(key), std::move(value) });
    }
};

[[nodiscard]] inline const char* to_string(StageKind s) noexcept
{
    switch (s)
    {
        case StageKind::kVertex:     return "vertex";
        case StageKind::kFragment:   return "fragment";
        case StageKind::kCompute:    return "compute";
        case StageKind::kGeometry:   return "geometry";
        case StageKind::kTessCtrl:   return "tess_ctrl";
        case StageKind::kTessEval:   return "tess_eval";
        case StageKind::kRayGen:     return "raygen";
        case StageKind::kClosestHit: return "closesthit";
        case StageKind::kAnyHit:     return "anyhit";
        case StageKind::kMiss:       return "miss";
    }
    return "unknown";
}

}  // namespace cd::shader
