// =============================================================================
// CHROMODYNAMIC — samples/profile/hello_gpu_marker
//
// Phase 612 — console proof that cd::profile::gpu_marker is consumable as a
// standalone library.
//
// Creates a Recorder, fake-begins/ends 5 named markers via NullCommandBuffer
// (stub mode — no real GPU device required), calls resolve(NullDevice), then
// prints each GpuMarkerSample name, raw ticks, and duration_ms_computed.
// Exit 0 on success.
// =============================================================================
#include <cd/profile/gpu_marker/GpuMarker.hpp>

#include <cd/rhi/NullCommandBuffer.hpp>
#include <cd/rhi/NullDevice.hpp>

#include <cstdio>
#include <cstdlib>

int main()
{
    using namespace cd::profile::gpu_marker;

    std::printf("=== hello_gpu_marker ===\n");

    cd::rhi::NullCommandBuffer cmd;
    cd::rhi::NullDevice        dev;
    Recorder                   rec;

    // --- Record 5 named GPU marker regions via the stub command buffer ---

    const MarkerHandle h0 = rec.begin_marker(cmd, "depth_prepass");
    const MarkerHandle h1 = rec.begin_marker(cmd, "gbuffer_pass");
    const MarkerHandle h2 = rec.begin_marker(cmd, "shadow_map");
    const MarkerHandle h3 = rec.begin_marker(cmd, "lighting_pass");
    const MarkerHandle h4 = rec.begin_marker(cmd, "post_fx");

    rec.end_marker(cmd, h0);
    rec.end_marker(cmd, h1);
    rec.end_marker(cmd, h2);
    rec.end_marker(cmd, h3);
    rec.end_marker(cmd, h4);

    // Resolve raw stub ticks to duration_ms_computed (stub frequency: 1 GHz).
    rec.resolve(dev);

    const auto s = rec.samples();

    std::printf("sample count: %zu\n", s.size());
    std::printf("%-20s  %12s  %12s  %16s\n",
                "name", "start_tick", "end_tick", "duration_ms");
    std::printf("%-20s  %12s  %12s  %16s\n",
                "--------------------", "----------", "--------", "---------------");

    for (const auto& sample : s)
    {
        std::printf("%-20s  %12llu  %12llu  %16.9f\n",
                    sample.name.c_str(),
                    static_cast<unsigned long long>(sample.gpu_start_tick),
                    static_cast<unsigned long long>(sample.gpu_end_tick),
                    sample.duration_ms_computed);
    }

    // --- Sanity checks ---

    if (s.size() != 5U)
    {
        std::fprintf(stderr,
                     "[hello_gpu_marker] FAIL: expected 5 samples, got %zu\n",
                     s.size());
        return EXIT_FAILURE;
    }

    for (const auto& sample : s)
    {
        if (sample.gpu_end_tick <= sample.gpu_start_tick)
        {
            std::fprintf(stderr,
                         "[hello_gpu_marker] FAIL: sample '%s' end_tick (%llu) "
                         "<= start_tick (%llu)\n",
                         sample.name.c_str(),
                         static_cast<unsigned long long>(sample.gpu_end_tick),
                         static_cast<unsigned long long>(sample.gpu_start_tick));
            return EXIT_FAILURE;
        }

        if (sample.duration_ms_computed <= 0.0)
        {
            std::fprintf(stderr,
                         "[hello_gpu_marker] FAIL: sample '%s' has non-positive "
                         "duration_ms_computed (%.9f)\n",
                         sample.name.c_str(),
                         sample.duration_ms_computed);
            return EXIT_FAILURE;
        }
    }

    std::printf("[hello_gpu_marker] OK\n");
    return EXIT_SUCCESS;
}
