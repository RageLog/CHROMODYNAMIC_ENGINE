// =============================================================================
// CHROMODYNAMIC — samples/engine/hello_engine/HelloRayQuery.hpp
//
// Phase 293 / Marathon Run 7 sub-N1E: ray-query / TLAS-instance data layouts
// and small helpers shared across hello_engine's TLAS rebuild and the
// kPrimFS shader's reflection-by-id branch.
//
// Types:
//   * InstanceMatGpu     — 32 B PoD matching the GLSL `struct InstanceMat`
//                          (vec4 albedo + vec4 emissive) declared in
//                          PrimShader_kPrimFS.inl at binding 10.
//   * kMaxGeomsPerInst   — phase465-perprim: per-instance geometry bucket
//                          width.  Each TLAS instance owns this many
//                          consecutive SSBO slots so a closest-hit ray that
//                          returns (instance_id, geometry_index) maps to
//                          one slot via inst*kMaxGeomsPerInst + geom.
//                          Sponza has ~28 prim ranges so 32 is the next
//                          power-of-two with headroom; non-Sponza
//                          instances replicate their instance albedo
//                          across all 32 slots.
//   * kMaxInstances      — phase465-perprim: per-frame TLAS instance cap.
//                          Bumped from 8 (old kMaxInstMats / 32) to 64 so
//                          the 16 PBR sphere grid + 5 prims + Sponza +
//                          CesiumMan + floor + future probes all fit.
//   * kMaxInstMats       — total SSBO slot cap = kMaxInstances *
//                          kMaxGeomsPerInst (2048 slots, 64 KB at 32 B
//                          per slot).
//
// Helpers:
//   * make_accel_instance(blas, model, mask) -> cd::rhi::AccelInstance —
//     converts a column-major Mat4f model into the row-major 3x4
//     transform Vulkan AS expects.
//   * fill_inst_mat(im, albedo) — populates the GPU material slot
//     from an RGB tint (alpha 1.0, emissive zero).
// =============================================================================
#pragma once

#include <cd/math/Matrix.hpp>
#include <cd/math/Vector.hpp>
#include <cd/rhi/Descriptors.hpp>

#include <cstdint>

