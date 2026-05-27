// =============================================================================
// CHROMODYNAMIC — hello_runtime sample
//
// Sprint S2.10 integration smoke-test. Builds an EngineContext, registers a
// few CVars, mounts an in-memory VFS source, loads a config blob from it,
// dispatches a parallel job that touches cd::math, and logs the result.
// =============================================================================
#include <cd/concurrency/WorkStealingThreadPool.hpp>
#include <cd/config/Config.hpp>
#include <cd/core/CVar.hpp>
#include <cd/io/BinaryStream.hpp>
#include <cd/log/ConsoleLogger.hpp>
#include <cd/math/Transform.hpp>
#include <cd/math/Vector.hpp>
#include <cd/runtime/EngineContext.hpp>
#include <cd/vfs/MemorySource.hpp>

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <memory>

int main()
{
    cd::runtime::EngineContext engine;
    auto logger = std::make_shared<cd::log::ConsoleLogger>(cd::log::LogLevel::Info);
    engine.set_logger(logger);

    // 1) Seed initial CVars and persist them into a memory-backed VFS layer.
    engine.cvars().set("camera.fov", 75.0);
    engine.cvars().set("renderer.vsync", true);
    engine.cvars().set("game.title", std::string { "CHROMODYNAMIC" });

    cd::io::BinaryWriter writer;
    cd::config::save(writer, engine.cvars());
    auto blob = writer.release();

    auto memfs = std::make_shared<cd::vfs::MemorySource>("config-mem");
    memfs->put("config.bin", std::move(blob));
    engine.vfs().mount_back(memfs);

    // 2) Read it back through the VFS and reload into a fresh registry.
    cd::core::CVarRegistry reloaded;
    if (auto r = engine.vfs().read("config.bin"); r.has_value())
    {
        cd::io::BinaryReader reader { *r };
        if (auto ok = cd::config::load(reader, reloaded); !ok.has_value())
        {
            std::fprintf(stderr, "config load failed: %u\n", ok.error().code);
            return 1;
        }
    }
    else
    {
        std::fprintf(stderr, "vfs read failed: %u\n", r.error().code);
        return 1;
    }
    std::printf(
        "[hello_runtime] reloaded %zu cvars; fov=%.1f title=%s\n",
        reloaded.size(),
        *reloaded.get_as<double>("camera.fov"),
        reloaded.get_as<std::string>("game.title")->c_str()
    );

    // 3) Dispatch a parallel math job across the worker pool.
    constexpr int kN = 256;
    std::atomic<int> done { 0 };
    cd::math::Vec3f origin { 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < kN; ++i)
    {
        engine.thread_pool().submit_detached(
            [i, &done, origin]
            {
                cd::math::Transformf t;
                t.position = { static_cast<float>(i), 0.0f, 0.0f };
                t.rotation = cd::math::Quatf::from_axis_angle({ 0.0f, 1.0f, 0.0f }, 0.01f * static_cast<float>(i));
                auto p = t.apply_to_point(origin);
                (void)p;
                done.fetch_add(1, std::memory_order_relaxed);
            }
        );
    }
    engine.thread_pool().wait_all();
    std::printf("[hello_runtime] dispatched %d/%d jobs on %zu workers\n", done.load(), kN, engine.worker_count());

    return 0;
}
