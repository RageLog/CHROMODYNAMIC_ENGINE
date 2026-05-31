// =============================================================================
// CHROMODYNAMIC — samples/script/hello_hot_reload/main.cpp
// Phase 534 — Lua entity behavior with live hot-reload.
//
// Demonstrates cd::script::Engine + cd::game::asset_hot_reload::HotReloadBus
// working together: a Lua script defines on_tick(entity, dt) which is called
// each frame from C++. While the sample is running the user may edit
// scripts/entity_behavior.lua on disk; the HotReloadBus detects the mtime
// advance (within its 100 ms throttle window), re-executes the Lua chunk, and
// the new on_tick definition takes effect immediately on the next frame.
//
// Architecture:
//   * One cd::ecs::World with a single entity that carries a
//     cd::scene::LocalTransform.
//   * cd::script::Bindings registers the cd.* table (Vec3f, World, etc.) so
//     the Lua side can call world:set_local_transform(entity, tbl) directly.
//   * HotReloadBus watches scripts/entity_behavior.lua. On reload the C++
//     side calls eng.run_file() to re-execute the chunk; because Lua globals
//     are mutable, the new on_tick definition replaces the old one atomically.
//   * The tick loop runs for kMaxFrames iterations (headless / CI gate) and
//     logs the entity position + reload events to stdout.
//
// Console output per tick:
//   [tick  42]  entity pos = ( 0.42,  0.00,  0.00)
// On reload:
//   [reload]  scripts/entity_behavior.lua re-applied (reload #1)
//
// Build gate: "sample compiles + exits 0" (CLAUDE.md §3). No ctest binary.
// =============================================================================

#include <cd/ecs/Entity.hpp>
#include <cd/ecs/World.hpp>
#include <cd/game/asset_hot_reload/HotReload.hpp>
#include <cd/math/Transform.hpp>
#include <cd/math/Vector.hpp>
#include <cd/scene/Scene.hpp>
#include <cd/script/Bindings.hpp>
#include <cd/script/Engine.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/// Number of simulated frames before the demo exits cleanly.
constexpr int kMaxFrames = 120;

/// Simulated fixed timestep in seconds (60 Hz).
constexpr double kDt = 1.0 / 60.0;

/// Script asset category used when registering with HotReloadBus.
/// There is no dedicated kScript variant in AssetCategory; kShader is the
/// closest semantic match for an on-disk text asset that drives runtime
/// behaviour, and its string label ("shader") does not affect dispatch logic.
using cd::game::asset_hot_reload::AssetCategory;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Resolve the path to scripts/entity_behavior.lua relative to this binary's
/// working directory.  In CI the working directory is the build output folder
/// so we fall back to the source-side path if the local copy is absent.
[[nodiscard]] fs::path locate_script()
{
    // Prefer a copy next to the executable (install / build output).
    const fs::path local { "scripts/entity_behavior.lua" };
    if (fs::exists(local))
        return fs::absolute(local);

    // Development fallback: CMake sets CMAKE_CURRENT_SOURCE_DIR as a compile
    // definition (CHROMA_SAMPLE_SOURCE_DIR) injected by cd_add_sample.  When
    // absent we use the current working directory.
#ifdef CHROMA_SAMPLE_SOURCE_DIR
    const fs::path src_side { CHROMA_SAMPLE_SOURCE_DIR
                              "/scripts/entity_behavior.lua" };
    if (fs::exists(src_side))
        return fs::absolute(src_side);
#endif

    // Last resort — return the local path and let run_file() produce a clear
    // error message.
    return fs::absolute(local);
}

/// Read a file to a std::string. Returns empty string on failure.
[[nodiscard]] std::string read_file(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f.is_open())
        return {};
    const auto sz = f.tellg();
    if (sz <= 0)
        return {};
    std::string out(static_cast<std::size_t>(sz), '\0');
    f.seekg(0);
    f.read(out.data(), sz);
    return out;
}

