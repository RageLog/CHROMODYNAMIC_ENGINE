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
//   --golden-capture <path>
//                    On the last headless frame, capture the swapchain
//                    image and write it to <path> as PNG. Phase 11
//                    Track A: produces the golden reference. Implies
//                    --headless (default 3) and --no-spin.
//   --golden-compare <path>
//                    On the last headless frame, capture the swapchain
//                    and diff against the PNG at <path> via cd::imgdiff.
//                    Process exit code is non-zero on mismatch above the
//                    tolerance. Implies --headless and --no-spin.
//   --golden-tolerance N
//                    Per-channel absolute tolerance for --golden-compare.
//                    Default 8 (out of 255) — wide enough to absorb
//                    cross-driver / cross-OS quantization noise, tight
//                    enough to catch the v1.0-rollback bug class.
//   --golden-diff-out <path>
//                    On compare failure, write a highlight PNG (matches
//                    darkened, mismatches painted red) to <path>. Useful
//                    as a CI artifact.
//
// Anything else is left for the sample to consume (e.g. hello_gltf reads
// argv[1] as the .glb path).
// =============================================================================
#pragma once

#include <cstdint>
#include <cstdlib>  // std::atoi — MinGW libc declares only in ::; force pull from std.
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
    /// If true, samples should freeze any time-driven animation so output
    /// is byte-identical across runs.
    bool no_spin { false };
    /// Golden-screenshot mode (Phase 11 Track A).
    GoldenMode golden_mode { GoldenMode::kNone };
    /// Filesystem path the golden helper reads or writes (PNG).
    std::string golden_path {};
    /// Per-channel absolute tolerance for compare. Default 8.
    std::uint8_t golden_tolerance { 8 };
    /// Optional path for highlight diff PNG on compare mismatch.
    std::string golden_diff_out {};

    /// True for any frame the sample should keep rendering. The sample
    /// passes its own incrementing frame counter; this method is the
    /// single point at which `--headless` exits the loop.
    [[nodiscard]] bool should_continue(std::uint32_t frame_index) const noexcept
    {
        if (headless_frames == 0)
            return true;
        return frame_index < headless_frames;
    }

    /// True iff the caller should run the golden-capture helper at exit.
    [[nodiscard]] bool wants_golden() const noexcept
    {
        return golden_mode != GoldenMode::kNone && !golden_path.empty();
    }

    /// True iff the current frame is the last one before exit — the
    /// frame the golden helper should capture from. Caller passes its
    /// own monotonically-incrementing frame_index.
    [[nodiscard]] bool is_golden_frame(std::uint32_t frame_index) const noexcept
    {
        return wants_golden() && headless_frames > 0 && frame_index + 1 == headless_frames;
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
        else if ((arg == "--golden-capture" || arg == "--golden-compare") && i + 1 < argc)
        {
            rt.golden_mode = (arg == "--golden-capture") ? GoldenMode::kCapture : GoldenMode::kCompare;
            rt.golden_path = argv[i + 1];
            ++i;
            // Imply --headless 3 + --no-spin so capture is deterministic.
            if (rt.headless_frames == 0)
                rt.headless_frames = 3;
            rt.no_spin = true;
        }
        else if (arg == "--golden-tolerance" && i + 1 < argc)
        {
            // Range-clamp into [0, 255]. atoi returns int; out-of-range
            // values folded to the endpoints rather than wrapping.
            const int v = ::atoi(argv[i + 1]);
            const int clamped = v < 0 ? 0 : (v > 255 ? 255 : v);
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
