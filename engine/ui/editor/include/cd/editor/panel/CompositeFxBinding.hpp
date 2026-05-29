// =============================================================================
// CHROMODYNAMIC -- cd/editor/panel/CompositeFxBinding.hpp
// ADR-005 namespace + ADR-016 D1 replace-ready surface.
//
// Non-owning binding from a CompositePreset to host-side post-fx
// fields.  Lets the UI button widget call apply(binding, preset)
// without knowing the layout of the consumer FX state struct.
//
// The sample uses cd_sample::HelloEngineFx; an editor binary or
// tooling consumer may use its own FX struct (or none at all).  This
// header is pure CPU + standard library; no ImGui, no RHI.
//
// Marathon Run 23 phase N5A.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/editor/panel/CompositePresets.hpp>

#include <cstdint>

namespace cd::editor::panel
{

// ---- CompositeFxBinding ---------------------------------------------------
// Pointer-bundle binding.  Every field is a non-owning raw pointer to
// the host FX scalar.  A null pointer means "this slot is read-only /
// not applicable" and is silently skipped by apply().
//
// Caller wires the binding once on boot; thereafter apply() is a
// vector store with no allocations or branches beyond null guards
// and the per-field engaged-optional gate.
struct CompositeFxBinding
{
    float*         exposure           { nullptr };
    float*         saturation_boost   { nullptr };
    float*         bloom_post         { nullptr };
    float*         ao_strength        { nullptr };
    float*         dof_strength       { nullptr };
    float*         shafts_strength    { nullptr };
    float*         ssr_strength       { nullptr };
    float*         motion_blur        { nullptr };
    float*         taa_amount         { nullptr };
    float*         clouds_coverage    { nullptr };
    float*         fog_density        { nullptr };
    float*         aerial_perspective { nullptr };
    float*         chromab_strength   { nullptr };
    float*         film_grain         { nullptr };
    float*         vignette_strength  { nullptr };
    std::int32_t*  tonemap_op         { nullptr };  ///< int repr of TonemapOp enum
};

// ---- apply ----------------------------------------------------------------
// Copy preset values into the bound fields.  Both gates are honoured:
// a null binding pointer (consumer omits the knob) AND an empty
// preset optional (preset deliberately skips the field) are silently
// skipped.
inline void apply(const CompositeFxBinding& b, const CompositePreset& p) noexcept
{
    if (b.exposure && p.exposure)
        *b.exposure = *p.exposure;
    if (b.saturation_boost && p.saturation_boost)
        *b.saturation_boost = *p.saturation_boost;
    if (b.bloom_post && p.bloom_post)
        *b.bloom_post = *p.bloom_post;
    if (b.ao_strength && p.ao_strength)
        *b.ao_strength = *p.ao_strength;
    if (b.dof_strength && p.dof_strength)
        *b.dof_strength = *p.dof_strength;
    if (b.shafts_strength && p.shafts_strength)
        *b.shafts_strength = *p.shafts_strength;
    if (b.ssr_strength && p.ssr_strength)
        *b.ssr_strength = *p.ssr_strength;
    if (b.motion_blur && p.motion_blur)
        *b.motion_blur = *p.motion_blur;
    if (b.taa_amount && p.taa_amount)
        *b.taa_amount = *p.taa_amount;
    if (b.clouds_coverage && p.clouds_coverage)
        *b.clouds_coverage = *p.clouds_coverage;
    if (b.fog_density && p.fog_density)
        *b.fog_density = *p.fog_density;
    if (b.aerial_perspective && p.aerial_perspective)
        *b.aerial_perspective = *p.aerial_perspective;
    if (b.chromab_strength && p.chromab_strength)
        *b.chromab_strength = *p.chromab_strength;
    if (b.film_grain && p.film_grain)
        *b.film_grain = *p.film_grain;
    if (b.vignette_strength && p.vignette_strength)
        *b.vignette_strength = *p.vignette_strength;
    if (b.tonemap_op && p.tonemap_op)
        *b.tonemap_op = static_cast<std::int32_t>(*p.tonemap_op);
}

// ---- apply by id ----------------------------------------------------------
// Convenience overload taking a preset id; resolves through the table
// and forwards to the preset overload above.
inline void apply(const CompositeFxBinding& b, PresetId id) noexcept
{
    apply(b, preset_by_id(id));
}

}  // namespace cd::editor::panel
