// =============================================================================
// CHROMODYNAMIC — tools/cook_texture
//
// Offline texture cooker. Takes a PNG/JPG/etc. via cd::asset_image, runs
// it through cd::asset_image::compress_bc7, and writes the BC7 block
// stream to disk. Output format is intentionally minimal — a 16-byte
// header + raw block bytes — so the engine can fread + Vulkan-upload
// straight into a VK_FORMAT_BC7_UNORM_BLOCK texture without a parser.
//
// Output file format ("CDBC7" magic):
//   ┌────────────── HEADER (16 B) ──────────────┐
//   │ magic[5]   "CDBC7"                          │
//   │ version    u8  = 1                          │
//   │ width      u32 (logical pixel width)        │
//   │ height     u32 (logical pixel height)       │
//   │ block_w    u16 (== ceil(width / 4))         │
//   │ block_h    u16 (== ceil(height / 4))        │
//   └─────────────────────────────────────────────┘
//   Payload: block_w * block_h * 16 bytes of BC7 blocks.
//
// Usage:
//   cd_cook_texture -i <input.png> -o <output.cdtex> [-q fast|balanced|high]
//   cd_cook_texture -i albedo.png -o albedo.cdtex -q high
//
// Quality presets map to bc7enc uber_level (0/2/4). Default `balanced`.
// =============================================================================
#include <cd/asset_image/Bc7.hpp>
#include <cd/asset_image/Image.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

struct Args
{
    std::string input;
    std::string output;
    cd::asset_image::Bc7Quality quality { cd::asset_image::Bc7Quality::kBalanced };
    bool verbose { false };
    bool gen_mips { false };  ///< --mips: emit full mip chain (.cdtex v2 format).
};

[[nodiscard]] bool parse_quality(std::string_view s, cd::asset_image::Bc7Quality& out) noexcept
{
    if (s == "fast")
    {
        out = cd::asset_image::Bc7Quality::kFast;
        return true;
    }
    if (s == "balanced")
    {
        out = cd::asset_image::Bc7Quality::kBalanced;
        return true;
    }
    if (s == "high")
    {
        out = cd::asset_image::Bc7Quality::kHigh;
        return true;
    }
    return false;
}

[[nodiscard]] bool parse_args(int argc, char** argv, Args& out)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view a { argv[i] };
        if ((a == "--input" || a == "-i") && i + 1 < argc)
        {
            out.input = argv[++i];
        }
        else if ((a == "--output" || a == "-o") && i + 1 < argc)
        {
            out.output = argv[++i];
        }
        else if ((a == "--quality" || a == "-q") && i + 1 < argc)
        {
            if (!parse_quality(argv[++i], out.quality))
            {
                std::fprintf(stderr, "cook_texture: bad --quality value (use fast|balanced|high)\n");
                return false;
            }
        }
        else if (a == "--verbose" || a == "-v")
        {
            out.verbose = true;
        }
        else if (a == "--mips" || a == "-m")
        {
            out.gen_mips = true;
        }
        else if (a == "--help" || a == "-h")
        {
            return false;
        }
        else
        {
            std::fprintf(stderr, "cook_texture: unknown argument: %.*s\n", static_cast<int>(a.size()), a.data());
            return false;
        }
    }
    return !out.input.empty() && !out.output.empty();
}

void print_usage()
{
    std::printf("Usage: cook_texture -i <input.png> -o <output.cdtex> [-q fast|balanced|high] [-m|--mips]\n");
    std::printf("  -m / --mips    Generate full mip chain and emit .cdtex v2 (default: single mip, v1)\n");
}

void write_u32(std::ofstream& f, std::uint32_t v)
{
    const std::uint8_t b[4] { static_cast<std::uint8_t>(v),
                              static_cast<std::uint8_t>(v >> 8),
                              static_cast<std::uint8_t>(v >> 16),
                              static_cast<std::uint8_t>(v >> 24) };
    f.write(reinterpret_cast<const char*>(b), 4);
}

void write_u16(std::ofstream& f, std::uint16_t v)
{
    const std::uint8_t b[2] { static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v >> 8) };
    f.write(reinterpret_cast<const char*>(b), 2);
}

}  // namespace

