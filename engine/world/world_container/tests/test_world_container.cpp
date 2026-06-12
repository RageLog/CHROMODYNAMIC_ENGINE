#include <cd/world_container/ProjectIo.hpp>
#include <cd/world_container/World.hpp>

#include <gtest/gtest.h>

#include <filesystem>

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


// ---------------------------------------------------------------------------
// phase1108 — ProjectIo (.cdproject persistence)
// ---------------------------------------------------------------------------

/// Build a fully-populated project exercising every serialized field.
[[nodiscard]] inline std::unique_ptr<cd::world_container::Project> make_rich_project()
{
    using namespace cd::world_container;
    auto p = std::make_unique<Project>("Rich Project");
    p->settings().enable_bloom      = true;
    p->settings().enable_rt_shadows = false;
    p->settings().tonemap_op        = 4;
    p->settings().master_volume     = 0.5F;
    p->settings().default_transport = "enet";

    Level* a = p->add_level("Hub");
    a->set_scene_path("scenes/hub.cdscene");
    a->bounds().min = { -10.0F, -1.0F, -10.0F };
    a->bounds().max = {  10.0F,  5.0F,  10.0F };
    a->layer(0)->set_name("Geometry");
    Layer* lights = a->add_layer("Lights");
    lights->set_visible(false);
    lights->set_locked(true);
    lights->set_persistent(true);
    lights->set_color_tag({ 1.0F, 0.8F, 0.2F });
    lights->set_draw_order(7);
    lights->postfx().override_bloom = true;
    lights->postfx().enable_bloom   = true;
    a->set_active_layer(1);

    Level* b = p->add_level("Arena");
    b->set_scene_path("scenes/arena.cdscene");
    return p;
}

TEST(ProjectIo, RoundTripPreservesEveryField)
{
    using namespace cd::world_container;
    const auto original = make_rich_project();
    const auto json = serialize_project(*original);
    const auto txt  = cd::asset::json::serialize(json, true);

    const auto parsed = cd::asset::json::parse(txt);
    ASSERT_TRUE(parsed.has_value());
    auto loaded_r = deserialize_project(*parsed);
    ASSERT_TRUE(loaded_r.has_value());
    const auto& p = **loaded_r;

    EXPECT_EQ(p.name(), "Rich Project");
    EXPECT_TRUE(p.settings().enable_bloom);
    EXPECT_FALSE(p.settings().enable_rt_shadows);
    EXPECT_EQ(p.settings().tonemap_op, 4);
    EXPECT_FLOAT_EQ(p.settings().master_volume, 0.5F);
    EXPECT_EQ(p.settings().default_transport, "enet");

    ASSERT_EQ(p.level_count(), 2u);
    const Level* a = p.level(0);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->name(), "Hub");
    EXPECT_EQ(a->scene_path(), "scenes/hub.cdscene");
    EXPECT_FLOAT_EQ(a->bounds().max.y, 5.0F);
    EXPECT_EQ(a->active_layer(), 1u);
    // Round-trip must not grow the layer list (Default reused).
    ASSERT_EQ(a->layer_count(), 2u);
    EXPECT_EQ(a->layer(0)->name(), "Geometry");
    const Layer* lights = a->layer(1);
    ASSERT_NE(lights, nullptr);
    EXPECT_EQ(lights->name(), "Lights");
    EXPECT_FALSE(lights->visible());
    EXPECT_TRUE(lights->locked());
    EXPECT_TRUE(lights->persistent());
    EXPECT_FLOAT_EQ(lights->color_tag().y, 0.8F);
    EXPECT_EQ(lights->draw_order(), 7);
    EXPECT_TRUE(lights->postfx().override_bloom);
    EXPECT_TRUE(lights->postfx().enable_bloom);

    EXPECT_EQ(p.level(1)->scene_path(), "scenes/arena.cdscene");
}

TEST(ProjectIo, RejectsUnknownSchemaVersion)
{
    const auto parsed = cd::asset::json::parse(R"({"schema_version": 999})");
    ASSERT_TRUE(parsed.has_value());
    const auto r = cd::world_container::deserialize_project(*parsed);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(
                  cd::world_container::project_io_errors::Code::kBadVersion));
}

TEST(ProjectIo, RejectsNonObjectRoot)
{
    const auto parsed = cd::asset::json::parse("[1, 2, 3]");
    ASSERT_TRUE(parsed.has_value());
    const auto r = cd::world_container::deserialize_project(*parsed);
    ASSERT_FALSE(r.has_value());
}

TEST(ProjectIo, AbsentOptionalKeysKeepDefaults)
{
    const auto parsed = cd::asset::json::parse(
        R"({"schema_version": 1, "name": "Bare"})");
    ASSERT_TRUE(parsed.has_value());
    auto r = cd::world_container::deserialize_project(*parsed);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ((*r)->name(), "Bare");
    EXPECT_EQ((*r)->level_count(), 0u);
    EXPECT_TRUE((*r)->settings().enable_csm);  // default preserved
}

TEST(ProjectIo, FileRoundTripIsAtomic)
{
    using namespace cd::world_container;
    const auto dir  = std::filesystem::temp_directory_path() / "cd_projectio_test";
    std::filesystem::create_directories(dir);
    const auto path = dir / "demo.cdproject";
    const auto tmp  = dir / "demo.cdproject.tmp";

    const auto original = make_rich_project();
    const auto saved = save_project_file(path, *original);
    ASSERT_TRUE(saved.has_value());
    EXPECT_TRUE(std::filesystem::exists(path));
    EXPECT_FALSE(std::filesystem::exists(tmp))
        << "atomic save must not leave the .tmp sibling behind";

    auto loaded = load_project_file(path);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ((*loaded)->name(), "Rich Project");
    EXPECT_EQ((*loaded)->level_count(), 2u);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST(ProjectIo, LoadMissingFileFails)
{
    const auto r = cd::world_container::load_project_file(
        std::filesystem::temp_directory_path() / "cd_projectio_missing.cdproject");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(
                  cd::world_container::project_io_errors::Code::kIoFailure));
}

}  // namespace
