#include <cd/world_container/World.hpp>

#include <gtest/gtest.h>

namespace
{

using cd::world_container::Layer;
using cd::world_container::Level;
using cd::world_container::Project;
using cd::world_container::World;

TEST(WorldContainer, WorldHasNoProjectByDefault)
{
    World w;
    EXPECT_EQ(w.project(), nullptr);
}

TEST(WorldContainer, SetProjectStoresAndReturns)
{
    World w;
    w.set_project(std::make_unique<Project>("Test"));
    ASSERT_NE(w.project(), nullptr);
    EXPECT_EQ(w.project()->name(), "Test");
}

TEST(WorldContainer, ProjectLevelLifecycle)
{
    Project p { "P" };
    EXPECT_EQ(p.level_count(), 0U);
    auto* l = p.add_level("Main");
    ASSERT_NE(l, nullptr);
    EXPECT_EQ(l->name(), "Main");
    EXPECT_EQ(p.level_count(), 1U);
    EXPECT_TRUE(p.remove_level(0));
    EXPECT_EQ(p.level_count(), 0U);
}

TEST(WorldContainer, LevelHasDefaultLayer)
{
    Level l { "L" };
    EXPECT_EQ(l.layer_count(), 1U);
    ASSERT_NE(l.find_layer("Default"), nullptr);
}

TEST(WorldContainer, LevelAddRemoveLayer)
{
    Level l { "L" };
    l.add_layer("UI");
    l.add_layer("VFX");
    EXPECT_EQ(l.layer_count(), 3U);
    EXPECT_TRUE(l.remove_layer(2));
    EXPECT_EQ(l.layer_count(), 2U);
    // Can't remove the last layer.
    EXPECT_TRUE(l.remove_layer(1));
    EXPECT_FALSE(l.remove_layer(0));
    EXPECT_EQ(l.layer_count(), 1U);
}

TEST(WorldContainer, LayerVisibilityLockDefaults)
{
    Layer ly;
    EXPECT_TRUE(ly.visible());
    EXPECT_FALSE(ly.locked());
    EXPECT_FALSE(ly.persistent());
    ly.set_visible(false);
    ly.set_locked(true);
    EXPECT_FALSE(ly.visible());
    EXPECT_TRUE(ly.locked());
}

TEST(WorldContainer, ProjectFindLevelByName)
{
    Project p;
    p.add_level("A");
    p.add_level("B");
    EXPECT_NE(p.find_level("A"), nullptr);
    EXPECT_NE(p.find_level("B"), nullptr);
    EXPECT_EQ(p.find_level("C"), nullptr);
}

TEST(WorldContainer, ActiveLayerSelection)
{
    Level l;
    l.add_layer("UI");
    l.set_active_layer(1);
    EXPECT_EQ(l.active_layer(), 1U);
    l.set_active_layer(99);  // out of range — ignored
    EXPECT_EQ(l.active_layer(), 1U);
}

}  // namespace
