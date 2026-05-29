// =============================================================================
// CHROMODYNAMIC — samples/hello_bench
// Demonstrates cd::bench on engine hot paths. Phase 12.A expansion:
// foundation math + ECS + handles + error format + log ring + imgdiff
// all benched, alongside the original asset/scene/pool/result set.
// =============================================================================
#include <cd/asset/json/Json.hpp>
#include <cd/bench/Benchmark.hpp>
#include <cd/core/CVar.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/ErrorFormat.hpp>
#include <cd/core/Handle.hpp>
#include <cd/core/HandleStore.hpp>
#include <cd/core/Result.hpp>
#include <cd/ecs/World.hpp>
#include <cd/imgdiff/ImageDiff.hpp>
#include <cd/log/RingBufferSink.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Transform.hpp>
#include <cd/math/Vector.hpp>
#include <cd/mem/PoolAllocator.hpp>
#include <cd/scene/Scene.hpp>
#include <cd/scene/Serializer.hpp>

#include <array>
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
    auto r = cd::asset::json::parse(kJsonInput);
    cd::bench::do_not_optimize(r);
}

cd::asset::json::Value g_serialize_root;

void bench_json_serialize()
{
    auto s = cd::asset::json::serialize(g_serialize_root, false);
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

// ---- Phase 12.A bench expansion ----------------------------------------
// Foundation hot paths the original bench set didn't cover.

// 8. cd::math::Mat4f multiply — frame's MVP-construction inner loop.
cd::math::Mat4f g_mat_a;
cd::math::Mat4f g_mat_b;

void bench_mat4_multiply()
{
    auto r = g_mat_a * g_mat_b;
    cd::bench::do_not_optimize(r);
}

// 9. cd::math::Mat4f inverse — view-projection inverse for screen-space
//    raycasts, picking, decal projection.
void bench_mat4_inverse()
{
    auto r = cd::math::inverse(g_mat_a);
    cd::bench::do_not_optimize(r);
}

// 10. cd::math::Vec3f normalize — happens in every vertex / lighting
//     shader's CPU-side setup, plus camera basis recompute.
cd::math::Vec3f g_vec_a { 1.0F, 2.0F, 3.0F };

void bench_vec3_normalize()
{
    auto r = cd::math::normalize(g_vec_a);
    cd::bench::do_not_optimize(r);
}

// 11. cd::math::slerp — animation playback's hot quaternion blend.
cd::math::Quatf g_quat_a { 0.0F, 0.0F, 0.0F, 1.0F };
cd::math::Quatf g_quat_b { 0.0F, 0.707F, 0.0F, 0.707F };

void bench_quat_slerp()
{
    auto r = cd::math::slerp(g_quat_a, g_quat_b, 0.5F);
    cd::bench::do_not_optimize(r);
}

// 12. cd::math::Transform -> Mat4 (per-entity world matrix synthesis).
cd::math::Transformf g_transform;

void bench_transform_to_mat4()
{
    auto m = cd::math::to_mat4(g_transform);
    cd::bench::do_not_optimize(m);
}

// 13. cd::core::Handle<T> create — generation+index bit-pack.
namespace bench_tags
{
struct Texture { };
}  // namespace bench_tags

void bench_handle_create()
{
    cd::core::Handle<bench_tags::Texture> h { 42U, 7U, 3U };
    cd::bench::do_not_optimize(h);
}

// 14. cd::core::HandleStore<T,Tag>::insert+erase round-trip — slot-map's
//     core mutation; the ECS, the RHI, the asset registry all lean on this.
struct HandleStoreState
{
    cd::core::HandleStore<int, bench_tags::Texture> store;
};

HandleStoreState g_handle_store_state;

void bench_handle_store_insert_erase()
{
    auto r = g_handle_store_state.store.insert(123);
    if (r.has_value())
    {
        cd::bench::do_not_optimize(*r);
        (void)g_handle_store_state.store.erase(*r);
    }
}

// 15. cd::core::format(ErrorCode) — non-owning literal message path.
//     The cold path; we still want to know its envelope.
void bench_error_format_literal()
{
    auto ec = cd::core::core_errors::make(
        cd::core::core_errors::Code::kInvalidArgument, "bench");
    auto s = cd::core::format(ec);
    cd::bench::do_not_optimize(s);
}

// 16. cd::core::format(ErrorCode) — owning shared_ptr message path
//     (Wave 126 fix). Allocates; the comparison vs. literal is the
//     signal we want.
void bench_error_format_owning()
{
    auto ec = cd::core::ErrorCode::make_owning(
        cd::core::core_errors::kDomain,
        static_cast<std::uint32_t>(cd::core::core_errors::Code::kInvalidArgument),
        std::string { "bench owning message" });
    auto s = cd::core::format(ec);
    cd::bench::do_not_optimize(s);
}

// 17. cd::log::RingBufferSink push + snapshot — every frame logs into
//     the panic-triage mirror; cost here multiplies by log volume.
struct RingState
{
    cd::log::RingBufferSink sink { 64 };
    cd::log::LogRecord record;

    RingState()
    {
        record.message = "bench record";
        record.level = cd::log::LogLevel::Info;
    }
};

RingState* g_ring_state = nullptr;

void bench_ring_push()
{
    g_ring_state->sink.on_log_record(g_ring_state->record);
}

void bench_ring_snapshot()
{
    auto snap = g_ring_state->sink.snapshot();
    cd::bench::do_not_optimize(snap);
}

// 18. cd::imgdiff::compare — golden gate's per-frame cost. 64x64 is
//     the smallest tile a real screenshot would have; the per-pixel
//     loop has a stable cost we can track.
constexpr std::uint32_t kImgW = 64;
constexpr std::uint32_t kImgH = 64;
struct ImgState
{
    std::vector<std::uint8_t> a;
    std::vector<std::uint8_t> b;

    ImgState() : a(kImgW * kImgH * 4U, 0), b(kImgW * kImgH * 4U, 0)
    {
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            a[i] = static_cast<std::uint8_t>(i & 0xFFU);
            b[i] = static_cast<std::uint8_t>((i + 1U) & 0xFFU);
        }
    }
};

