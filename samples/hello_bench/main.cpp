// =============================================================================
// CHROMODYNAMIC — samples/hello_bench
// Demonstrates cd::bench on a handful of engine hot paths.
// =============================================================================
#include <cd/asset_json/Json.hpp>
#include <cd/bench/Benchmark.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>
#include <cd/ecs/World.hpp>
#include <cd/mem/PoolAllocator.hpp>
#include <cd/scene/Scene.hpp>
#include <cd/scene/Serializer.hpp>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{

// 1. Tight integer loop — the speed-of-light floor.
void bench_integer_loop()
{
    std::uint64_t acc = 0;
    for (std::uint64_t i = 0; i < 64; ++i)
        acc = acc * 1664525u + 1013904223u + i;
    cd::bench::do_not_optimize(acc);
}

// 2. cd::core::Result success path (no error allocation).
void bench_result_success()
{
    cd::core::Result<int> r = 42;
    cd::bench::do_not_optimize(r);
}

// 3. std::vector reserve+push pattern, commonly used in asset loaders.
void bench_vector_push()
{
    std::vector<int> v;
    v.reserve(64);
    for (int i = 0; i < 64; ++i)
        v.push_back(i);
    cd::bench::do_not_optimize(v.data());
}

// 4. cd::mem::PoolAllocator alloc/free pair — O(1) bump on the free list.
//    We hold the pool external so we don't pay ctor cost per iteration.
struct PoolBenchState
{
    cd::mem::PoolAllocator pool { sizeof(std::uint64_t), 1024 };
};

PoolBenchState g_pool_state;

void bench_pool_alloc_free()
{
    void* p = g_pool_state.pool.allocate(sizeof(std::uint64_t), alignof(std::uint64_t));
    cd::bench::do_not_optimize(p);
    if (p != nullptr)
        g_pool_state.pool.deallocate(p);
}

// 5+6. cd::asset_json parse + serialize on a small object — the kind of
// payload a config or scene-sidecar file would have. Volatile string sink
// keeps the optimizer honest on the parse result.
constexpr std::string_view kJsonInput = R"({"name":"clip","gain":0.85,"loop":true,"chunks":[0,1,2,3,4,5,6,7]})";

void bench_json_parse()
{
    auto r = cd::asset_json::parse(kJsonInput);
    cd::bench::do_not_optimize(r);
}

cd::asset_json::Value g_serialize_root;

void bench_json_serialize()
{
    auto s = cd::asset_json::serialize(g_serialize_root, false);
    cd::bench::do_not_optimize(s);
}

// 7. cd::scene::serialize_scene on a 16-node scene — the kind of payload
//    an editor "save scene" button emits. State held externally so the
//    sample doesn't pay scene-construction cost per iteration.
struct SceneBenchState
{
    cd::ecs::World world;
    cd::scene::Scene scene { world };

    SceneBenchState()
    {
        auto root = scene.create_node();
        // 1 root + 15 children — typical small test scene
        for (int i = 0; i < 15; ++i)
        {
            auto c = scene.create_node();
            scene.local(c)->value.position = cd::math::Vec3f { static_cast<float>(i), 0.0F, 0.0F };
            scene.attach(c, root);
        }
    }
};

SceneBenchState* g_scene_state = nullptr;

void bench_scene_serialize()
{
    auto v = cd::scene::serialize_scene(g_scene_state->scene);
    cd::bench::do_not_optimize(v);
}

}  // namespace

int main(int argc, char** argv)
{
    std::string json_out_path;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view a = argv[i];
        constexpr std::string_view kPrefix = "--json=";
        if (a.starts_with(kPrefix))
            json_out_path = std::string { a.substr(kPrefix.size()) };
    }

    std::printf("=== hello_bench — cd::bench microbench demo ===\n");

    cd::bench::Config cfg;
    cfg.min_samples = 64;
    cfg.min_time_ms = 30;

    std::vector<cd::bench::Report> reports;
    auto add = [&](std::string_view name, auto fn) {
        auto r = cd::bench::run(name, fn, cfg);
        r.print(std::cout);
        reports.push_back(std::move(r));
    };

    add("integer_loop_64", bench_integer_loop);
    add("Result<int>_success", bench_result_success);
    add("vector<int>_reserve64", bench_vector_push);
    add("PoolAllocator_alloc+free", bench_pool_alloc_free);

    // Prime the serializer benchmark by parsing once.
    if (auto r = cd::asset_json::parse(kJsonInput); r)
        g_serialize_root = std::move(*r);
    add("asset_json_parse_S", bench_json_parse);
    add("asset_json_serialize_S", bench_json_serialize);

    SceneBenchState scene_state;
    g_scene_state = &scene_state;
    add("scene_serialize_16", bench_scene_serialize);

    // Markdown summary table — drop straight into a CI artifact /
    // PR description for cross-build performance tracking.
    std::printf("\n=== Markdown summary ===\n");
    std::printf("%s\n", std::string { cd::bench::markdown_header() }.c_str());
    for (const auto& r : reports)
        std::printf("%s\n", r.to_markdown_row().c_str());

    // Optional JSON artifact for cd_bench_compare consumption.
    if (!json_out_path.empty())
    {
        std::ofstream out { json_out_path, std::ios::binary | std::ios::trunc };
        if (out)
        {
            out << cd::bench::reports_to_json_array(reports);
            std::printf("[hello_bench] wrote JSON artifact → %s\n", json_out_path.c_str());
        }
        else
        {
            std::printf("[hello_bench] failed to open JSON path %s\n", json_out_path.c_str());
        }
    }

    std::printf("[hello_bench] done\n");
    return 0;
}
