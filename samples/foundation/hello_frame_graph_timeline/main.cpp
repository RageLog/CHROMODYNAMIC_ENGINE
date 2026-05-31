// =============================================================================
// CHROMODYNAMIC — samples/foundation/hello_frame_graph_timeline
//
// Phase 604 — console proof that cd::profile::frame_graph_timeline is
// consumable as a standalone library.
//
// Creates a Timeline, records 5 synthetic render-pass timings, calls
// end_frame(), then prints last_frame_total_ms and each PassRecord.
// Exit 0 on success.
// =============================================================================
#include <cd/profile/frame_graph_timeline/FrameGraphTimeline.hpp>

#include <cstdio>
#include <cstdlib>

int main()
{
    using namespace cd::profile::frame_graph_timeline;

    std::printf("=== hello_frame_graph_timeline ===\n");

    Timeline tl;

    tl.begin_frame();
    tl.record_pass("ShadowMap",    0.00,  1.20, 1U);
    tl.record_pass("GBuffer",      1.20,  2.50, 2U);
    tl.record_pass("Lighting",     3.70,  3.10, 3U);
    tl.record_pass("Bloom",        6.80,  0.80, 4U);
    tl.record_pass("Tonemap+TAA",  7.60,  0.40, 5U);
    tl.end_frame();

    const double total = tl.last_frame_total_ms();
    const auto   passes = tl.last_frame_passes();

    std::printf("last_frame_total_ms : %.4f ms\n", total);
    std::printf("pass count          : %zu\n", passes.size());
    std::printf("%-20s  %8s  %10s  %s\n",
                "pass_name", "start_ms", "duration_ms", "node_id");
    std::printf("%-20s  %8s  %10s  %s\n",
                "--------------------", "--------", "----------", "-------");

    for (const auto& p : passes)
    {
        std::printf("%-20s  %8.4f  %10.4f  %u\n",
                    p.pass_name.c_str(),
                    p.gpu_start_ms,
                    p.gpu_duration_ms,
                    p.graph_node_id);
    }

    // Sanity check: 5 passes recorded.
    if (passes.size() != 5U)
    {
        std::fprintf(stderr, "[hello_frame_graph_timeline] FAIL: expected 5 passes, got %zu\n",
                     passes.size());
        return EXIT_FAILURE;
    }

    // Sanity check: total = sum of all gpu_duration_ms
    // 1.20 + 2.50 + 3.10 + 0.80 + 0.40 = 8.00
    constexpr double kExpectedTotal = 8.00;
    constexpr double kTolerance     = 1e-9;
    const double     diff           = total - kExpectedTotal;
    const double     absDiff        = diff < 0.0 ? -diff : diff;
    if (absDiff > kTolerance)
    {
        std::fprintf(stderr,
                     "[hello_frame_graph_timeline] FAIL: total_ms=%.6f expected=%.6f\n",
                     total, kExpectedTotal);
        return EXIT_FAILURE;
    }

    std::printf("[hello_frame_graph_timeline] OK\n");
    return EXIT_SUCCESS;
}
