// =============================================================================
// CHROMODYNAMIC — samples/common/SampleRuntime.hpp
//
// Tiny header-only helpers that every demo opts into so CI can smoke-test
// the GPU pipeline without a person clicking the close button.
//
// Why a header-only sample-side helper instead of cd::runtime: cd::runtime
// is a real engine library with subsystem ownership. The sample loop is
// hand-rolled, so this is the right tier — the helpers exist purely to
// keep argument-handling boilerplate out of main() and consistent across
// demos that test the full Renderer / framegraph stack.
//
// Supported flags (case-sensitive, must be 2nd argv slot or later):
//   --headless [N]   Run for N frames (default 3) then return false from
//                    should_continue() so the loop exits cleanly. CI sets
//                    this in the smoke-test job; humans never pass it.
//   --no-spin        Disable any time-based animation (orbit cameras,
//                    rotating cubes). Useful for deterministic golden-
//                    image testing.
//
// Anything else is left for the sample to consume (e.g. hello_gltf reads
// argv[1] as the .glb path).
// =============================================================================
#pragma once

#include <cstdint>
#include <cstdlib>  // std::atoi — MinGW libc declares only in ::; force pull from std.
#include <cstring>
#include <string_view>

namespace cd::sample
{

struct Runtime
{
    /// If > 0, the loop is capped at this many frames.
    std::uint32_t headless_frames { 0 };
    /// If true, samples should freeze any time-driven animation so output
    /// is byte-identical across runs.
    bool no_spin { false };

    /// True for any frame the sample should keep rendering. The sample
    /// passes its own incrementing frame counter; this method is the
    /// single point at which `--headless` exits the loop.
    [[nodiscard]] bool should_continue(std::uint32_t frame_index) const noexcept
    {
        if (headless_frames == 0)
            return true;
        return frame_index < headless_frames;
    }
};

/// Parse the common runtime flags out of `argv`. Unknown options are
/// silently ignored — each sample may consume its own argv entries
/// (e.g. file paths) independently. Returns the parsed Runtime by value.
[[nodiscard]] inline Runtime parse_runtime(int argc, char** argv) noexcept
{
    Runtime rt {};
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg { argv[i] };
        if (arg == "--headless")
        {
            // Optional positive integer follows; default 3 frames is enough
            // for a swapchain to acquire/release once per frame-in-flight
            // (we use 2) plus one extra round-trip.
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
                    // Use ::atoi (always declared by <cstdlib>) instead of
                    // std::atoi which MinGW libc omits from namespace std.
                    rt.headless_frames = static_cast<std::uint32_t>(::atoi(next));
                    if (rt.headless_frames == 0)
                        rt.headless_frames = 1;
                    ++i;  // consume the count
                }
            }
        }
        else if (arg == "--no-spin")
        {
            rt.no_spin = true;
        }
    }
    return rt;
}

}  // namespace cd::sample
