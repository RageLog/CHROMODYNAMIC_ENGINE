// =============================================================================
// CHROMODYNAMIC -- engine/render/rhi/tests/test_tlas_coverage.cpp
//
// Phase 656 (M11 W2A / T1.11) -- TLAS coverage contract regression test.
//
// Locks the host-side eligibility gate (`TlasInstanceCandidate::
// tlas_eligible`) and the rhi-level "no geometry-kind filter" invariant
// on three required cases:
//
//   * A "glTF prim" candidate (BLAS handle from a glTF asset, default
//     eligibility flag) enters the live `AccelInstance` list -- this is
//     the T1.11 moment: a developer drags a glTF asset into a scene and
//     it shows up in the chrome PBR sphere's reflection without
//     per-asset boilerplate.
//
//   * A "sphere prim" candidate (BLAS handle for a built-in sphere,
//     default eligibility flag) also enters the live list -- proves the
//     filter is not gating by handle source.
//
//   * A candidate with `tlas_eligible = false` is excluded -- proves the
//     opt-out path works.  Order is preserved for surviving entries
//     (rayQueryGetIntersectionInstanceIdEXT depends on a dense ordering
//     that matches per-instance SSBO slot indices on the GPU side).
//
// The moment under test: glTF asset drop -> chrome reflection picks it
// up immediately.
// =============================================================================
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/TlasBuilder.hpp>

#include <gtest/gtest.h>

#include <array>
#include <span>
#include <vector>

namespace rhi = cd::rhi;

namespace
{

// Synthetic BLAS handles -- the rhi-level filter must not care which
// asset they came from.  Distinct value() lets us assert preservation of
// ordering / membership in the eligibility test.
constexpr rhi::AccelStructureHandle kBlasGltf   { 11u, 1u, 0u };
constexpr rhi::AccelStructureHandle kBlasSphere { 22u, 1u, 0u };
constexpr rhi::AccelStructureHandle kBlasGhost  { 33u, 1u, 0u };

rhi::TlasInstanceCandidate make_candidate(rhi::AccelStructureHandle blas,
                                          std::uint32_t              id,
                                          bool                       eligible)
{
    rhi::TlasInstanceCandidate c {};
    c.instance.blas         = blas;
    c.instance.instance_id  = id & 0x00FFFFFFu;
    c.tlas_eligible         = eligible;
    return c;
}

}  // namespace

// ---- T1.11 case 1: glTF candidate with default flag enters TLAS list ------
TEST(TlasCoverage, GltfPrimWithDefaultFlagEntersInstanceList)
{
    // Default-construct: tlas_eligible MUST be true -- the engine wants
    // new geometry to enter the TLAS without per-call setup.
    rhi::TlasInstanceCandidate gltf {};
    gltf.instance.blas        = kBlasGltf;
    gltf.instance.instance_id = 42u;
    EXPECT_TRUE(gltf.tlas_eligible)
        << "Default tlas_eligible must be true so glTF assets enter the "
           "chrome reflection without per-asset boilerplate.";

    const std::array<rhi::TlasInstanceCandidate, 1> in { gltf };
    std::vector<rhi::AccelInstance> live;
    const std::size_t n = rhi::build_tlas_instances(
        std::span<const rhi::TlasInstanceCandidate>(in), live);

    ASSERT_EQ(n, 1U);
    ASSERT_EQ(live.size(), 1U);
    EXPECT_EQ(live[0].blas.value(),    kBlasGltf.value());
    EXPECT_EQ(live[0].instance_id,     42U);
}

// ---- T1.11 case 2: sphere candidate with default flag enters TLAS list ----
TEST(TlasCoverage, SpherePrimWithDefaultFlagEntersInstanceList)
{
    const auto sphere = make_candidate(kBlasSphere, 7u, /*eligible=*/true);
    const std::array<rhi::TlasInstanceCandidate, 1> in { sphere };
    std::vector<rhi::AccelInstance> live;
    const std::size_t n = rhi::build_tlas_instances(
        std::span<const rhi::TlasInstanceCandidate>(in), live);

    ASSERT_EQ(n, 1U);
    ASSERT_EQ(live.size(), 1U);
    EXPECT_EQ(live[0].blas.value(), kBlasSphere.value())
        << "Sphere BLAS must reach the TLAS the same way a glTF BLAS does; "
           "there is no kind-based filter at the rhi level.";
    EXPECT_EQ(live[0].instance_id, 7U);
}

