// =============================================================================
// CHROMODYNAMIC — samples/hello_asset_registry
//
// Demonstrates cd::asset::AssetRegistry + the wave-10 loader adapters:
//   * cd::asset_json::JsonAssetLoader
//   * cd::asset_wav::WavAssetLoader
//
// Headless, in-memory VFS — no disk artefacts. The point is to show the
// full "register loader → load by tag → downcast to typed asset" loop
// that an application's asset bootstrap would do at startup.
// =============================================================================
#include <cd/asset/AssetRegistry.hpp>
#include <cd/asset_json/AssetLoader.hpp>
#include <cd/asset_wav/AssetLoader.hpp>
#include <cd/vfs/MemorySource.hpp>
#include <cd/vfs/VirtualFileSystem.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace
{

// Build a tiny mono s16 PCM WAV in memory (16 samples of zeros).
std::vector<std::byte> make_tiny_wav()
{
    constexpr std::uint32_t fmt_size  = 16;
    constexpr std::uint32_t frame     = 16;
    constexpr std::uint32_t data_size = frame * 2;
    constexpr std::uint32_t riff_size = 4 + 8 + fmt_size + 8 + data_size;

    std::vector<std::byte> v;
    auto push16 = [&](std::uint16_t x) {
        v.push_back(std::byte { static_cast<unsigned char>(x & 0xFFu) });
        v.push_back(std::byte { static_cast<unsigned char>((x >> 8u) & 0xFFu) });
    };
    auto push32 = [&](std::uint32_t x) {
        push16(static_cast<std::uint16_t>(x & 0xFFFFu));
        push16(static_cast<std::uint16_t>((x >> 16u) & 0xFFFFu));
    };
    auto push_tag = [&](const char (&t)[5]) {
        for (int i = 0; i < 4; ++i)
            v.push_back(static_cast<std::byte>(t[i]));
    };

    push_tag("RIFF"); push32(riff_size); push_tag("WAVE");
    push_tag("fmt "); push32(fmt_size);
    push16(1); push16(1); push32(44100); push32(44100 * 2); push16(2); push16(16);
    push_tag("data"); push32(data_size);
    v.insert(v.end(), data_size, std::byte { 0 });
    return v;
}

}  // namespace

int main()
{
    std::printf("=== hello_asset_registry — AssetRegistry + wav/json loaders ===\n");

    // 1. Build a virtual filesystem with an in-memory source layered in.
    auto src = std::make_shared<cd::vfs::MemorySource>("embedded");
    src->put_text("data/config.json",
        R"({"app":"chromodynamic","version":1,"vsync":true})");
    src->put("audio/empty.wav", make_tiny_wav());

    cd::vfs::VirtualFileSystem vfs;
    vfs.mount_back(src);
    std::printf("vfs files: ");
    for (const auto& p : src->list(""))
        std::printf("%s  ", p.c_str());
    std::printf("\n");

    // 2. Wire up the asset registry with the two loader adapters.
    cd::asset::AssetRegistry registry { vfs };
    registry.register_loader(std::make_unique<cd::asset_json::JsonAssetLoader>());
    registry.register_loader(std::make_unique<cd::asset_wav::WavAssetLoader>());
    std::printf("registered loaders: %zu\n", registry.loader_count());

    // 3. Load both assets through the registry.
    auto cfg_id = registry.load("json", "data/config.json");
    if (!cfg_id) { std::printf("load json failed\n"); return 1; }
    auto wav_id = registry.load("wav",  "audio/empty.wav");
    if (!wav_id) { std::printf("load wav failed\n"); return 2; }
    std::printf("cached assets: %zu\n", registry.cached_count());

    // 4. Downcast to the concrete asset types and use them.
    const auto* cfg = dynamic_cast<const cd::asset_json::JsonAsset*>(registry.find(*cfg_id));
    const auto* wav = dynamic_cast<const cd::asset_wav::WavAsset*>(registry.find(*wav_id));
    if (cfg == nullptr || wav == nullptr) { std::printf("downcast failed\n"); return 3; }

    auto name = cfg->value().at("app");
    if (!name || !(*name)->is_string()) { std::printf("missing app field\n"); return 4; }
    std::printf("app name : %s\n", (*name)->as_string().c_str());

    std::printf("wav meta : %u Hz, %u channels, %u bits, %zu frames\n",
        wav->wav().sample_rate, wav->wav().channels, wav->wav().bits_per_sample,
        wav->wav().frame_count());

    // 5. Idempotency check — second load returns the cached id.
    auto wav_id2 = registry.load("wav", "audio/empty.wav");
    if (!wav_id2 || *wav_id2 != *wav_id) { std::printf("cache miss on second load\n"); return 5; }
    std::printf("second load hit cache (id matches)\n");

    std::printf("[hello_asset_registry] done\n");
    return 0;
}
