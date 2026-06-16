#include <cd/world_container/LayerMember.hpp>
#include <cd/world_container/LevelStreamer.hpp>
#include <cd/world_container/ProjectIo.hpp>
#include <cd/world_container/World.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

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


// ---------------------------------------------------------------------------
// phase1110 — LayerMember (ECS layer membership)
// ---------------------------------------------------------------------------

TEST(LayerMember, ImplicitDefaultAndAssignment)
{
    using namespace cd::world_container;
    cd::ecs::World w;
    const auto e = w.create();

    EXPECT_EQ(layer_of(w, e), kDefaultLayerName);  // no component yet

    assign_layer(w, e, "Lights");
    EXPECT_EQ(layer_of(w, e), "Lights");

    assign_layer(w, e, "UI");                       // reassign replaces
    EXPECT_EQ(layer_of(w, e), "UI");

    clear_layer(w, e);
    EXPECT_EQ(layer_of(w, e), kDefaultLayerName);   // falls back
}

TEST(LayerMember, CountAndVisitFilterByName)
{
    using namespace cd::world_container;
    cd::ecs::World w;
    const auto a = w.create();
    const auto b = w.create();
    const auto c = w.create();
    assign_layer(w, a, "Lights");
    assign_layer(w, b, "Lights");
    assign_layer(w, c, "Props");

    EXPECT_EQ(count_members(w, "Lights"), 2u);
    EXPECT_EQ(count_members(w, "Props"), 1u);
    EXPECT_EQ(count_members(w, "Absent"), 0u);

    std::size_t visited = 0;
    bool saw_c = false;
    for_each_member(w, "Lights",
                    [&](cd::ecs::Entity e)
                    {
                        ++visited;
                        if (e.id == c.id) saw_c = true;
                    });
    EXPECT_EQ(visited, 2u);
    EXPECT_FALSE(saw_c);
}

TEST(LayerMember, RenameMigratesMembers)
{
    using namespace cd::world_container;
    cd::ecs::World w;
    const auto a = w.create();
    const auto b = w.create();
    const auto c = w.create();
    assign_layer(w, a, "Old");
    assign_layer(w, b, "Old");
    assign_layer(w, c, "Other");

    EXPECT_EQ(rename_layer_members(w, "Old", "New"), 2u);
    EXPECT_EQ(layer_of(w, a), "New");
    EXPECT_EQ(layer_of(w, b), "New");
    EXPECT_EQ(layer_of(w, c), "Other");
    EXPECT_EQ(count_members(w, "Old"), 0u);
}


// ---------------------------------------------------------------------------
// phase1114 — LevelStreamer (v1.7 streaming slice 1: level switching
// with persistent-layer survival)
// ---------------------------------------------------------------------------

namespace
{
/// Write a .cdscene with `n` entities; entity i is on `layer_for(i)`.
template <class LayerFor>
void write_scene_file(const std::filesystem::path& path,
                      cd::ecs::World& scratch_world, int n, LayerFor&& layer_for)
{
    cd::scene::Scene scratch { scratch_world };
    std::vector<cd::ecs::Entity> ents;
    for (int i = 0; i < n; ++i)
    {
        const auto e = scratch.create_node();
        scratch.local(e)->value.position = { static_cast<float>(i), 0.0F, 0.0F };
        ents.push_back(e);
    }
    auto root = cd::scene::serialize_scene_with(
        scratch,
        [&](cd::ecs::Entity e, cd::asset::json::Object& obj)
        {
            for (std::size_t i = 0; i < ents.size(); ++i)
            {
                if (ents[i].id != e.id) continue;
                obj["layer"] = cd::asset::json::Value {
                    std::string { layer_for(static_cast<int>(i)) } };
            }
        });
    const auto txt = cd::asset::json::serialize(root, true);
    std::ofstream f { path, std::ios::binary | std::ios::trunc };
    f.write(txt.data(), static_cast<std::streamsize>(txt.size()));
}
}  // namespace

