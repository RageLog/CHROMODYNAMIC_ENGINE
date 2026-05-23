// =============================================================================
// CHROMODYNAMIC — samples/hello_flip
//
// Demonstrates cd::imgdiff::compute_flip_lite (Wave 68) on a pair of
// synthetic test images: a radial-gradient baseline and a "regressed"
// variant with a 1-pixel diagonal stripe burned in. Prints the FLIP
// aggregate scores + a green-yellow-red ASCII art slice of the error
// map so the demo is visible without external tooling.
// =============================================================================
#include <cd/imgdiff/Flip.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace
{

[[nodiscard]] std::vector<std::uint8_t> make_baseline(std::uint32_t w, std::uint32_t h)
{
    std::vector<std::uint8_t> img(static_cast<std::size_t>(w) * h * 4U);
    const double cx = static_cast<double>(w) * 0.5;
    const double cy = static_cast<double>(h) * 0.5;
    const double max_r = std::sqrt(cx * cx + cy * cy);
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x)
        {
            const double dx = static_cast<double>(x) - cx;
            const double dy = static_cast<double>(y) - cy;
            const double r = std::sqrt(dx * dx + dy * dy);
            const auto t = static_cast<std::uint8_t>(255.0 * (1.0 - r / max_r));
            const std::size_t k = (y * w + x) * 4U;
            img[k + 0] = t;
            img[k + 1] = t;
            img[k + 2] = t;
            img[k + 3] = 255;
        }
    return img;
}

void scribble_diagonal(std::vector<std::uint8_t>& img, std::uint32_t w, std::uint32_t h)
{
    // Draw a bright 1-pixel diagonal stripe through the image.
    for (std::uint32_t i = 0; i < std::min(w, h); ++i)
    {
        const std::size_t k = (i * w + i) * 4U;
        img[k + 0] = 255;
        img[k + 1] = 0;
        img[k + 2] = 0;
        img[k + 3] = 255;
    }
}

void print_ascii_slice(const std::vector<double>& err, std::uint32_t w, std::uint32_t h)
{
    // Sample a centre horizontal slice through the error map and
    // render with ' .:-=+*#%@' glyphs scaled by error magnitude.
    constexpr const char* kGlyphs = " .:-=+*#%@";
    const std::uint32_t row = h / 2;
    std::printf("  row %u: ", row);
    for (std::uint32_t x = 0; x < w; ++x)
    {
        const double e = err[static_cast<std::size_t>(row) * w + x];
        const auto idx = static_cast<std::size_t>(
            std::min(9.0, std::max(0.0, e * 10.0)));
        std::printf("%c", kGlyphs[idx]);
    }
    std::printf("\n");
}

}  // namespace

int main()
{
    std::printf("=== hello_flip — FLIP-lite perceptual diff demo ===\n");

    constexpr std::uint32_t W = 64;
    constexpr std::uint32_t H = 32;

    const auto baseline = make_baseline(W, H);
    auto candidate = baseline;
    scribble_diagonal(candidate, W, H);

    const cd::imgdiff::ImageView va { baseline.data(), W, H };
    const cd::imgdiff::ImageView vb { candidate.data(), W, H };

    auto r = cd::imgdiff::compute_flip_lite(va, vb, /*ppd=*/67.0);
    if (!r.has_value())
    {
        std::printf("[hello_flip] compute_flip_lite failed: %u\n", r.error().code);
        return 1;
    }

    std::printf("\n=== FLIP-lite Report ===\n");
    std::printf("  pixel_count = %u\n", r->pixel_count);
    std::printf("  mean_error  = %.4f\n", r->mean_error);
    std::printf("  p95_error   = %.4f\n", r->p95_error);
    std::printf("  max_error   = %.4f\n", r->max_error);
    std::printf("  flip_passes(0.05) = %s\n",
                cd::imgdiff::flip_passes(*r, 0.05) ? "true" : "false");
    std::printf("\n=== Error map (centre row, ' '=zero → '@'=max) ===\n");
    print_ascii_slice(r->error_map, W, H);

    std::printf("[hello_flip] done\n");
    return 0;
}
