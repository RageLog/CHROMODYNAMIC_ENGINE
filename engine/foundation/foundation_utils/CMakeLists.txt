# engine/foundation/foundation_utils — cd::foundation_utils (Phase 234)
#
# Umbrella aggregating the small foundation libs that each ship 1-6
# headers. Bigger libs (core, math, concurrency, io, log, platform,
# time, vfs) stay independent — they're substantial enough to warrant
# direct deps.
#
# Member libs (1-6 headers each):
#   cd::bench    Benchmark.hpp
#   cd::config   Config.hpp
#   cd::diag     Assert.hpp + CrashReporter.hpp + DeadlineMonitor.hpp
#   cd::events   EventBus.hpp + EventRecorder.hpp + ScopedConnection.hpp
#   cd::plugin   FileWatcher.hpp + HotReload.hpp + IPlugin.hpp + Loader.hpp
#   cd::profile  BufferSink.hpp + ChromeTraceSink.hpp + CsvSink.hpp + Scope.hpp + StatsAggregator.hpp
#   cd::mem      IAllocator.hpp + LinearAllocator.hpp + PageAllocator.hpp + PmrAdapter.hpp + PoolAllocator.hpp + TrackingAllocator.hpp
cd_add_library(foundation_utils
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::bench
    cd::config
    cd::diag
    cd::events
    cd::plugin
    cd::profile
    cd::mem
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
