// =============================================================================
// CHROMODYNAMIC -- samples/engine/hello_engine/tests/test_hello_engine_w8be_layout.cpp
//
// phase845-W8-BE-rt-bindless-texture-sampling: regression nets for the
// W8-BE host-side data structures. Locks the InstanceMatGpu layout
// (must match the shader's std430 stride), the W8BEGeomMeta default-
// sentinel contract, and the SSBO size math.
//
// Wrong layout here = misaligned per-prim SSBO reads in the fragment
// shader = chrome reflections paint garbage colours / textures. The
// 32 B → 48 B extension that landed in phase840 is exactly the kind
// of "looks fine but actually drifts the shader index" failure mode
// these tests catch at compile + boot time, not at render time.
// =============================================================================

#include "../HelloRayQuery.hpp"
#include "../HelloTlasRebuild.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <type_traits>

// ===========================================================================
// InstanceMatGpu layout — must match shader's GLSL `struct InstanceMat`
// (vec4 albedo + vec4 emissive + uint albedo_tex_slot + uint
// index_offset + uint _pad0 + uint _pad1 = 48 B std430).
// ===========================================================================

TEST(HelloEngineW8BELayout, InstanceMatGpuIsFortyEightBytes)
{
    EXPECT_EQ(sizeof(cd::hello_engine::InstanceMatGpu), 48U);
}

TEST(HelloEngineW8BELayout, InstanceMatGpuFieldOffsetsMatchShader)
{
    using IM = cd::hello_engine::InstanceMatGpu;
    EXPECT_EQ(offsetof(IM, albedo),          0U);
    EXPECT_EQ(offsetof(IM, emissive),        16U);
    EXPECT_EQ(offsetof(IM, albedo_tex_slot), 32U);
    EXPECT_EQ(offsetof(IM, index_offset),    36U);
    EXPECT_EQ(offsetof(IM, _pad),            40U);
}

TEST(HelloEngineW8BELayout, InstanceMatGpuDefaultsToSentinel)
{
    // The default-constructed (zero-init in the SSBO scratch path)
    // InstanceMatGpu should carry the sentinel slot — the shader
    // reads it and falls through to the W8-BD per-prim avg-colour
    // path. This is the safe default for non-Sponza prims.
    cd::hello_engine::InstanceMatGpu im {};
    EXPECT_EQ(im.albedo_tex_slot, cd::hello_engine::kBindlessAlbedoSlotNone);
    EXPECT_EQ(im.index_offset,    0U);
}

TEST(HelloEngineW8BELayout, FillInstMatPreservesSentinel)
{
    // fill_inst_mat zero-initializes the new fields. Sponza prims
    // overwrite the slot in HelloTlasRebuild's per-(instance, geom)
    // SSBO loop; non-Sponza prims keep the sentinel.
    cd::hello_engine::InstanceMatGpu im {};
    im.albedo_tex_slot = 99U;  // garbage from a prior frame
    im.index_offset    = 12345U;
    cd::hello_engine::fill_inst_mat(im, cd::math::Vec3f { 0.5F, 0.5F, 0.5F });
    EXPECT_EQ(im.albedo_tex_slot, cd::hello_engine::kBindlessAlbedoSlotNone);
    EXPECT_EQ(im.index_offset,    0U);
}

// ===========================================================================
// kBindlessAlbedoSlotNone sentinel — must equal 0xFFFFFFFFu so it
// matches the GLSL `const uint kBindlessAlbedoSlotNone = 0xFFFFFFFFu`.
// ===========================================================================

TEST(HelloEngineW8BELayout, BindlessAlbedoSlotNoneIsUint32Max)
{
    EXPECT_EQ(cd::hello_engine::kBindlessAlbedoSlotNone, 0xFFFFFFFFu);
    EXPECT_EQ(cd::hello_engine::kBindlessAlbedoSlotNone,
              std::numeric_limits<std::uint32_t>::max());
}

// ===========================================================================
// SSBO size math — kMaxInstMats * 48 B = 384 KB. Trivial on modern
// GPUs but the math must be right for the layout / write path.
// ===========================================================================

TEST(HelloEngineW8BELayout, SsboTotalSizeIsConsistent)
{
    EXPECT_EQ(cd::hello_engine::kMaxInstMats,
              cd::hello_engine::kMaxInstances *
              cd::hello_engine::kMaxGeomsPerInst);
    EXPECT_EQ(cd::hello_engine::kInstMatBytes,
              cd::hello_engine::kMaxInstMats *
              sizeof(cd::hello_engine::InstanceMatGpu));
    // The 384 KiB figure cited in ADR W8-BE.
    EXPECT_EQ(cd::hello_engine::kInstMatBytes, 8192U * 48U);
}

// ===========================================================================
// W8BEGeomMeta layout — small 8 B POD. The default-constructed value
// must carry the sentinel slot so the per-prim SSBO loop's fall-through
// case (entity has no per-geom W8-BE metadata) keeps the W8-BD path
// active for non-Sponza prims.
// ===========================================================================

TEST(HelloEngineW8BELayout, W8BEGeomMetaIsEightBytes)
{
    EXPECT_EQ(sizeof(cd_sample::W8BEGeomMeta), 8U);
}

TEST(HelloEngineW8BELayout, W8BEGeomMetaDefaultsToSentinel)
{
    cd_sample::W8BEGeomMeta m {};
    EXPECT_EQ(m.albedo_tex_slot, cd::hello_engine::kBindlessAlbedoSlotNone);
    EXPECT_EQ(m.index_offset,    0U);
}

TEST(HelloEngineW8BELayout, W8BEGeomMetaIsTriviallyCopyable)
{
    // Required because the SSBO compaction loop blits these by
    // value into scratch arrays.
    EXPECT_TRUE(std::is_trivially_copyable_v<cd_sample::W8BEGeomMeta>);
    EXPECT_TRUE(std::is_standard_layout_v<cd_sample::W8BEGeomMeta>);
}
