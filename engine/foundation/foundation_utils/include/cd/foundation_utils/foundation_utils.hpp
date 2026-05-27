// =============================================================================
// CHROMODYNAMIC — cd/foundation_utils/foundation_utils.hpp
// Phase 234 umbrella aggregating the small foundation libs.
// =============================================================================
#pragma once

// bench
#include <cd/bench/Benchmark.hpp>
// config
#include <cd/config/Config.hpp>
// diag
#include <cd/diag/Assert.hpp>
#include <cd/diag/CrashReporter.hpp>
#include <cd/diag/DeadlineMonitor.hpp>
// events
#include <cd/events/EventBus.hpp>
#include <cd/events/EventRecorder.hpp>
#include <cd/events/ScopedConnection.hpp>
// frame_timing (Phase 219)
#include <cd/frame_timing/FrameTimeRing.hpp>
// plugin
#include <cd/plugin/FileWatcher.hpp>
#include <cd/plugin/HotReload.hpp>
#include <cd/plugin/IPlugin.hpp>
#include <cd/plugin/Loader.hpp>
// profile
#include <cd/profile/BufferSink.hpp>
#include <cd/profile/ChromeTraceSink.hpp>
#include <cd/profile/CsvSink.hpp>
#include <cd/profile/Scope.hpp>
#include <cd/profile/StatsAggregator.hpp>
// mem
#include <cd/mem/IAllocator.hpp>
#include <cd/mem/LinearAllocator.hpp>
#include <cd/mem/PageAllocator.hpp>
#include <cd/mem/PmrAdapter.hpp>
#include <cd/mem/PoolAllocator.hpp>
#include <cd/mem/TrackingAllocator.hpp>
