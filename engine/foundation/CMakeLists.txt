# engine/foundation — Phase 2 Sprint S2.0+
add_subdirectory(core)
add_subdirectory(mem)
add_subdirectory(concurrency)
add_subdirectory(time)
add_subdirectory(platform)
add_subdirectory(diag)
add_subdirectory(log)
add_subdirectory(events)
add_subdirectory(plugin)
add_subdirectory(io)
add_subdirectory(math)
add_subdirectory(config)
add_subdirectory(vfs)
add_subdirectory(profile)
add_subdirectory(bench)
add_subdirectory(frame_timing)       # Phase 218 — header-only dt ring + stats
add_subdirectory(foundation_utils)   # Phase 234 — umbrella over 7 small foundation libs
# Sprint S2.9+ adds: schema (still ahead)
add_subdirectory(profile_cpu_marker_overlay)       # Phase 587 — Tracy-style CPU marker bar-chart overlay
add_subdirectory(profile_frame_graph_timeline)    # Phase 593 — GPU-side Gantt-chart timeline overlay
