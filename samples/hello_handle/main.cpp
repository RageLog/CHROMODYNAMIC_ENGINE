// =============================================================================
// CHROMODYNAMIC — samples/hello_handle
// Sprint S2.1.a — Handle + HandleStore round-trip demo
// =============================================================================
#include <cd/core/Defines.hpp>
#include <cd/core/Handle.hpp>
#include <cd/core/HandleStore.hpp>
#include <cd/core/SourceLocation.hpp>

#include <cstdio>
#include <string>

namespace demo
{

struct TextureTag
{
};

struct BufferTag
{
};

using TextureHandle = cd::core::Handle<TextureTag>;
using BufferHandle = cd::core::Handle<BufferTag>;

struct Texture
{
    std::string name;
    std::uint32_t width {};
    std::uint32_t height {};
};

struct Buffer
{
    std::string name;
    std::size_t bytes {};
};

}  // namespace demo

int main()
{
    std::printf("=== CHROMODYNAMIC hello_handle ===\n");
    std::printf("Captured at: %s\n", cd::core::file_name_only(cd::core::here()).data());

    cd::core::HandleStore<demo::Texture, demo::TextureTag> textures;
    cd::core::HandleStore<demo::Buffer, demo::BufferTag> buffers;
    textures.set_type_id(0x01);
    buffers.set_type_id(0x02);

    // Insert a couple of textures.
    const auto albedo_r = textures.insert(demo::Texture { "albedo", 2048, 2048 });
    const auto normal_r = textures.insert(demo::Texture { "normal", 2048, 2048 });
    const auto vb_r = buffers.insert(demo::Buffer { "vertex_buffer", 1u << 20u });

    if (!albedo_r || !normal_r || !vb_r)
    {
        std::printf("HandleStore insert failed\n");
        return 1;
    }

    const demo::TextureHandle albedo = *albedo_r;
    const demo::TextureHandle normal = *normal_r;
    const demo::BufferHandle vb = *vb_r;

    std::printf("Inserted %u textures, %u buffers\n", textures.size(), buffers.size());
    std::printf("  albedo: idx=%u gen=%u type=%u\n", albedo.index(), albedo.generation(), albedo.type_id());
    std::printf("  normal: idx=%u gen=%u type=%u\n", normal.index(), normal.generation(), normal.type_id());
    std::printf("  vbuf:   idx=%u gen=%u type=%u\n", vb.index(), vb.generation(), vb.type_id());

    // The compiler rejects cross-tag assignment:
    // demo::TextureHandle bad = vb;  // <-- would not compile (good)

    // Lookup
    if (auto* t = textures.get(albedo))
    {
        std::printf("Lookup albedo OK: name='%s' %ux%u\n", t->name.c_str(), t->width, t->height);
    }

    // Stale-handle defeat: erase, then reinsert into the same slot.
    textures.erase(albedo);
    const auto roughness_r = textures.insert(demo::Texture { "roughness", 1024, 1024 });
    if (!roughness_r)
        return 2;
    const demo::TextureHandle roughness = *roughness_r;

    std::printf("After erase+reinsert: slot reused?\n");
    std::printf("  albedo idx=%u gen=%u (stale)\n", albedo.index(), albedo.generation());
    std::printf("  rough  idx=%u gen=%u (fresh)\n", roughness.index(), roughness.generation());
    std::printf(
        "  albedo.contains? %s   roughness.contains? %s\n",
        textures.contains(albedo) ? "YES (BUG)" : "no (correct)",
        textures.contains(roughness) ? "yes" : "NO (BUG)"
    );

    // Iterate live items.
    std::printf("Live textures:\n");
    textures.for_each(
        [](demo::TextureHandle h, const demo::Texture& tex)
        {
            std::printf("  [idx=%u gen=%u] %s\n", h.index(), h.generation(), tex.name.c_str());
        }
    );

    std::printf("[hello_handle] OK\n");
    return 0;
}