int main(int argc, char** argv)
{
    Args args;
    if (!parse_args(argc, argv, args))
    {
        print_usage();
        return 1;
    }

    // ---- Decode ----
    auto image = cd::asset_image::load_image(args.input);
    if (!image.has_value())
    {
        std::fprintf(
            stderr,
            "cook_texture: load failed: %.*s\n",
            static_cast<int>(image.error().message.size()),
            image.error().message.data()
        );
        return 2;
    }
    if (args.verbose)
        std::printf("cook_texture: loaded %u x %u (%s alpha)\n",
                    image->width, image->height, image->has_alpha ? "with" : "no");

    // ---- Build mip chain (1-deep when --mips not set) ----
    std::vector<cd::asset_image::Image> mip_chain;
    if (args.gen_mips)
    {
        auto chain = cd::asset_image::generate_mips(*image);
        if (!chain.has_value())
        {
            std::fprintf(
                stderr,
                "cook_texture: generate_mips failed: %.*s\n",
                static_cast<int>(chain.error().message.size()),
                chain.error().message.data()
            );
            return 3;
        }
        mip_chain = std::move(*chain);
    }
    else
    {
        mip_chain.push_back(*image);
    }
    if (args.verbose)
        std::printf("cook_texture: generated %zu mip level(s)\n", mip_chain.size());

    // ---- Compress every mip via BC7 ----
    std::vector<cd::asset_image::Bc7Block> encoded;
    encoded.reserve(mip_chain.size());
    std::size_t total_encoded_bytes = 0;
    std::size_t total_source_bytes = 0;
    for (std::size_t i = 0; i < mip_chain.size(); ++i)
    {
        const auto& m = mip_chain[i];
        auto bc7 = cd::asset_image::compress_bc7(m.rgba, m.width, m.height, args.quality);
        if (!bc7.has_value())
        {
            std::fprintf(
                stderr,
                "cook_texture: BC7 encode mip %zu failed: %.*s\n",
                i,
                static_cast<int>(bc7.error().message.size()),
                bc7.error().message.data()
            );
            return 3;
        }
        if (args.verbose)
            std::printf("  mip %zu: %u x %u (%u x %u blocks, %zu bytes)\n",
                        i, m.width, m.height, bc7->block_w, bc7->block_h, bc7->data.size());
        total_encoded_bytes += bc7->data.size();
        total_source_bytes += m.rgba.size();
        encoded.push_back(std::move(*bc7));
    }
    if (args.verbose)
        std::printf(
            "cook_texture: total %zu BC7 bytes from %zu source RGBA bytes (%.1fx compression)\n",
            total_encoded_bytes,
            total_source_bytes,
            static_cast<double>(total_source_bytes) / static_cast<double>(total_encoded_bytes)
        );

    // ---- Write .cdtex ----
    std::ofstream out(args.output, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        std::fprintf(stderr, "cook_texture: cannot write %s\n", args.output.c_str());
        return 4;
    }
    constexpr char kMagic[5] = { 'C', 'D', 'B', 'C', '7' };
    out.write(kMagic, 5);
    // v1 single-mip, v2 multi-mip. Pick the smallest version that can
    // describe what we have so older readers keep working.
    const std::uint8_t version = (mip_chain.size() == 1) ? 1U : 2U;
    out.write(reinterpret_cast<const char*>(&version), 1);
    write_u32(out, image->width);
    write_u32(out, image->height);
    write_u16(out, static_cast<std::uint16_t>(encoded[0].block_w));
    write_u16(out, static_cast<std::uint16_t>(encoded[0].block_h));
    if (version == 2U)
    {
        const std::uint8_t mip_count = static_cast<std::uint8_t>(mip_chain.size());
        out.write(reinterpret_cast<const char*>(&mip_count), 1);
    }
    for (const auto& m : encoded)
    {
        out.write(reinterpret_cast<const char*>(m.data.data()), static_cast<std::streamsize>(m.data.size()));
    }
    if (!out.good())
    {
        std::fprintf(stderr, "cook_texture: write failed\n");
        return 4;
    }
    if (args.verbose)
        std::printf("cook_texture: wrote %s (v%u, %zu mip(s))\n",
                    args.output.c_str(), static_cast<unsigned>(version), mip_chain.size());
    return 0;
}
