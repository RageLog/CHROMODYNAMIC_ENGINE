// =============================================================================
// CHROMODYNAMIC — samples/asset/hello_texture_compress_ratio
// Phase 663 — console proof that cd::asset::texture_compress BC1 ratio is real.
//
// Demonstrates cd::asset::texture_compress::encode() + analyze():
//   1. Generate 3 synthetic RGBA8 images: 64x64, 256x256, 1024x1024.
//   2. Encode each to BC1 via encode().
//   3. Measure CompressionStats via analyze().
//   4. Print per-image row: original_bytes / output_bytes / ratio / rmse / time_ms.
//   5. Print a summary table showing 4 MB -> 0.5 MB (8:1 BC1 ratio).
//
// Moment: a texture artist runs this, sees the measured 8:1 ratio + RMSE,
//         and trusts the BC1 encoder is correct and worth shipping in production.
//
// Console-only. No GPU. No window.
// Always exits 0.
// =============================================================================
#include <cd/asset/texture_compress/TextureCompress.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace
{

// ---- synthetic image generators ----------------------------------------------

/// Fill an RGBA8 image with a deterministic colour gradient.
/// The pattern has non-trivial spatial variation so BC1 exercises real
/// endpoint selection rather than collapsing every block to a constant.
///
/// Pattern: R = x % 256, G = y % 256, B = (x+y) % 256, A = 255.
[[nodiscard]] std::vector<std::uint8_t>
make_gradient_rgba8(std::uint32_t w, std::uint32_t h)
{
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w) * h * 4U);
    for (std::uint32_t y = 0U; y < h; ++y)
    {
        for (std::uint32_t x = 0U; x < w; ++x)
        {
            const std::size_t off = (static_cast<std::size_t>(y) * w + x) * 4U;
            pixels[off + 0U] = static_cast<std::uint8_t>(x % 256U);
            pixels[off + 1U] = static_cast<std::uint8_t>(y % 256U);
            pixels[off + 2U] = static_cast<std::uint8_t>((x + y) % 256U);
            pixels[off + 3U] = 255U;
        }
    }
    return pixels;
}

// ---- formatting helpers ------------------------------------------------------

