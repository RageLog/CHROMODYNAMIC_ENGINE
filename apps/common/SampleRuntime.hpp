// =============================================================================
// CHROMODYNAMIC — apps/common/SampleRuntime.hpp
//
// Tiny header-only helpers for headless CI smoke-testing and golden-image
// capture/compare across CHROMODYNAMIC shipping apps and showcase runners.
// =============================================================================
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>  // ::strtol — MinGW libc declares only in ::, not std::.
#include <cstring>
#include <string>
#include <string_view>

namespace cd::sample
{

enum class GoldenMode : std::uint8_t
{
    kNone = 0,
    kCapture = 1,
    kCompare = 2,
};

struct Runtime
{
    /// If > 0, the loop is capped at this many frames.
    std::uint32_t headless_frames { 0 };
    /// If true, freeze time-driven animation so output is byte-identical.
    bool no_spin { false };
    /// Golden-screenshot mode.
    GoldenMode golden_mode { GoldenMode::kNone };
    /// Filesystem path the golden helper reads or writes (PNG).
    std::string golden_path {};
    /// Per-channel absolute tolerance for compare. Default 8.
    std::uint8_t golden_tolerance { 8 };
    /// Optional path for highlight diff PNG on compare mismatch.
    std::string golden_diff_out {};

    [[nodiscard]] bool should_continue(std::uint32_t frame_index) const noexcept
    {
        if (headless_frames == 0)
            return true;
        return frame_index < headless_frames;
    }

    [[nodiscard]] bool wants_golden() const noexcept
    {
        return golden_mode != GoldenMode::kNone && !golden_path.empty();
    }

    [[nodiscard]] bool is_golden_frame(std::uint32_t frame_index) const noexcept
    {
        return wants_golden() && headless_frames > 0 && frame_index + 1 == headless_frames;
    }
};

[[nodiscard]] inline Runtime parse_runtime(int argc, char** argv) noexcept
{
    Runtime rt {};
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg { argv[i] };
        if (arg == "--headless")
        {
            rt.headless_frames = 3;
            if (i + 1 < argc)
            {
                const auto* next = argv[i + 1];
                bool numeric = true;
                for (std::size_t k = 0; next[k] != '\0'; ++k)
                {
                    if (next[k] < '0' || next[k] > '9')
                    {
                        numeric = false;
                        break;
                    }
                }
                if (numeric && next[0] != '\0')
                {
                    rt.headless_frames = static_cast<std::uint32_t>(
                        ::strtol(next, nullptr, 10));
                    if (rt.headless_frames == 0)
                        rt.headless_frames = 1;
                    ++i;
                }
            }
        }
        else if (arg == "--no-spin")
        {
            rt.no_spin = true;
        }
        else if ((arg == "--golden-capture" || arg == "--golden-compare") && i + 1 < argc)
        {
            rt.golden_mode = (arg == "--golden-capture") ? GoldenMode::kCapture : GoldenMode::kCompare;
            rt.golden_path = argv[i + 1];
            ++i;
            if (rt.headless_frames == 0)
                rt.headless_frames = 3;
            rt.no_spin = true;
        }
        else if (arg == "--golden-tolerance" && i + 1 < argc)
        {
            const int v = static_cast<int>(::strtol(argv[i + 1], nullptr, 10));
            const int clamped = std::clamp(v, 0, 255);
            rt.golden_tolerance = static_cast<std::uint8_t>(clamped);
            ++i;
        }
        else if (arg == "--golden-diff-out" && i + 1 < argc)
        {
            rt.golden_diff_out = argv[i + 1];
            ++i;
        }
    }
    return rt;
}

}  // namespace cd::sample