// ---- T1.11 case 3: tlas_eligible=false is excluded -----------------------
TEST(TlasCoverage, IneligibleCandidateIsExcludedAndOrderingIsPreserved)
{
    // Mixed batch: gltf (true), ghost-shadow (false), sphere (true).
    // The ghost-shadow slot must drop out; the surviving entries must
    // retain their relative ordering -- the GPU side relies on a dense
    // ordering aligned with the per-instance SSBO that
    // rayQueryGetIntersectionInstanceIdEXT indexes into.
    const std::array<rhi::TlasInstanceCandidate, 3> in {
        make_candidate(kBlasGltf,   100u, /*eligible=*/true),
        make_candidate(kBlasGhost,  200u, /*eligible=*/false),
        make_candidate(kBlasSphere, 300u, /*eligible=*/true),
    };

    std::vector<rhi::AccelInstance> live;
    const std::size_t n = rhi::build_tlas_instances(
        std::span<const rhi::TlasInstanceCandidate>(in), live);

    ASSERT_EQ(n, 2U);
    ASSERT_EQ(live.size(), 2U);
    EXPECT_EQ(live[0].blas.value(),    kBlasGltf.value())
        << "First eligible entry must keep its slot at the head of the live list.";
    EXPECT_EQ(live[0].instance_id,     100U);
    EXPECT_EQ(live[1].blas.value(),    kBlasSphere.value())
        << "Sphere entry must slide forward into the ghost-shadow's vacated slot.";
    EXPECT_EQ(live[1].instance_id,     300U);

    // The excluded ghost-shadow must NOT appear anywhere.
    for (const auto& inst : live)
    {
        EXPECT_NE(inst.blas.value(), kBlasGhost.value())
            << "tlas_eligible=false candidate leaked into the TLAS instance list.";
    }
}

// ---- Bonus: by-value overload returns equivalent result ------------------
TEST(TlasCoverage, ByValueOverloadMatchesScratchVectorOverload)
{
    const std::array<rhi::TlasInstanceCandidate, 2> in {
        make_candidate(kBlasGltf,   1u, /*eligible=*/true),
        make_candidate(kBlasGhost,  2u, /*eligible=*/false),
    };

    std::vector<rhi::AccelInstance> live_scratch;
    rhi::build_tlas_instances(
        std::span<const rhi::TlasInstanceCandidate>(in), live_scratch);

    const auto live_returned = rhi::build_tlas_instances(
        std::span<const rhi::TlasInstanceCandidate>(in));

    ASSERT_EQ(live_scratch.size(), live_returned.size());
    ASSERT_EQ(live_scratch.size(), 1U);
    EXPECT_EQ(live_scratch[0].blas.value(), live_returned[0].blas.value());
    EXPECT_EQ(live_scratch[0].instance_id,  live_returned[0].instance_id);
}

// ---- ABI guard: TlasInstanceCandidate is host-side only -------------------
// The 64-byte AccelInstance ABI must not change.  TlasInstanceCandidate
// is a host-side wrapper, NOT something the driver sees -- it sits a
// strict layer above the VkAccelerationStructureInstanceKHR mirror.
TEST(TlasCoverage, AccelInstanceAbiStaysAt64Bytes)
{
    static_assert(sizeof(rhi::AccelInstance) == 64,
                  "AccelInstance must remain 64 B; tlas_eligible lives on "
                  "the host-side TlasInstanceCandidate wrapper, never on "
                  "the driver-facing struct.");
    EXPECT_EQ(sizeof(rhi::AccelInstance), 64U);
}
