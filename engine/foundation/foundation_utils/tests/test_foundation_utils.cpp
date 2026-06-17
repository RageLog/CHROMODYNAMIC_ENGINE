// cd::foundation_utils umbrella — reachability smoke. The lib has no own
// logic (it is a pure aggregator header, sealed 100%-by-definition in
// docs/ADR/ADR-20260616-band6-render-misc-scope.md §5); "completeness" =
// the umbrella header pulls in + re-exports the right siblings. This test
// proves that by naming one symbol from EVERY re-exported member lib so a
// dropped #include in the umbrella fails to compile here (fail-on-revert).
#include <cd/foundation_utils/foundation_utils.hpp>

#include <gtest/gtest.h>

namespace
{

// One reachable symbol per member lib, named through pointer-typedef
// references so the check is constructibility-agnostic (no instantiation, no
// always-true sizeof comparison). If the umbrella ever drops a member
// #include, the corresponding alias stops naming a complete type and the TU
// fails to compile — turning a silent aggregation gap into a build break
// rather than a runtime surprise.
TEST(FoundationUtils, EveryMemberLibSymbolIsReachable)
{
    using BenchT  = cd::bench::Config;             // bench
    using DiagT   = cd::diag::DeadlineMonitor;     // diag
    using EventsT = cd::events::EventBus;          // events
    using FrameT  = cd::frame_timing::FrameTimeRing<>;  // frame_timing
    using PluginT = cd::plugin::IFileWatcher;      // plugin
    using ProfT   = cd::profile::BufferSink;       // profile
    using MemT    = cd::mem::TrackingAllocator;    // mem

    [[maybe_unused]] BenchT*  bench_p  = nullptr;
    [[maybe_unused]] DiagT*   diag_p   = nullptr;
    [[maybe_unused]] EventsT* events_p = nullptr;
    [[maybe_unused]] FrameT*  frame_p  = nullptr;
    [[maybe_unused]] PluginT* plugin_p = nullptr;
    [[maybe_unused]] ProfT*   prof_p   = nullptr;
    [[maybe_unused]] MemT*    mem_p    = nullptr;

    // config — CVar serialization format constant + entry point (free fn).
    EXPECT_EQ(cd::config::kMagic, 0x43564152u);
    EXPECT_NE(&cd::config::save, nullptr);

    SUCCEED();
}

}  // namespace
