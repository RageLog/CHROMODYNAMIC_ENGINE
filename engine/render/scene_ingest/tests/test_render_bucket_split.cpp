// =============================================================================
// CHROMODYNAMIC — cd::render::scene RenderBucket / partition_prims tests
//
// M11 W1 — T1.15. The four required cases from the phase brief:
//   1. Mixed opaque + alpha-mask + alpha-blend prims partition into the
//      expected three buckets.
//   2. Empty input yields three empty buckets and reports empty()==true.
//   3. All-opaque input fills exactly the opaque bucket; the two alpha
//      buckets stay empty.
//   4. `bucket_for(MaterialInstance{})` (default-constructed) routes to
//      `kOpaque`, matching the glTF 2.0 default alphaMode.
//
// MaterialInstance is constructed default — it has no descriptor set,
// no IDevice, no RHI handles. The alpha-mode setters operate purely on
// CPU state, so we exercise the routing rule without touching Vulkan.
// =============================================================================
#include <cd/material/AlphaMode.hpp>
#include <cd/material/Material.hpp>
#include <cd/render/scene/RenderBucket.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace
{

using cd::material::AlphaMode;
using cd::material::MaterialInstance;
using cd::render::scene::bucket_for;
using cd::render::scene::BucketedPrims;
using cd::render::scene::partition_prims;
using cd::render::scene::PrimHandle;
using cd::render::scene::RenderBucket;

}  // namespace

// ---- Case 4 (predicate / default) — also acts as a sanity check on the
// underlying material accessors before we drive the partition tests.
TEST(RenderBucketTest, DefaultMaterialInstanceRoutesToOpaque)
{
    // Arrange
    const MaterialInstance mat {};

    // Act
    const RenderBucket b = bucket_for(mat);

    // Assert
    EXPECT_EQ(b, RenderBucket::kOpaque);
    EXPECT_TRUE(mat.is_opaque());
    EXPECT_FALSE(mat.is_mask());
    EXPECT_FALSE(mat.is_blend());

    // Scalar overload matches.
    EXPECT_EQ(bucket_for(AlphaMode::kOpaque), RenderBucket::kOpaque);
    EXPECT_EQ(bucket_for(AlphaMode::kMask),   RenderBucket::kAlphaMask);
    EXPECT_EQ(bucket_for(AlphaMode::kBlend),  RenderBucket::kAlphaBlend);
}

// ---- Case 2 (empty scene) ---------------------------------------------------
TEST(RenderBucketTest, EmptySceneYieldsThreeEmptyBuckets)
{
    // Arrange
    const std::vector<PrimHandle>                       prims;
    const std::vector<const MaterialInstance*>          materials;

    // Act
    const BucketedPrims result = partition_prims(
        std::span<const PrimHandle>(prims),
        std::span<const MaterialInstance* const>(materials));

    // Assert
    EXPECT_TRUE(result.empty());
    EXPECT_EQ(result.size(), 0U);
    EXPECT_EQ(result.opaque_prims().size(),      0U);
    EXPECT_EQ(result.alpha_mask_prims().size(),  0U);
    EXPECT_EQ(result.alpha_blend_prims().size(), 0U);
}

// ---- Case 3 (all-opaque) ----------------------------------------------------
TEST(RenderBucketTest, AllOpaqueSceneFillsOpaqueBucketOnly)
{
    // Arrange — 4 prims with default (opaque) materials.
    std::array<MaterialInstance, 4>     mats;
    std::vector<const MaterialInstance*> mat_ptrs;
    std::vector<PrimHandle>             prims;
    mat_ptrs.reserve(mats.size());
    prims.reserve(mats.size());
    for (std::size_t i = 0; i < mats.size(); ++i)
    {
        mat_ptrs.push_back(&mats[i]);
        prims.push_back(static_cast<PrimHandle>(100U + i));
    }

    // Act
    const BucketedPrims result = partition_prims(
        std::span<const PrimHandle>(prims),
        std::span<const MaterialInstance* const>(mat_ptrs));

    // Assert
    ASSERT_EQ(result.opaque_prims().size(), 4U);
    EXPECT_EQ(result.alpha_mask_prims().size(),  0U);
    EXPECT_EQ(result.alpha_blend_prims().size(), 0U);
    EXPECT_EQ(result.size(), 4U);
    EXPECT_FALSE(result.empty());

    // Input order preserved within the bucket.
    EXPECT_EQ(result.opaque_prims()[0], 100U);
    EXPECT_EQ(result.opaque_prims()[1], 101U);
    EXPECT_EQ(result.opaque_prims()[2], 102U);
    EXPECT_EQ(result.opaque_prims()[3], 103U);
}

// ---- Case 1 (mixed scene — the headline T1.15 test) ------------------------
TEST(RenderBucketTest, MixedSceneRoutesOpaqueMaskBlendIntoSeparateBuckets)
{
    // Arrange — 6 prims arranged as O M B O B M (insertion order shuffled to
    // verify the partition truly classifies per-material rather than
    // chunking by position).
    MaterialInstance m_opaque0 {};
    MaterialInstance m_opaque1 {};
    MaterialInstance m_mask0 {};
    MaterialInstance m_mask1 {};
    MaterialInstance m_blend0 {};
    MaterialInstance m_blend1 {};

    m_mask0.set_alpha_mode(AlphaMode::kMask);
    m_mask0.set_alpha_cutoff(0.5F);
    m_mask1.set_alpha_params({ AlphaMode::kMask, 0.25F });

    m_blend0.set_alpha_mode(AlphaMode::kBlend);
    m_blend1.set_alpha_params({ AlphaMode::kBlend, 0.0F });

    const std::array<const MaterialInstance*, 6> mat_ptrs {
        &m_opaque0, &m_mask0, &m_blend0, &m_opaque1, &m_blend1, &m_mask1
    };
    const std::array<PrimHandle, 6> prims { 10U, 20U, 30U, 40U, 50U, 60U };

    // Act
    const BucketedPrims result = partition_prims(
        std::span<const PrimHandle>(prims),
        std::span<const MaterialInstance* const>(mat_ptrs));

    // Assert — sizes
    ASSERT_EQ(result.opaque_prims().size(),      2U);
    ASSERT_EQ(result.alpha_mask_prims().size(),  2U);
    ASSERT_EQ(result.alpha_blend_prims().size(), 2U);
    EXPECT_EQ(result.size(), 6U);

    // Assert — opaque prims preserve relative input order
    EXPECT_EQ(result.opaque_prims()[0], 10U);
    EXPECT_EQ(result.opaque_prims()[1], 40U);

    // Assert — mask prims preserve relative input order
    EXPECT_EQ(result.alpha_mask_prims()[0], 20U);
    EXPECT_EQ(result.alpha_mask_prims()[1], 60U);

    // Assert — blend prims preserve relative input order (this is the
    // curtain bleed-through fix: blend prims are the last bucket, and the
    // framegraph applies a back-to-front sort on top of this stable order).
    EXPECT_EQ(result.alpha_blend_prims()[0], 30U);
    EXPECT_EQ(result.alpha_blend_prims()[1], 50U);

    // Spot-check the predicate routing on the source materials so a future
    // regression in MaterialInstance state machinery is caught here too.
    EXPECT_EQ(bucket_for(m_opaque0), RenderBucket::kOpaque);
    EXPECT_EQ(bucket_for(m_mask0),   RenderBucket::kAlphaMask);
    EXPECT_EQ(bucket_for(m_blend0),  RenderBucket::kAlphaBlend);
}
