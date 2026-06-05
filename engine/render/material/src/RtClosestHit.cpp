// =============================================================================
// CHROMODYNAMIC — cd/material/RtClosestHit.cpp
//
// FINALE-5 W1 / A8 / phase759 — RT closest-hit shader LIVE WIRE.
//
// Packs a list of `MaterialInstance*` into the std430 `RtMaterialRecord`
// vector that the closest-hit GLSL (`kRtClosestHitGlsl`) reads via
// `gl_InstanceCustomIndexEXT`. Inert instances round-trip through
// `ray_hit_sample` so the neutral-grey 0.6 fallback contract from T1.12 is
// preserved end-to-end.
// =============================================================================
#include <cd/material/RtClosestHit.hpp>

#include <cd/material/Material.hpp>

#include <cstddef>

namespace cd::material
{

std::vector<RtMaterialRecord>
pack_rt_material_records(std::span<const MaterialInstance*> instances)
{
    std::vector<RtMaterialRecord> out;
    out.reserve(instances.size());

    for (const MaterialInstance* inst : instances)
    {
        // Route every record through `ray_hit_sample` — this preserves the
        // T1.12 defensive fallback (inert / nullptr → neutral-grey 0.6) so
        // the shader's record_count guard and the C++ packer agree
        // byte-for-byte on what an "unwired" prim looks like.
        RayHitSample sample {};
        if (inst != nullptr)
        {
            sample = ray_hit_sample(*inst);
        }
        else
        {
            // nullptr collapses to the inert-instance fallback: ray_hit_sample
            // would also return this if passed a default-constructed instance,
            // but nullptr is the more common shape callers will hand us when
            // a scene has TLAS entries without a paired MaterialInstance.
            sample = ray_hit_sample(MaterialInstance {});
        }

        RtMaterialRecord rec {};
        rec.albedo[0]   = sample.albedo[0];
        rec.albedo[1]   = sample.albedo[1];
        rec.albedo[2]   = sample.albedo[2];
        rec.albedo[3]   = sample.metallic;     // packed into .a
        rec.emissive[0] = sample.emissive[0];
        rec.emissive[1] = sample.emissive[1];
        rec.emissive[2] = sample.emissive[2];
        rec.emissive[3] = sample.roughness;    // packed into .a
        out.push_back(rec);
    }

    return out;
}

}  // namespace cd::material
