// =============================================================================
// CHROMODYNAMIC — samples/foundation/hello_cpu_marker
//
// Phase 603 — console proof that cd::profile::cpu_marker_overlay is
// consumable as a standalone library.
//
// Creates a Collector, runs 4 timed scopes (two nested inside an outer),
// calls samples_since(0.0), then prints each sample name + duration_ms.
// Exit 0 on success.
// =============================================================================
#include <cd/profile/cpu_marker_overlay/CpuMarkerOverlay.hpp>

#include <cstdio>
#include <cstdlib>

int main()
{
    using namespace cd::profile::cpu_marker_overlay;

    std::printf("=== hello_cpu_marker ===\n");

    Collector col;

    // Scope A — top-level outer region.
    {
        Scope outer(col, "frame_outer");

        // Scope B — first nested region.
        {
            Scope update(col, "update_systems");
        }

        // Scope C — second nested region.
        {
            Scope render(col, "render_commands");
        }
    }

    // Scope D — peer-level region after the outer block.
    {
        Scope present(col, "present_swap");
    }

    const auto samples = col.samples_since(0.0);

    std::printf("sample count: %zu\n", samples.size());
    std::printf("%-24s  %12s\n", "name", "duration_ms");
    std::printf("%-24s  %12s\n", "------------------------", "------------");

    for (const auto& s : samples)
    {
        std::printf("%-24s  %12.6f\n", s.name.c_str(), s.duration_ms);
    }

    // Sanity: 4 named scopes must have been committed.
    if (samples.size() != 4U)
    {
        std::fprintf(stderr,
                     "[hello_cpu_marker] FAIL: expected 4 samples, got %zu\n",
                     samples.size());
        return EXIT_FAILURE;
    }

    // Sanity: every duration must be non-negative.
    for (const auto& s : samples)
    {
        if (s.duration_ms < 0.0)
        {
            std::fprintf(stderr,
                         "[hello_cpu_marker] FAIL: sample '%s' has negative duration %.6f\n",
                         s.name.c_str(), s.duration_ms);
            return EXIT_FAILURE;
        }
    }

    std::printf("[hello_cpu_marker] OK\n");
    return EXIT_SUCCESS;
}
