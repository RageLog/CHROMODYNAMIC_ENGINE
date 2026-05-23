// =============================================================================
// examples/external_app/src/main.cpp
//
// Downstream-consumer entrypoint. Builds an asset-inventory demo via
// cd::ecs, streams pseudo-events through cd::log, serializes to JSON
// via cd::asset_json, and exercises cd::imgdiff round-trip — all
// linked from a `find_package(CHROMODYNAMIC)` install tree.
//
// Exit 0 means the entire downstream link path works end-to-end.
// =============================================================================
#include "Inventory.hpp"

#include <cd/asset_image/Image.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/ErrorFormat.hpp>
#include <cd/core/Version.hpp>
#include <cd/diag/Assert.hpp>
#include <cd/imgdiff/ImageDiff.hpp>
#include <cd/log/RingBufferSink.hpp>

#include <array>
#include <cstdio>
#include <filesystem>
#include <string>

int main()
{
    // ---- 1. Engine version banner -----------------------------------------
    std::fprintf(stdout,
                 "linking CHROMODYNAMIC %u.%u.%u (engine name: %.*s)\n",
                 static_cast<unsigned>(cd::core::kEngineVersion.major),
                 static_cast<unsigned>(cd::core::kEngineVersion.minor),
                 static_cast<unsigned>(cd::core::kEngineVersion.patch),
                 static_cast<int>(cd::core::kEngineName.size()),
                 cd::core::kEngineName.data());

    // ---- 2. Build the demo inventory --------------------------------------
    my_game::InventoryDemo demo;
    const auto populated = my_game::populate_demo_inventory(demo);
    std::fprintf(stdout, "populated %zu inventory entries\n", populated);
    CD_VERIFY(demo.alive_count() == populated);

    // ---- 3. Stream 200 pseudo-events through cd::log ----------------------
    cd::log::RingBufferSink ring { 32 };  // bounded; older messages drop
    for (int i = 0; i < 200; ++i)
    {
        cd::log::LogRecord r;
        r.message = "tick " + std::to_string(i);
        ring.on_log_record(r);
    }
    const auto snap = ring.snapshot();
    std::fprintf(stdout,
                 "log ring: %zu records snapshot, wrapped=%s\n",
                 snap.size(), ring.wrapped() ? "yes" : "no");
    CD_VERIFY(snap.size() == 32);  // bounded ring kept the last 32
    CD_VERIFY(snap.back().message == "tick 199");  // last write is at the tail

    // ---- 4. Format an ErrorCode ------------------------------------------
    const auto missing = cd::core::core_errors::make(
        cd::core::core_errors::Code::kNotFound, "item ID 9999");
    const auto formatted = cd::core::format(missing);
    std::fprintf(stdout, "example error: %s\n", formatted.c_str());
    CD_VERIFY(formatted == "core::NotFound: item ID 9999");

    // ---- 5. Serialize the inventory --------------------------------------
    const auto json = demo.serialize_to_json();
    std::fprintf(stdout, "serialized %zu bytes of JSON\n", json.size());
    CD_VERIFY(json.find("Steel Sword") != std::string::npos);
    CD_VERIFY(json.find("inventory") != std::string::npos);

    // ---- 6. cd::imgdiff round-trip through cd::asset_image ----------------
    const std::array<std::uint8_t, 16> pixels {
        255, 0,   0,   255,
        0,   255, 0,   255,
        0,   0,   255, 255,
        255, 255, 255, 255,
    };
    const auto tmp = std::filesystem::temp_directory_path() / "cd_external_app_demo.png";
    if (auto w = cd::asset_image::write_png_rgba(tmp.string(), pixels.data(), 2, 2); !w.has_value())
    {
        std::fprintf(stderr, "write_png_rgba failed: %.*s\n",
                     static_cast<int>(w.error().message.size()),
                     w.error().message.data());
        return 1;
    }
    auto loaded = cd::asset_image::load_image(tmp.string());
    if (!loaded.has_value())
    {
        std::fprintf(stderr, "load_image failed\n");
        return 1;
    }
    const cd::imgdiff::ImageView a { pixels.data(), 2, 2 };
    const cd::imgdiff::ImageView b { loaded->rgba.data(), 2, 2 };
    auto cmp = cd::imgdiff::compare(a, b, /*tolerance=*/0);
    if (!cmp.has_value() || cmp->different_pixels != 0)
    {
        std::fprintf(stderr, "imgdiff compare mismatch\n");
        return 1;
    }
    std::error_code ec;
    std::filesystem::remove(tmp, ec);

    std::fprintf(stdout, "external app demo OK\n");
    return 0;
}