TEST(LevelStreamer, SwitchDestroysNonPersistentAndKeepsPersistent)
{
    using namespace cd::world_container;
    const auto dir = std::filesystem::temp_directory_path() / "cd_streamer_test";
    std::filesystem::create_directories(dir);

    // Scene A: entity0 on persistent "Keep", entity1 on Default.
    {
        cd::ecs::World scratch;
        write_scene_file(dir / "a.cdscene", scratch, 2,
                         [](int i) { return i == 0 ? "Keep" : "Default"; });
    }
    // Scene B: one entity on Default.
    {
        cd::ecs::World scratch;
        write_scene_file(dir / "b.cdscene", scratch, 1,
                         [](int) { return "Default"; });
    }

    Project proj { "Stream Test" };
    Level* a = proj.add_level("A");
    a->set_scene_path("a.cdscene");
    Layer* keep = a->add_layer("Keep");
    keep->set_persistent(true);
    Level* b = proj.add_level("B");
    b->set_scene_path("b.cdscene");

    cd::ecs::World world;
    cd::scene::Scene scene { world };
    LevelStreamer streamer { world, scene };

    std::vector<cd::ecs::Entity> loaded_a;
    auto r = streamer.activate(proj, 0, dir,
                               [&](cd::ecs::Entity e, const cd::asset::json::Object&)
                               { loaded_a.push_back(e); });
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(loaded_a.size(), 2u);
    EXPECT_EQ(streamer.tracked_count(), 2u);
    EXPECT_EQ(layer_of(world, loaded_a[0]), "Keep");
    EXPECT_EQ(layer_of(world, loaded_a[1]), kDefaultLayerName);

    // Switch to B: the "Keep" entity must SURVIVE, the Default one dies.
    std::vector<cd::ecs::Entity> loaded_b;
    r = streamer.activate(proj, 1, dir,
                          [&](cd::ecs::Entity e, const cd::asset::json::Object&)
                          { loaded_b.push_back(e); });
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(loaded_b.size(), 1u);
    EXPECT_NE(scene.local(loaded_a[0]), nullptr) << "persistent entity destroyed";
    EXPECT_EQ(scene.local(loaded_a[1]), nullptr) << "non-persistent entity leaked";
    EXPECT_EQ(streamer.tracked_count(), 2u);  // survivor + B's entity
    EXPECT_EQ(streamer.active_index(), 1u);

    // Deactivate: B has no persistent layers — everything tracked dies
    // EXCEPT entities whose layer B marks persistent (none), but the
    // survivor from A keeps its "Keep" membership — B doesn't know that
    // layer, so it dies too. Contract: persistence is per OUTGOING level.
    streamer.deactivate(proj);
    EXPECT_EQ(scene.local(loaded_a[0]), nullptr);
    EXPECT_EQ(scene.local(loaded_b[0]), nullptr);
    EXPECT_FALSE(streamer.has_active());

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST(LevelStreamer, BadLevelAndMissingFileFail)
{
    using namespace cd::world_container;
    Project proj { "Errs" };
    proj.add_level("NoScene");  // empty scene_path
    Level* missing = proj.add_level("Missing");
    missing->set_scene_path("does_not_exist.cdscene");

    cd::ecs::World world;
    cd::scene::Scene scene { world };
    LevelStreamer streamer { world, scene };
    const auto dir = std::filesystem::temp_directory_path();

    auto r1 = streamer.activate(proj, 0, dir,
                                [](cd::ecs::Entity, const cd::asset::json::Object&) {});
    ASSERT_FALSE(r1.has_value());
    auto r2 = streamer.activate(proj, 99, dir,
                                [](cd::ecs::Entity, const cd::asset::json::Object&) {});
    ASSERT_FALSE(r2.has_value());
    auto r3 = streamer.activate(proj, 1, dir,
                                [](cd::ecs::Entity, const cd::asset::json::Object&) {});
    ASSERT_FALSE(r3.has_value());
    EXPECT_FALSE(streamer.has_active());
    EXPECT_EQ(streamer.tracked_count(), 0u);
}

// BAND-2 world topup: the SwitchDestroys... test only covers deactivate()
// when the active level marks NOTHING persistent (everything dies). The
// survivor branch of deactivate() — an entity on a layer the OUTGOING
// (active) level marks persistent must SURVIVE and stay tracked — and the
// no-active early-return no-op were unasserted contract branches.
TEST(LevelStreamer, DeactivateKeepsPersistentOfActiveLevelAndNoOpWhenInactive)
{
    using namespace cd::world_container;
    const auto dir = std::filesystem::temp_directory_path() / "cd_streamer_deact_test";
    std::filesystem::create_directories(dir);

    // Scene A: entity0 on persistent "Keep", entity1 on Default.
    {
        cd::ecs::World scratch;
        write_scene_file(dir / "a.cdscene", scratch, 2,
                         [](int i) { return i == 0 ? "Keep" : "Default"; });
    }

    Project proj { "Deact Test" };
    Level* a = proj.add_level("A");
    a->set_scene_path("a.cdscene");
    Layer* keep = a->add_layer("Keep");
    keep->set_persistent(true);

    cd::ecs::World world;
    cd::scene::Scene scene { world };
    LevelStreamer streamer { world, scene };

    // Inactive no-op branch: deactivate before any activate must not crash
    // and must leave the streamer inactive with nothing tracked.
    streamer.deactivate(proj);
    EXPECT_FALSE(streamer.has_active());
    EXPECT_EQ(streamer.tracked_count(), 0u);

    std::vector<cd::ecs::Entity> loaded;
    auto r = streamer.activate(proj, 0, dir,
                               [&](cd::ecs::Entity e, const cd::asset::json::Object&)
                               { loaded.push_back(e); });
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(loaded.size(), 2u);

    // Deactivate WHILE A is active: A marks "Keep" persistent → entity0
    // survives and stays tracked; entity1 (Default) dies. active is forgotten.
    streamer.deactivate(proj);
    EXPECT_NE(scene.local(loaded[0]), nullptr) << "persistent survivor destroyed";
    EXPECT_EQ(scene.local(loaded[1]), nullptr) << "non-persistent entity leaked";
    EXPECT_EQ(streamer.tracked_count(), 1u) << "survivor must remain tracked";
    EXPECT_FALSE(streamer.has_active());

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

}  // namespace
