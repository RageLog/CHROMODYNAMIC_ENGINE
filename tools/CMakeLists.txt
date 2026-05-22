# tools/ — offline asset and build helpers (NOT samples).
# Tools opt into engine deps but never participate in the runtime DAG.
add_subdirectory(cook_mesh)
add_subdirectory(cook_texture)
add_subdirectory(bench_compare)
add_subdirectory(render_thread_soak)
add_subdirectory(changelog_gen)
