// =============================================================================
// CHROMODYNAMIC -- samples/engine/hello_engine/tests/test_tlas_compaction.cpp
//
// Regression test for the ghost-shadow / TLAS-exclusion invariant
// (W8-AS) and the GPU instanceCustomIndex / inst_mat SSBO slot
// alignment (W8-BC contract). Lives next to the hello_engine sample
// because the helper-under-test
// (cd_sample::compact_tlas_entity_instances) is sample-local; the
// only library dependency is cd::concurrency::parallel_for -- no
// RHI device / window / Vulkan needed to drive it.
//
// Marathon Run 23 phase N2B.
//
// Scenarios:
//   * GhostShadow_*       -- model_for() returning nullopt produces
//                            ZERO TLAS instances.
//   * InvalidBlas_*       -- blas_for_kind() returning an invalid
//                            handle is also excluded.
//   * Ordering_*          -- output ordering matches input entity
//                            order for included entities.
//   * SlotAlignment_*     -- instances[i] and inst_mats[i] correspond
//                            (W8-BC tint round-trip).
//   * EmptyInput_*        -- zero entities yields zero instances.
//   * AllHidden_*         -- every entity model_for() == nullopt
//                            yields zero TLAS instances.
//   * ParallelScatter_*   -- repeated runs are deterministic.
// =============================================================================
#include "../HelloTlasCompaction.hpp"
#include "../HelloRayQuery.hpp"

#include <cd/math/Matrix.hpp>
#include <cd/math/Vector.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Handles.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace
{

struct FakeEntity
{
    int                            kind { 0 };
    cd::math::Vec3f                tint { 1.0F, 1.0F, 1.0F };
    std::optional<cd::math::Mat4f> model {};
};

[[nodiscard]] cd::rhi::AccelStructureHandle make_valid_handle(std::uint32_t idx)
{
    return cd::rhi::AccelStructureHandle { idx, 1, 0 };
}

struct BlasForKindFake
{
    cd::rhi::AccelStructureHandle operator()(int kind) const
    {
        switch (kind)
        {
            case 0: return make_valid_handle(1);
            case 1: return make_valid_handle(2);
            case 2: return make_valid_handle(3);
            default: return {};
        }
    }
};

struct TintForFake
{
    cd::math::Vec3f operator()(const FakeEntity& e) const { return e.tint; }
};

struct KindForFake
{
    int operator()(const FakeEntity& e) const { return e.kind; }
};

struct ModelForFake
{
    std::optional<cd::math::Mat4f> operator()(const FakeEntity& e) const { return e.model; }
};

[[nodiscard]] cd::math::Mat4f model_at(float x, float y, float z)
{
    cd::math::Mat4f m = cd::math::Mat4f::identity();
    m[3][0] = x;
    m[3][1] = y;
    m[3][2] = z;
    return m;
}

}  // namespace

TEST(TlasCompaction, GhostShadow_ExcludesEntitiesWithoutModel)
{
    std::vector<FakeEntity> entities {
        FakeEntity { .kind = 0, .tint = { 1.0F, 0.0F, 0.0F }, .model = model_at(1, 0, 0) },
        FakeEntity { .kind = 1, .tint = { 0.0F, 1.0F, 0.0F }, .model = std::nullopt },
        FakeEntity { .kind = 2, .tint = { 0.0F, 0.0F, 1.0F }, .model = model_at(3, 0, 0) },
    };

    auto result = cd_sample::compact_tlas_entity_instances<FakeEntity>(
        std::span<const FakeEntity>(entities),
        BlasForKindFake {},
        TintForFake {},
        KindForFake {},
        ModelForFake {});

    EXPECT_EQ(result.instances.size(), 2U);
    EXPECT_EQ(result.inst_mats.size(), 2U);

    EXPECT_FLOAT_EQ(result.inst_mats[0].albedo[0], 1.0F);
    EXPECT_FLOAT_EQ(result.inst_mats[0].albedo[1], 0.0F);
    EXPECT_EQ(result.instances[0].blas.value(), make_valid_handle(1).value());

    EXPECT_FLOAT_EQ(result.inst_mats[1].albedo[2], 1.0F);
    EXPECT_FLOAT_EQ(result.inst_mats[1].albedo[0], 0.0F);
    EXPECT_EQ(result.instances[1].blas.value(), make_valid_handle(3).value());
}

TEST(TlasCompaction, InvalidBlas_ExcludesUnregisteredKind)
{
    std::vector<FakeEntity> entities {
        FakeEntity { .kind = 0,  .tint = { 1, 0, 0 }, .model = model_at(0, 0, 0) },
        FakeEntity { .kind = 99, .tint = { 1, 1, 0 }, .model = model_at(1, 0, 0) },
        FakeEntity { .kind = 2,  .tint = { 0, 0, 1 }, .model = model_at(2, 0, 0) },
    };

    auto result = cd_sample::compact_tlas_entity_instances<FakeEntity>(
        std::span<const FakeEntity>(entities),
        BlasForKindFake {},
        TintForFake {},
        KindForFake {},
        ModelForFake {});

    EXPECT_EQ(result.instances.size(), 2U);
    EXPECT_EQ(result.inst_mats.size(), 2U);
    EXPECT_EQ(result.instances[0].blas.value(), make_valid_handle(1).value());
    EXPECT_EQ(result.instances[1].blas.value(), make_valid_handle(3).value());
}

