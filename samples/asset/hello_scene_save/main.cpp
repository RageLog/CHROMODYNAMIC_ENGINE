// =============================================================================
// CHROMODYNAMIC — samples/hello_scene_save
// Headless: build a scene tree, serialize → JSON, deserialize into a fresh
// world, assert the topology is identical. Demonstrates the full cd::scene
// save/load loop.
// =============================================================================
#include <cd/asset/json/Json.hpp>
#include <cd/ecs/World.hpp>
#include <cd/math/Transform.hpp>
#include <cd/scene/Scene.hpp>
#include <cd/scene/Serializer.hpp>

#include <cstdio>

namespace
{

bool approx_eq(float a, float b, float eps = 1e-5F)
{
    return std::fabs(a - b) < eps;
}

}  // namespace

int main()
{
    std::printf("=== hello_scene_save — scene <-> JSON round-trip ===\n");

    // 1. Build a small scene: a root with three children, each at a different
    //    translation.
    cd::ecs::World w_src;
    cd::scene::Scene s_src { w_src };
    auto root = s_src.create_node();
    s_src.local(root)->value.position = cd::math::Vec3f { 0.0F, 0.0F, 0.0F };

    auto a = s_src.create_node();
    s_src.local(a)->value.position = cd::math::Vec3f { 1.0F, 0.0F, 0.0F };
    s_src.attach(a, root);

    auto b = s_src.create_node();
    s_src.local(b)->value.position = cd::math::Vec3f { 0.0F, 2.0F, 0.0F };
    s_src.local(b)->value.scale = cd::math::Vec3f { 0.5F, 0.5F, 0.5F };
    s_src.attach(b, root);

    auto c = s_src.create_node();
    s_src.local(c)->value.position = cd::math::Vec3f { 0.0F, 0.0F, 3.0F };
    s_src.attach(c, root);

    std::printf("source scene: 1 root, 3 children\n");

    // 2. Serialize.
    const auto json = cd::scene::serialize_scene(s_src);
    const auto text = cd::asset::json::serialize(json, true);
    std::printf("\n--- pretty JSON (%zu chars) ---\n%s\n", text.size(), text.c_str());

    // 3. Parse back from text and deserialize into a fresh world.
    auto reparsed = cd::asset::json::parse(text);
    if (!reparsed)
    {
        std::printf("re-parse failed\n");
        return 1;
    }

    cd::ecs::World w_dst;
    cd::scene::Scene s_dst { w_dst };
    auto restored = cd::scene::deserialize_scene(s_dst, *reparsed);
    if (!restored)
    {
        std::printf(
            "deserialize failed: %.*s\n",
            static_cast<int>(restored.error().message.size()),
            restored.error().message.data()
        );
        return 2;
    }

    // 4. Verify topology + transforms.
    auto root2 = restored->at(root.id);
    auto a2 = restored->at(a.id);
    auto b2 = restored->at(b.id);
    auto c2 = restored->at(c.id);

    if (s_dst.parent_of(root2).is_valid())
    {
        std::printf("root must not have a parent\n");
        return 3;
    }
    if (s_dst.parent_of(a2) != root2 || s_dst.parent_of(b2) != root2 || s_dst.parent_of(c2) != root2)
    {
        std::printf("child parent links not preserved\n");
        return 4;
    }
    const auto& bp = s_dst.local(b2)->value;
    if (!approx_eq(bp.position.y, 2.0F) || !approx_eq(bp.scale.x, 0.5F))
    {
        std::printf("scale + translation not preserved\n");
        return 5;
    }

    std::printf("\n[hello_scene_save] round-trip OK (4 nodes, parent links intact)\n");
    return 0;
}
