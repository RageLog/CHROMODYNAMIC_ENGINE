#include <cd/decal/Decal.hpp>

#include <gtest/gtest.h>

namespace
{

using cd::decal::Decal;
using cd::decal::decal_intersects_aabb;
using cd::decal::project_world_to_decal;

constexpr float kEps = 1e-3F;

TEST(Decal, ProjectCentreSampleAtUvHalf)
{
    Decal d {};
    cd::math::Vec3f local;
    std::array<float, 2> uv;
    const bool inside = project_world_to_decal(d, { 0, 0, 0 }, local, uv);
    EXPECT_TRUE(inside);
    EXPECT_NEAR(uv[0], 0.5F, kEps);
    EXPECT_NEAR(uv[1], 0.5F, kEps);
}

TEST(Decal, ProjectOutsideRejected)
{
    Decal d {};
    cd::math::Vec3f local;
    std::array<float, 2> uv;
    EXPECT_FALSE(project_world_to_decal(d, { 5, 0, 0 }, local, uv));
}

TEST(Decal, ProjectAtEdgeProducesEdgeUv)
{
    Decal d {};  // half_extents = (0.5, 0.5, 0.5)
    cd::math::Vec3f local;
    std::array<float, 2> uv;
    EXPECT_TRUE(project_world_to_decal(d, { 0.5F, 0, 0 }, local, uv));
    EXPECT_NEAR(uv[0], 1.0F, kEps);
}

TEST(Decal, AabbIntersectionTrueForOverlap)
{
    Decal d {};
    EXPECT_TRUE(decal_intersects_aabb(d, { -1, -1, -1 }, { 1, 1, 1 }));
}

TEST(Decal, AabbIntersectionFalseForDisjoint)
{
    Decal d {};
    EXPECT_FALSE(decal_intersects_aabb(d, { 10, 10, 10 }, { 11, 11, 11 }));
}

TEST(Decal, GlslHelperNonEmpty)
{
    EXPECT_FALSE(cd::decal::kDecalGlsl.empty());
    EXPECT_NE(cd::decal::kDecalGlsl.find("decal_project"),
              std::string_view::npos);
}

}  // namespace
