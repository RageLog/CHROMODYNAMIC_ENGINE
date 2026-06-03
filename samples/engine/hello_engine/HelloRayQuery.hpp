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
struct InstanceMatGpu
{
    float albedo[4];
    float emissive[4];
};

static_assert(sizeof(InstanceMatGpu) == 32, "InstanceMatGpu must be 32 B");

// phase465-perprim: per-(instance, geometry) SSBO layout for Sponza
// multi-geometry BLAS reflections. See file header for the indexing
// scheme.  Constants chosen so the SSBO size stays modest (64 KB) while
// still covering the typical hello_engine scene.
constexpr std::uint32_t kMaxGeomsPerInst = 32;
constexpr std::uint32_t kMaxInstances    = 64;
constexpr std::uint32_t kMaxInstMats  = kMaxInstances * kMaxGeomsPerInst;
constexpr std::uint32_t kInstMatBytes = kMaxInstMats * sizeof(InstanceMatGpu);

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
}

}  // namespace cd::hello_engine
