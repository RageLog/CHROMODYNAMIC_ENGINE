// =============================================================================
// examples/consuming/main.cpp
//
// Downstream-consumer smoke. Links cd::core / cd::diag / cd::log /
// cd::imgdiff / cd::asset_image (all from the installed prefix) and
// exercises one symbol from each in a single 60-line main(). Exit 0
// means find_package(CHROMODYNAMIC) is producing a usable install tree.
//
// See docs/CONSUMING.md for the longer walkthrough.
// =============================================================================
#include <cd/core/ErrorCode.hpp>
#include <cd/core/ErrorFormat.hpp>
#include <cd/core/Version.hpp>
#include <cd/diag/Assert.hpp>
#include <cd/log/RingBufferSink.hpp>
#include <cd/asset_image/Image.hpp>
#include <cd/imgdiff/ImageDiff.hpp>

#include <array>
#include <cstdio>
#include <filesystem>

int main()
{
    // --- 1. cd::core --------------------------------------------------------
    std::fprintf(stdout, "CHROMODYNAMIC %u.%u.%u (engine name: %.*s)\n",
                 static_cast<unsigned>(cd::core::kEngineVersion.major),
                 static_cast<unsigned>(cd::core::kEngineVersion.minor),
                 static_cast<unsigned>(cd::core::kEngineVersion.patch),
                 static_cast<int>(cd::core::kEngineName.size()),
                 cd::core::kEngineName.data());

    const auto ec = cd::core::core_errors::make(
        cd::core::core_errors::Code::kInvalidArgument, "downstream smoke arg");
    const auto pretty = cd::core::format(ec);
    if (pretty != "core::InvalidArgument: downstream smoke arg")
    {
        std::fprintf(stderr, "format() returned unexpected: %s\n", pretty.c_str());
        return 1;
    }

    // --- 2. cd::diag --------------------------------------------------------
    CD_VERIFY(1 + 1 == 2);

    // --- 3. cd::log ---------------------------------------------------------
    cd::log::RingBufferSink sink { 4 };
    for (int i = 0; i < 3; ++i)
    {
        cd::log::LogRecord r;
        r.message = "downstream alive frame " + std::to_string(i);
        sink.on_log_record(r);
    }
    const auto snap = sink.snapshot();
    if (snap.size() != 3)
    {
        std::fprintf(stderr, "ring snapshot wrong size: %zu\n", snap.size());
        return 1;
    }

    // --- 4. cd::asset_image + cd::imgdiff ------------------------------------
    // Write a tiny 2x2 PNG, read it back, diff against itself. Catches
    // PNG codec drift and the imgdiff dimensions/tolerance plumbing.
    const std::array<std::uint8_t, 16> pixels {
        255, 0,   0,   255,
        0,   255, 0,   255,
        0,   0,   255, 255,
        255, 255, 255, 255,
    };
    const auto tmp = std::filesystem::temp_directory_path() / "cd_consuming_smoke.png";
    if (auto w = cd::asset_image::write_png_rgba(tmp.string(), pixels.data(), 2, 2); !w.has_value())
    {
        std::fprintf(stderr, "write_png_rgba failed: %.*s\n",
                     static_cast<int>(w.error().message.size()),
                     w.error().message.data());
        return 1;
    }
    auto loaded = cd::asset_image::load_image(tmp.string());
    if (!loaded.has_value() || loaded->width != 2 || loaded->height != 2)
    {
        std::fprintf(stderr, "load_image after write failed\n");
        return 1;
    }
    const cd::imgdiff::ImageView a { pixels.data(), 2, 2 };
    const cd::imgdiff::ImageView b { loaded->rgba.data(), 2, 2 };
    auto cmp = cd::imgdiff::compare(a, b, /*tolerance=*/0);
    if (!cmp.has_value() || cmp->different_pixels != 0)
    {
        std::fprintf(stderr, "imgdiff compare failed: diff=%u\n",
                     cmp.has_value() ? cmp->different_pixels : 0);
        return 1;
    }
    std::error_code ec_;
    std::filesystem::remove(tmp, ec_);

    std::fprintf(stdout, "downstream consumer smoke OK\n");
    return 0;
}
