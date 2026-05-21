// =============================================================================
// CHROMODYNAMIC — cd::scene tests
// =============================================================================
#include <cd/ecs/World.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Transform.hpp>
#include <cd/scene/Scene.hpp>
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

namespace
{

constexpr float kEps = 1e-5F;

[[nodiscard]] bool approx_eq(float a, float b) noexcept
{
    return std::fabs(a - b) < kEps;
}

TEST(Scene, CreateNodeInstallsIdentityTransforms)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto e = s.create_node();
    EXPECT_TRUE(w.is_alive(e));
    ASSERT_NE(s.local(e), nullptr);
    ASSERT_NE(s.world_transform(e), nullptr);
    const auto id = cd::math::Mat4f::identity();
    for (std::size_t c = 0; c < 4; ++c)
    {
        for (std::size_t r = 0; r < 4; ++r)
        {
            EXPECT_TRUE(approx_eq(s.world_transform(e)->matrix[c][r], id[c][r]));
        }
    }
}

TEST(Scene, AttachAndDetachMaintainChildrenList)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto parent = s.create_node();
    auto child = s.create_node();
    ASSERT_TRUE(s.attach(child, parent));
    // Parent component on child.
    EXPECT_EQ(s.parent_of(child), parent);
    // Children list on parent.
    ASSERT_NE(w.get<cd::scene::Children>(parent), nullptr);
    EXPECT_EQ(w.get<cd::scene::Children>(parent)->entities.size(), 1U);

    s.detach(child);
    EXPECT_EQ(s.parent_of(child), cd::ecs::Entity {});
    // Empty Children list is cleaned up.
    EXPECT_EQ(w.get<cd::scene::Children>(parent), nullptr);
}

TEST(Scene, AttachToSelfRejected)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto e = s.create_node();
    EXPECT_FALSE(s.attach(e, e));
}

TEST(Scene, ReparentReplacesOldLink)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto a = s.create_node();
    auto b = s.create_node();
    auto c = s.create_node();
    ASSERT_TRUE(s.attach(c, a));
    ASSERT_TRUE(s.attach(c, b));
    EXPECT_EQ(s.parent_of(c), b);
    EXPECT_EQ(w.get<cd::scene::Children>(a), nullptr);
    EXPECT_EQ(w.get<cd::scene::Children>(b)->entities.size(), 1U);
}

TEST(Scene, UpdateTransformsComposesParentChild)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto root = s.create_node();
    auto child = s.create_node();
    ASSERT_TRUE(s.attach(child, root));

    // Root translated to +(2, 0, 0). Child offset by +(1, 0, 0). Child's
    // world position must therefore be +(3, 0, 0).
    s.local(root)->value.position = { 2.0F, 0.0F, 0.0F };
    s.local(child)->value.position = { 1.0F, 0.0F, 0.0F };

    s.update_transforms();
    // Column-major: translation lives in column 3 (m[3]).
    const auto& wm = s.world_transform(child)->matrix;
    EXPECT_TRUE(approx_eq(wm[3][0], 3.0F));
    EXPECT_TRUE(approx_eq(wm[3][1], 0.0F));
    EXPECT_TRUE(approx_eq(wm[3][2], 0.0F));
}

TEST(Scene, UpdateTransformsThreeLevels)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto a = s.create_node();
    auto b = s.create_node();
    auto c = s.create_node();
    ASSERT_TRUE(s.attach(b, a));
    ASSERT_TRUE(s.attach(c, b));
    s.local(a)->value.position = { 1.0F, 0.0F, 0.0F };
    s.local(b)->value.position = { 0.0F, 1.0F, 0.0F };
    s.local(c)->value.position = { 0.0F, 0.0F, 1.0F };
    s.update_transforms();
    const auto& wm = s.world_transform(c)->matrix;
    EXPECT_TRUE(approx_eq(wm[3][0], 1.0F));
    EXPECT_TRUE(approx_eq(wm[3][1], 1.0F));
    EXPECT_TRUE(approx_eq(wm[3][2], 1.0F));
}

TEST(Scene, DestroySubtreeRemovesAllDescendants)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto root = s.create_node();
    auto mid = s.create_node();
    auto leaf = s.create_node();
    ASSERT_TRUE(s.attach(mid, root));
    ASSERT_TRUE(s.attach(leaf, mid));
    EXPECT_EQ(w.alive_count(), 3U);
    s.destroy_node(root);
    EXPECT_EQ(w.alive_count(), 0U);
}

TEST(Scene, DestroyMiddleNodeRemovesDescendantsOnly)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto root = s.create_node();
    auto mid = s.create_node();
    auto leaf = s.create_node();
    auto sibling = s.create_node();
    ASSERT_TRUE(s.attach(mid, root));
    ASSERT_TRUE(s.attach(leaf, mid));
    ASSERT_TRUE(s.attach(sibling, root));
    EXPECT_EQ(w.alive_count(), 4U);
    s.destroy_node(mid);  // takes leaf with it
    EXPECT_EQ(w.alive_count(), 2U);
    EXPECT_TRUE(w.is_alive(root));
    EXPECT_TRUE(w.is_alive(sibling));
    EXPECT_FALSE(w.is_alive(mid));
    EXPECT_FALSE(w.is_alive(leaf));
    // root's Children should no longer reference mid.
    ASSERT_NE(w.get<cd::scene::Children>(root), nullptr);
    EXPECT_EQ(w.get<cd::scene::Children>(root)->entities.size(), 1U);
    EXPECT_EQ(w.get<cd::scene::Children>(root)->entities[0], sibling);
}

TEST(Scene, AttachDeadEntityRejected)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto parent = s.create_node();
    cd::ecs::Entity ghost {};
    EXPECT_FALSE(s.attach(ghost, parent));
    EXPECT_FALSE(s.attach(parent, ghost));
}

}  // namespace