namespace cd::hello_engine
{

// ---- TLAS-instance material table SSBO entry ------------------------------
// Filled host-side in the same loop that pushes TLAS instances, so the
// GPU index returned by rayQueryGetIntersectionInstanceIdEXT lines up
// 1:1 with cd_instance_mats.data[i] (binding 10 in PrimShader_kPrimFS.inl).
//
// phase840-W8-BE-rt-bindless-texture-sampling:
// Two new fields added so the ray-side branch can sample the per-prim
// albedo TEXTURE (binding 11 sampler2D array) rather than the per-prim
// AVG colour. Layout grows 32 B -> 48 B (kInstMatBytes 256 KB -> 384 KB,
// still trivial).
//
//   albedo_tex_slot  — index into the bindless sampler2D array
//                      (binding 11). 0xFFFFFFFFu = "no per-prim texture,
//                      fall back to the avg-colour path" (W8-BD).
//   index_offset     — base index in the Sponza index buffer (binding 13)
//                      for this prim's triangle list. Matches
//                      GltfPrimRange::index_offset. Used by the shader to
//                      reconstruct triangle UVs at the ray hit.
//   _pad             — reserved for a future field (e.g. vertex_offset
//                      when a future asset has multiple vertex buffers).
struct InstanceMatGpu
{
    // phase1046b: zero-init NSDMIs — hicpp-member-init fired once the
    // phase-1045 config fix revived the hicpp family; every other
    // field already carried an initializer. Host code overwrites
    // both before upload, so {} only hardens the partial-init path.
    float         albedo[4] {};
    float         emissive[4] {};
    std::uint32_t albedo_tex_slot { 0xFFFFFFFFu };
    std::uint32_t index_offset    { 0u };
    // phase866-2-bounce-sphere-normal: when is_sphere = 1, the
    // shader can compute an analytical hit normal as
    // `normalize(hit_pos - sphere_center_radius.xyz)` and fire a
    // SECOND reflection ray query along reflect(Ri, N) — the
    // recursive "yansımanın yansıması" path. is_sphere = 0 keeps
    // the legacy single-bounce + IBL-shine path.
    std::uint32_t is_sphere       { 0u };
    // phase886-non-sponza-bindless-prep: mesh_id selects which
    // global VB/IB the shader's bindless texture-UV interpolation
    // should index into. 0 = Sponza (sponza_vb/ib bindings 11/12),
    // 1 = CesiumMan (queued binding 14/15 in a future shader
    // extension). For now the host fills this so the SSBO carries
    // the right hint; the shader change to actually USE it is
    // queued. Sentinel/default 0 = Sponza so existing chrome
    // reflections stay correct.
    std::uint32_t mesh_id          { 0u };
    float         sphere_center_radius[4] { 0.0F, 0.0F, 0.0F, 1.0F };
};

static_assert(sizeof(InstanceMatGpu) == 64,
              "InstanceMatGpu must be 64 B after the phase866 sphere extension");
static constexpr std::uint32_t kBindlessAlbedoSlotNone = 0xFFFFFFFFu;

// phase465-perprim: per-(instance, geometry) SSBO layout for Sponza
// multi-geometry BLAS reflections. See file header for the indexing
// scheme.
//
// phase798-rt-chrome-sponza-geom-cap: kMaxGeomsPerInst raised from 32
// to 128 because the Khronos Sponza glTF has 103 primitives — every
// curtain / column / vegetation prim past index 31 was previously
// silently clamping to the SSBO's slot 31 in the fragment shader (see
// prim.frag.glsl:637 — `if (g >= kMaxGeomsPerInst) g = kMaxGeomsPerInst-1;`)
// — which made every chrome reflection of a curtain panel paint
// whatever colour happened to occupy slot 31 instead of the curtain's
// actual albedo. SSBO size grows from 64 KB → 256 KB (still trivial).
constexpr std::uint32_t kMaxGeomsPerInst = 128;
constexpr std::uint32_t kMaxInstances    = 64;
constexpr std::uint32_t kMaxInstMats  = kMaxInstances * kMaxGeomsPerInst;
constexpr std::uint32_t kInstMatBytes = kMaxInstMats * sizeof(InstanceMatGpu);

// phase807-rt-chrome-sponza-regression-test: compile-time floor for
// kMaxGeomsPerInst. The Khronos Sponza glTF currently shipped under
// assets/samples/Sponza/Sponza.gltf has 103 primitives (see boot log
// "[gltf] 103/103 primitives have baseColor textures"). Phase 798
// raised the cap from 32 -> 128 specifically to fit this prim count
// without the shader's `if (g >= kMaxGeomsPerInst) g = kMaxGeomsPerInst-1;`
// silently clamping every curtain past slot 31 to a single colour
// (the user-reported "spheres in a different universe" symptom that
// drove ADR W8-BD). The floor below catches a future refactor that
// would push the cap back below the prim count.
constexpr std::uint32_t kKhronosSponzaPrimCount = 103U;
static_assert(kMaxGeomsPerInst >= kKhronosSponzaPrimCount,
              "kMaxGeomsPerInst must cover the full Sponza primitive set "
              "so RT reflection per-prim albedos don't clamp to the last "
              "per-geom slot. See ADR W8-BD.");

// ---- make_accel_instance --------------------------------------------------
// Build a cd::rhi::AccelInstance from a BLAS handle + column-major Mat4f
// world transform. The Vulkan AS spec expects a row-major 3x4 transform
// (last row is implicit (0,0,0,1)). cd::math::Mat4f is column-major
// (see ADR-017 P4), so we transpose on the fly.
//
// instance_id maps to VkAccelerationStructureInstanceKHR::instanceCustomIndex
// and is returned by rayQueryGetIntersectionInstanceIdEXT on the GPU.
// It MUST match the slot index in the inst_mat SSBO so the shader can
// look up the correct material for RT reflection / shadow hits.
[[nodiscard]] inline cd::rhi::AccelInstance
make_accel_instance(cd::rhi::AccelStructureHandle blas,
                    const cd::math::Mat4f& m,
                    std::uint32_t instance_id = 0,
                    std::uint8_t mask = 0xFFu) noexcept
{
    cd::rhi::AccelInstance inst {};
    for (std::size_t r = 0; r < 3; ++r)
    {
        inst.transform[r * 4 + 0] = m[0][r];
        inst.transform[r * 4 + 1] = m[1][r];
        inst.transform[r * 4 + 2] = m[2][r];
        inst.transform[r * 4 + 3] = m[3][r];
    }
    inst.blas = blas;
    inst.instance_id = instance_id & 0x00FFFFFFu;  // 24-bit field
    inst.mask = mask;
    return inst;
}

// ---- fill_inst_mat --------------------------------------------------------
// Populate an InstanceMatGpu slot from an RGB albedo. Emissive set to
// zero; alpha set to 1.0. Used by the per-frame TLAS material upload.
inline void fill_inst_mat(InstanceMatGpu& im, cd::math::Vec3f albedo) noexcept
{
    im.albedo[0] = albedo.x;
    im.albedo[1] = albedo.y;
    im.albedo[2] = albedo.z;
    im.albedo[3] = 1.0F;
    im.emissive[0] = 0.0F;
    im.emissive[1] = 0.0F;
    im.emissive[2] = 0.0F;
    im.emissive[3] = 0.0F;
    // phase840-W8-BE-rt-bindless-texture-sampling: sentinel slot value
    // signals "no per-prim texture; fall back to the avg-colour path".
    // The Sponza-side wiring (phase843) overwrites this for textured
    // prims; CesiumMan, PBR grid, procedural seeds keep the sentinel.
    im.albedo_tex_slot = kBindlessAlbedoSlotNone;
    im.index_offset    = 0u;
    im.is_sphere       = 0u;
    im.mesh_id         = 0u;  // phase886: 0 = Sponza (default)
    im.sphere_center_radius[0] = 0.0F;
    im.sphere_center_radius[1] = 0.0F;
    im.sphere_center_radius[2] = 0.0F;
    im.sphere_center_radius[3] = 1.0F;
}

}  // namespace cd::hello_engine