/// Print the entity's current position to stdout.
void log_entity_position(int frame, const cd::scene::LocalTransform& lt)
{
    const auto& pos = lt.value.position;
    std::printf("[tick %3d]  entity pos = (%6.3f, %6.3f, %6.3f)\n",
                frame, static_cast<double>(pos.x),
                static_cast<double>(pos.y),
                static_cast<double>(pos.z));
    std::fflush(stdout);
}

}  // namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main()
{
    std::printf("=== hello_hot_reload — Lua entity behavior + live re-apply ===\n");
    std::fflush(stdout);

    // ---- Locate the Lua script --------------------------------------------
    const fs::path script_path = locate_script();
    std::printf("[init]  watching script: %s\n", script_path.string().c_str());
    std::fflush(stdout);

    // ---- ECS world + entity -----------------------------------------------
    cd::ecs::World world;
    const cd::ecs::Entity entity = world.create();

    // Attach an identity LocalTransform so Lua can read/write it.
    cd::scene::LocalTransform lt {};
    lt.value.position = cd::math::Vec3f { 0.0F, 0.0F, 0.0F };
    lt.value.scale    = cd::math::Vec3f { 1.0F, 1.0F, 1.0F };
    world.emplace<cd::scene::LocalTransform>(entity, lt);

    // ---- Script engine + bindings -----------------------------------------
    cd::script::Engine eng;
    if (!eng.valid())
    {
        std::fprintf(stderr, "[fatal]  cd::script::Engine allocation failed\n");
        return 1;
    }

    cd::script::BindingState* binding_state = cd::script::register_bindings(eng);
    if (binding_state == nullptr)
    {
        std::fprintf(stderr, "[fatal]  cd::script::register_bindings failed\n");
        return 1;
    }

    // Expose the ECS World to Lua as the global "world".
    if (!cd::script::bind_world(binding_state, &world))
    {
        std::fprintf(stderr, "[fatal]  cd::script::bind_world failed\n");
        return 1;
    }

    // Seed the entity into Lua globals so on_tick can reference it without
    // needing to call world:create() — the entity already exists in C++.
    eng.set_global("entity_id",  static_cast<double>(entity.id));
    eng.set_global("entity_gen", static_cast<double>(entity.generation));

    // Expose a helper script that reconstructs the entity table from the two
    // globals; on_tick() receives it as an argument each frame. The entity
    // table follows the Bindings.hpp convention:
    //   { id = <int>, generation = <int> }
    constexpr const char* kBootstrap = R"(
        -- Reconstruct the entity table from C++-injected globals.
        the_entity = { id = math.floor(entity_id), generation = math.floor(entity_gen) }
    )";
    if (auto r = eng.run_string(kBootstrap); !r.has_value())
    {
        std::fprintf(stderr, "[fatal]  bootstrap script failed: %.*s\n",
                     static_cast<int>(eng.last_error().size()),
                     eng.last_error().data());
        return 1;
    }

    // ---- Load the behavior script for the first time ----------------------
    {
        const std::string src = read_file(script_path);
        if (src.empty())
        {
            std::fprintf(stderr, "[fatal]  could not read script: %s\n",
                         script_path.string().c_str());
            return 1;
        }
        if (auto r = eng.run_string(src); !r.has_value())
        {
            std::fprintf(stderr, "[fatal]  initial script load failed: %.*s\n",
                         static_cast<int>(eng.last_error().size()),
                         eng.last_error().data());
            return 1;
        }
    }
    std::printf("[init]  script loaded OK; starting tick loop (%d frames)\n",
                kMaxFrames);
    std::fflush(stdout);

    // ---- Hot-reload bus ---------------------------------------------------
    // Default 100 ms throttle window — bursts within the window collapse to
    // one reload callback per file, matching the HotReloadBus design intent.
    cd::game::asset_hot_reload::HotReloadBus hot_bus;

    int reload_count = 0;

    const cd::game::asset_hot_reload::SubscriptionId sub_id =
        hot_bus.watch(
            script_path.string(),
            AssetCategory::kShader,   // closest category for a text script asset
            [&](const cd::game::asset_hot_reload::ReloadEvent& ev)
            {
                if (ev.kind == cd::game::asset_hot_reload::ChangeKind::kDeleted)
                {
                    std::printf("[reload] warning: script file deleted — "
                                "continuing with last known on_tick\n");
                    std::fflush(stdout);
                    return;
                }

                const std::string src = read_file(script_path);
                if (src.empty())
                {
                    std::printf("[reload] warning: could not read %s — "
                                "skipping reload\n", ev.path.data());
                    std::fflush(stdout);
                    return;
                }

                if (auto r = eng.run_string(src); r.has_value())
                {
                    ++reload_count;
                    std::printf("[reload] %.*s re-applied (reload #%d)\n",
                                static_cast<int>(ev.path.size()), ev.path.data(),
                                reload_count);
                }
                else
                {
                    std::printf("[reload] re-execute failed: %.*s\n",
                                static_cast<int>(eng.last_error().size()),
                                eng.last_error().data());
                }
                std::fflush(stdout);
            });

    if (!sub_id.is_valid())
    {
        std::fprintf(stderr, "[warn]  HotReloadBus::watch returned invalid id "
                             "(file may not exist yet)\n");
    }

    // ---- Tick loop --------------------------------------------------------
    std::vector<double> call_args;
    call_args.reserve(1);

    for (int frame = 0; frame < kMaxFrames; ++frame)
    {
        // Poll HotReloadBus first so a reload fires before the tick that
        // would use the old script.
        hot_bus.tick();

        // Push dt to Lua as a global — the on_tick(entity, dt) signature
        // receives entity from the global "the_entity" and dt from the arg.
        // We pass dt as a numeric arg via call_global_numeric for type safety.
        call_args.clear();
        call_args.push_back(kDt);

        // call_global_numeric pushes each element of call_args as Lua numbers,
        // calls on_tick, and expects 0 return values.
        std::vector<double> out;
        if (auto r = eng.call_global_numeric("on_tick", call_args, out, 0);
            !r.has_value())
        {
            std::fprintf(stderr, "[tick %3d] on_tick failed: %.*s\n", frame,
                         static_cast<int>(eng.last_error().size()),
                         eng.last_error().data());
            // Non-fatal — print the error and continue so the demo survives a
            // syntax error in the edited file.
            continue;
        }

        // Read back the entity transform that Lua may have mutated.
        const cd::scene::LocalTransform* xf =
            world.get<cd::scene::LocalTransform>(entity);
        if (xf != nullptr)
            log_entity_position(frame, *xf);
    }

    // ---- Summary ----------------------------------------------------------
    std::printf("\n=== Summary ===\n");
    std::printf("  frames_ticked  = %d\n", kMaxFrames);
    std::printf("  reloads        = %d\n", reload_count);
    std::printf("  scripts_run    = %llu\n",
                static_cast<unsigned long long>(eng.script_count()));

    const cd::scene::LocalTransform* final_xf =
        world.get<cd::scene::LocalTransform>(entity);
    if (final_xf != nullptr)
    {
        const auto& p = final_xf->value.position;
        std::printf("  final_pos      = (%.4f, %.4f, %.4f)\n",
                    static_cast<double>(p.x),
                    static_cast<double>(p.y),
                    static_cast<double>(p.z));
    }

    hot_bus.unwatch_subscription(sub_id);
    std::printf("[hello_hot_reload] done\n");
    return 0;
}