/// Format byte count as a human-readable string (B / KB / MB).
void print_bytes(std::uint64_t bytes)
{
    if (bytes >= 1024ULL * 1024ULL)
    {
        std::printf("%.2f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    }
    else if (bytes >= 1024ULL)
    {
        std::printf("%.2f KB", static_cast<double>(bytes) / 1024.0);
    }
    else
    {
        std::printf("%llu B", static_cast<unsigned long long>(bytes));
    }
}

// ---- per-image encode + measure ----------------------------------------------

struct ImageResult
{
    std::uint32_t width {};
    std::uint32_t height {};
    std::uint64_t original_bytes {};
    std::uint64_t output_bytes {};
    double        ratio {};
    double        rmse {};
    double        encode_time_ms {};
    bool          ok { false };
};

[[nodiscard]] ImageResult
run_encode(std::uint32_t w, std::uint32_t h)
{
    ImageResult result;
    result.width  = w;
    result.height = h;

    // Generate input.
    const std::vector<std::uint8_t> pixels = make_gradient_rgba8(w, h);
    result.original_bytes = static_cast<std::uint64_t>(pixels.size());

    // Time the encode call.
    const auto t0 = std::chrono::steady_clock::now();

    const auto compressed = cd::asset::texture_compress::encode(
        std::span<const std::uint8_t>{ pixels },
        w, h,
        cd::asset::texture_compress::EncodeOptions{
            .target        = cd::asset::texture_compress::Format::kBC1,
            .quality       = 128U,
            .generate_mips = false
        });

    const auto t1 = std::chrono::steady_clock::now();

    if (!compressed)
    {
        std::printf("  ERROR: encode() returned nullopt for %ux%u\n", w, h);
        return result;
    }

    result.encode_time_ms =
        std::chrono::duration<double, std::milli>(t1 - t0).count();

    // Analyze: compute stats (ratio + RMSE via decode-and-compare).
    const auto stats = cd::asset::texture_compress::analyze(
        std::span<const std::uint8_t>{ pixels }, *compressed);

    if (!stats)
    {
        std::printf("  ERROR: analyze() returned nullopt for %ux%u\n", w, h);
        return result;
    }

    result.output_bytes   = stats->output_bytes;
    result.ratio          = stats->ratio;
    result.rmse           = stats->rmse;
    result.ok             = true;
    return result;
}

// ---- table printing ----------------------------------------------------------

void print_header()
{
    std::printf("%-12s  %-10s  %-10s  %-8s  %-8s  %-12s\n",
                "Image",
                "Original",
                "BC1 Output",
                "Ratio",
                "RMSE",
                "EncodeTime");
    std::printf("%-12s  %-10s  %-10s  %-8s  %-8s  %-12s\n",
                "------------",
                "----------",
                "----------",
                "--------",
                "--------",
                "------------");
}

void print_row(const ImageResult& r)
{
    // Image label.
    char label[32];
    std::snprintf(label, sizeof(label), "%ux%u", r.width, r.height);

    std::printf("%-12s  ", label);

    // Original bytes.
    char orig_buf[32];
    if (r.original_bytes >= 1024ULL * 1024ULL)
    {
        std::snprintf(orig_buf, sizeof(orig_buf), "%.2f MB",
                      static_cast<double>(r.original_bytes) / (1024.0 * 1024.0));
    }
    else
    {
        std::snprintf(orig_buf, sizeof(orig_buf), "%llu KB",
                      static_cast<unsigned long long>(r.original_bytes / 1024ULL));
    }
    std::printf("%-10s  ", orig_buf);

    // BC1 output bytes.
    char out_buf[32];
    if (r.output_bytes >= 1024ULL * 1024ULL)
    {
        std::snprintf(out_buf, sizeof(out_buf), "%.2f MB",
                      static_cast<double>(r.output_bytes) / (1024.0 * 1024.0));
    }
    else
    {
        std::snprintf(out_buf, sizeof(out_buf), "%llu KB",
                      static_cast<unsigned long long>(r.output_bytes / 1024ULL));
    }
    std::printf("%-10s  ", out_buf);

    // Ratio.
    std::printf("%-8.2f  ", r.ratio);

    // RMSE.
    std::printf("%-8.3f  ", r.rmse);

    // Encode time.
    std::printf("%.3f ms\n", r.encode_time_ms);
}

void print_summary(const ImageResult& r1mb)
{
    // The 1024x1024 result demonstrates the canonical 4 MB -> 0.5 MB moment.
    std::printf("\n");
    std::printf("=== Summary ===\n");
    std::printf("\n");
    std::printf("  BC1 block size:  4x4 texels, 8 bytes/block  =>  4 bpp (vs 32 bpp RGBA8)\n");
    std::printf("  Theoretical ratio: 32 / 4 = 8:1\n");
    std::printf("\n");

    if (r1mb.ok)
    {
        std::printf("  Measured (1024x1024):\n");
        std::printf("    Original : ");
        print_bytes(r1mb.original_bytes);
        std::printf("\n");
        std::printf("    BC1 blob : ");
        print_bytes(r1mb.output_bytes);
        std::printf("\n");
        std::printf("    Ratio    : %.2f:1\n", r1mb.ratio);
        std::printf("    RMSE     : %.3f  (lower = better; BC1 Sprint-1 naive encoder)\n",
                    r1mb.rmse);
        std::printf("\n");
        std::printf("  => 4 MB raw RGBA8 texture fits in %.0f KB on the GPU.\n",
                    static_cast<double>(r1mb.output_bytes) / 1024.0);
        std::printf("     Ship a 2 GB texture budget as 256 MB — 8x more content per VRAM.\n");
    }
    else
    {
        std::printf("  (1024x1024 encode failed — see errors above)\n");
    }
}

}  // namespace

// ---- main --------------------------------------------------------------------

int main()
{
    std::printf("=== hello_texture_compress_ratio — cd::asset::texture_compress BC1 demo ===\n");
    std::printf("\n");
    std::printf("Generating 3 synthetic RGBA8 gradient images and encoding to BC1...\n");
    std::printf("\n");

    // Run the three canonical sizes.
    const ImageResult r64   = run_encode(  64U,   64U);   //  16 KB raw RGBA8
    const ImageResult r256  = run_encode( 256U,  256U);   // 256 KB raw RGBA8
    const ImageResult r1024 = run_encode(1024U, 1024U);   //   4 MB raw RGBA8

    // Print the results table.
    print_header();
    if (r64.ok)   { print_row(r64); }
    if (r256.ok)  { print_row(r256); }
    if (r1024.ok) { print_row(r1024); }

    // Print the summary for the 1024x1024 case.
    print_summary(r1024);

    std::printf("\n");
    std::printf("=== done ===\n");
    return 0;
}