TEST(TlasCompaction, Ordering_PreservedAcrossEntityOrder)
{
    std::vector<FakeEntity> entities;
    constexpr int kN = 64;
    entities.reserve(static_cast<std::size_t>(kN));
    for (int i = 0; i < kN; ++i)
    {
        FakeEntity e;
        e.kind = i % 3;
        e.tint = cd::math::Vec3f { static_cast<float>(i) / static_cast<float>(kN), 0.5F, 1.0F - (static_cast<float>(i) / static_cast<float>(kN)) };
        e.model = model_at(static_cast<float>(i), 0.0F, 0.0F);
        entities.push_back(e);
    }

    auto result = cd_sample::compact_tlas_entity_instances<FakeEntity>(
        std::span<const FakeEntity>(entities),
        BlasForKindFake {},
        TintForFake {},
        KindForFake {},
        ModelForFake {});

    ASSERT_EQ(result.instances.size(), static_cast<std::size_t>(kN));
    for (int i = 0; i < kN; ++i)
    {
        const auto& inst = result.instances[static_cast<std::size_t>(i)];
        EXPECT_FLOAT_EQ(inst.transform[3], static_cast<float>(i))
            << "Ordering mismatch at slot " << i;
        const auto& im = result.inst_mats[static_cast<std::size_t>(i)];
        EXPECT_FLOAT_EQ(im.albedo[0], static_cast<float>(i) / static_cast<float>(kN));
    }
}

TEST(TlasCompaction, SlotAlignment_InstancesAndMatsCohabitate)
{
    std::vector<FakeEntity> entities {
        FakeEntity { .kind = 0, .tint = { 0.9F, 0.1F, 0.1F }, .model = model_at(0, 0, 0) },
        FakeEntity { .kind = 1, .tint = { 0.1F, 0.9F, 0.1F }, .model = std::nullopt },
        FakeEntity { .kind = 99, .tint = { 0.1F, 0.1F, 0.9F }, .model = model_at(1, 0, 0) },
        FakeEntity { .kind = 2, .tint = { 0.5F, 0.5F, 0.1F }, .model = model_at(2, 0, 0) },
    };

    auto result = cd_sample::compact_tlas_entity_instances<FakeEntity>(
        std::span<const FakeEntity>(entities),
        BlasForKindFake {},
        TintForFake {},
        KindForFake {},
        ModelForFake {});

    ASSERT_EQ(result.instances.size(), result.inst_mats.size());
    ASSERT_EQ(result.instances.size(), 2U);

    EXPECT_FLOAT_EQ(result.inst_mats[0].albedo[0], 0.9F);
    EXPECT_EQ(result.instances[0].blas.value(), make_valid_handle(1).value());

    EXPECT_FLOAT_EQ(result.inst_mats[1].albedo[1], 0.5F);
    EXPECT_EQ(result.instances[1].blas.value(), make_valid_handle(3).value());
}

TEST(TlasCompaction, EmptyInput_YieldsEmptyResult)
{
    std::vector<FakeEntity> entities {};
    auto result = cd_sample::compact_tlas_entity_instances<FakeEntity>(
        std::span<const FakeEntity>(entities),
        BlasForKindFake {},
        TintForFake {},
        KindForFake {},
        ModelForFake {});
    EXPECT_EQ(result.instances.size(), 0U);
    EXPECT_EQ(result.inst_mats.size(), 0U);
}

TEST(TlasCompaction, AllHidden_YieldsEmptyResult)
{
    std::vector<FakeEntity> entities;
    for (int i = 0; i < 16; ++i)
    {
        entities.push_back(
            FakeEntity { .kind = i % 3, .tint = { 1, 1, 1 }, .model = std::nullopt });
    }
    auto result = cd_sample::compact_tlas_entity_instances<FakeEntity>(
        std::span<const FakeEntity>(entities),
        BlasForKindFake {},
        TintForFake {},
        KindForFake {},
        ModelForFake {});
    EXPECT_EQ(result.instances.size(), 0U);
    EXPECT_EQ(result.inst_mats.size(), 0U);
}

TEST(TlasCompaction, ParallelScatter_IsDeterministic)
{
    std::vector<FakeEntity> entities;
    for (int i = 0; i < 32; ++i)
    {
        FakeEntity e;
        e.kind = i % 3;
        e.tint = cd::math::Vec3f { static_cast<float>(i), 0, 0 };
        e.model = model_at(static_cast<float>(i), 0, 0);
        if (i % 5 == 0)
            e.model = std::nullopt;
        entities.push_back(e);
    }

    auto first = cd_sample::compact_tlas_entity_instances<FakeEntity>(
        std::span<const FakeEntity>(entities),
        BlasForKindFake {},
        TintForFake {},
        KindForFake {},
        ModelForFake {});

    for (int repeat = 0; repeat < 5; ++repeat)
    {
        auto again = cd_sample::compact_tlas_entity_instances<FakeEntity>(
            std::span<const FakeEntity>(entities),
            BlasForKindFake {},
            TintForFake {},
            KindForFake {},
            ModelForFake {});
        ASSERT_EQ(again.instances.size(), first.instances.size())
            << "Non-deterministic compaction size at iter " << repeat;
        for (std::size_t i = 0; i < first.instances.size(); ++i)
        {
            EXPECT_FLOAT_EQ(again.inst_mats[i].albedo[0], first.inst_mats[i].albedo[0])
                << "Slot " << i << " differs at iter " << repeat;
            EXPECT_EQ(again.instances[i].blas.value(), first.instances[i].blas.value())
                << "Slot " << i << " BLAS differs at iter " << repeat;
        }
    }
}
