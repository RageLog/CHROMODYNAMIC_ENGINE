// =============================================================================
// HelloGoldenCli.hpp
// -----------------------------------------------------------------------------
// phase825-rt-chrome-sponza-golden-cli-extract: parse the
// `--golden-fixture` / `--golden-out` / `--golden-frames` flag triplet
// into a pure POD `CliOptions`. Extracted from main.cpp so the parser
// is unit-testable without bringing up Vulkan + the App lifecycle.
//
// Two surfaces:
//   * Pure  — `parse_args(argc, argv)` returns a fresh `CliOptions`,
//             zero side effects. Tests use this.
//   * App   — `parse(argc, argv)` writes into a singleton, the rest of
//             main.cpp reads it via `options()` / `enabled()`. Identical
//             behaviour to the pre-extract main.cpp inline path.
// =============================================================================
#pragma once

#include "SponzaFixtures.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

namespace cd::hello_engine::golden
{

struct CliOptions
{
    int           fixture_index    { -1 };  ///< -1 = `--golden-fixture` was not parsed
    std::string   out_png_path     {};      ///< empty = `--golden-out` was not parsed
    std::uint32_t capture_at_frame { 2 };   ///< 0-based; `--golden-frames N` lands on frame N-1
    bool          fixture_seen     { false };
};

// ---------------------------------------------------------------------------
// parse_args — pure version.
//
// Unknown flags are silently ignored so other CLI surfaces (asset-path
// overrides, future render-quality knobs) keep working. A non-numeric
// or out-of-range value for `--golden-fixture` / `--golden-frames`
// leaves the corresponding field at its default — the caller can spot
// the failure via `fixture_seen == false`.
// ---------------------------------------------------------------------------
[[nodiscard]] inline CliOptions parse_args(int argc, char** argv) noexcept
{
    CliOptions out {};
    if (argv == nullptr) return out;
    // NOLINTNEXTLINE(google-runtime-int) — strtol returns long by C ABI.
    const auto parse_int = [](const char* arg) -> std::optional<long> {
        if (arg == nullptr) return std::nullopt;
        char* end_ptr  = nullptr;
        errno          = 0;
        const long val = std::strtol(arg, &end_ptr, 10);  // NOLINT(google-runtime-int)
        if (end_ptr == arg || *end_ptr != '\0' || errno != 0)
            return std::nullopt;
        return val;
    };
    for (int i = 1; i < argc; ++i)
    {
        if (argv[i] == nullptr) continue;
        const std::string_view a { argv[i] };
        if (a == "--golden-fixture" && i + 1 < argc && argv[i + 1] != nullptr)
        {
            if (const auto n = parse_int(argv[i + 1]); n.has_value() &&
                *n >= 0 && static_cast<std::size_t>(*n)
                           < cd::hello_engine::sponza_fixtures::kFixtureCount)
            {
                out.fixture_index = static_cast<int>(*n);
                out.fixture_seen  = true;
            }
            ++i;
        }
        else if (a == "--golden-out" && i + 1 < argc && argv[i + 1] != nullptr)
        {
            out.out_png_path = argv[i + 1];
            ++i;
        }
        else if (a == "--golden-frames" && i + 1 < argc && argv[i + 1] != nullptr)
        {
            if (const auto n = parse_int(argv[i + 1]); n.has_value() && *n >= 1)
                out.capture_at_frame = static_cast<std::uint32_t>(*n - 1);
            ++i;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Application-side singleton wrappers. Keep behaviour byte-identical to
// the pre-extract main.cpp inline path.
// ---------------------------------------------------------------------------
inline CliOptions& options() noexcept
{
    static CliOptions o {};
    return o;
}

inline bool parse(int argc, char** argv) noexcept
{
    options() = parse_args(argc, argv);
    return options().fixture_seen;
}

[[nodiscard]] inline bool enabled() noexcept
{
    return options().fixture_index >= 0;
}

}  // namespace cd::hello_engine::golden