ImgState* g_img_state = nullptr;

void bench_imgdiff_compare()
{
    cd::imgdiff::ImageView va { g_img_state->a.data(), kImgW, kImgH };
    cd::imgdiff::ImageView vb { g_img_state->b.data(), kImgW, kImgH };
    auto r = cd::imgdiff::compare(va, vb, 0);
    cd::bench::do_not_optimize(r);
}

// 19. cd::core::CVarRegistry set+get — config-mutation hot path
//     (debug menus, console commands, hot-reload of config).
struct CVarState
{
    cd::core::CVarRegistry registry;
};

CVarState* g_cvar_state = nullptr;

void bench_cvar_set_get()
{
    g_cvar_state->registry.set("bench.value", static_cast<std::int64_t>(42));
    auto v = g_cvar_state->registry.get_as<std::int64_t>("bench.value");
    cd::bench::do_not_optimize(v);
}

// 20. cd::ecs::World::for_each over 256 entities — the steady-state
//     game-loop iteration pattern.
struct EcsBenchState
{
    cd::ecs::World world;

    EcsBenchState()
    {
        for (int i = 0; i < 256; ++i)
        {
            auto e = world.create();
            world.emplace<float>(e, static_cast<float>(i));
        }
    }
};

EcsBenchState* g_ecs_state = nullptr;

void bench_ecs_for_each_256()
{
    float sum = 0.0F;
    g_ecs_state->world.for_each<float>([&](cd::ecs::Entity, float& f) {
        sum += f;
    });
    cd::bench::do_not_optimize(sum);
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
    if (auto r = cd::asset::json::parse(kJsonInput); r)
        g_serialize_root = std::move(*r);
    add("asset_json_parse_S", bench_json_parse);
    add("asset_json_serialize_S", bench_json_serialize);

    SceneBenchState scene_state;
    g_scene_state = &scene_state;
    add("scene_serialize_16", bench_scene_serialize);

    // ---- Phase 12.A bench expansion ----
    // Prime the math state with non-trivial matrices so the multiply
    // and inverse have a real workload (identity would optimize away).
    for (std::size_t c = 0; c < 4; ++c)
    {
        const auto cf = static_cast<float>(c);
        g_mat_a[c] = cd::math::Vec4f { cf * 0.13F + 1.0F, cf * 0.27F, cf * 0.41F, 1.0F };
        g_mat_b[c] = cd::math::Vec4f { 1.0F + cf * 0.17F, cf * 0.31F, cf * 0.43F, 1.0F };
    }
    add("math_mat4_multiply",        bench_mat4_multiply);
    add("math_mat4_inverse",         bench_mat4_inverse);
    add("math_vec3_normalize",       bench_vec3_normalize);
    add("math_quat_slerp",           bench_quat_slerp);

    g_transform.position = { 1.0F, 2.0F, 3.0F };
    g_transform.rotation = g_quat_b;
    g_transform.scale    = { 1.5F, 1.5F, 1.5F };
    add("math_transform_to_mat4",    bench_transform_to_mat4);

    add("core_handle_create",        bench_handle_create);
    add("core_handle_store_ins+ers", bench_handle_store_insert_erase);

    add("core_format_literal",       bench_error_format_literal);
    add("core_format_owning",        bench_error_format_owning);

    RingState ring_state;
    g_ring_state = &ring_state;
    add("log_ring_push",             bench_ring_push);
    add("log_ring_snapshot_64",      bench_ring_snapshot);

    ImgState img_state;
    g_img_state = &img_state;
    add("imgdiff_compare_64x64",     bench_imgdiff_compare);

    CVarState cvar_state;
    g_cvar_state = &cvar_state;
    add("core_cvar_set+get",         bench_cvar_set_get);

    EcsBenchState ecs_state;
    g_ecs_state = &ecs_state;
    add("ecs_for_each_256",          bench_ecs_for_each_256);

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
