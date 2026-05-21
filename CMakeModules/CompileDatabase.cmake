# cmake/CompileDatabase.cmake
# =============================================================================
# IDE COMPILE DATABASE — Ensures compile_commands.json at source root
# =============================================================================
#
# Problem: Visual Studio generators don't produce compile_commands.json.
#          Ninja Multi-Config produces it in the build dir but IDEs expect it
#          at the source root.
#
# Solution: On every configure, sync compile_commands.json to ${CMAKE_SOURCE_DIR}.
#   1. If current build dir has it (Ninja/Ninja Multi-Config) → copy to root
#   2. Else if tidy-db has it (from a prior StaticAnalysis configure) → copy to root
#   3. Else → run a lightweight Ninja sub-configure into build/tidy-db → copy
#
# This module is safe to include unconditionally. It does NOT modify any targets
# or interfere with StaticAnalysis.cmake.
# =============================================================================

set(_cdb_source_root_dst "${CMAKE_SOURCE_DIR}/compile_commands.json")
set(_cdb_build_dir_src   "${CMAKE_BINARY_DIR}/compile_commands.json")
set(_cdb_tidy_db_dir     "${CMAKE_SOURCE_DIR}/build/tidy-db")
set(_cdb_tidy_db_src     "${_cdb_tidy_db_dir}/compile_commands.json")

# Guard: skip if we ARE the tidy-db sub-configure (prevent infinite recursion)
if(CMAKE_BINARY_DIR STREQUAL "${_cdb_tidy_db_dir}")
    return()
endif()

# ─── Strategy 1: Copy from current build directory ───────────────────────────
# Works for: Ninja, Ninja Multi-Config, Unix Makefiles (any generator that
# honours CMAKE_EXPORT_COMPILE_COMMANDS). On a fresh first configure the file
# won't exist yet (generated during the Generate phase), but on every
# subsequent re-configure it will be present from the previous run.
if(EXISTS "${_cdb_build_dir_src}")
    file(COPY_FILE "${_cdb_build_dir_src}" "${_cdb_source_root_dst}")
    message(STATUS "[CompileDB] Synced compile_commands.json → source root (from build dir)")
    return()
endif()

# ─── Strategy 2: Copy from tidy-db (previous StaticAnalysis run) ─────────────
if(EXISTS "${_cdb_tidy_db_src}")
    file(COPY_FILE "${_cdb_tidy_db_src}" "${_cdb_source_root_dst}")
    message(STATUS "[CompileDB] Synced compile_commands.json → source root (from tidy-db)")
    return()
endif()

# ─── Strategy 3: Generate via a Ninja sub-configure ──────────────────────────
# This runs only once (first configure, usually with VS generator).
# Subsequent configures will hit Strategy 1 or 2.
message(STATUS "[CompileDB] No compile_commands.json found. Running Ninja sub-configure...")

execute_process(
    COMMAND ${CMAKE_COMMAND}
        -G "Ninja"
        -DCMAKE_BUILD_TYPE=Debug
        -DCMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD}
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
        # Include the real targets (tests + samples) so clangd has flags for every TU.
        -DCD_ENABLE_TESTING=ON
        -DCD_ENABLE_SAMPLES=ON
        -DCD_ENABLE_CLANG_TIDY=OFF
        -DCD_ENABLE_CLANG_FORMAT=OFF
        -DCD_ENABLE_DOXYGEN=OFF
        -DCD_DISABLE_VCPKG=${CD_DISABLE_VCPKG}
        -S "${CMAKE_SOURCE_DIR}"
        -B "${_cdb_tidy_db_dir}"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    RESULT_VARIABLE _cdb_result
    OUTPUT_QUIET
    ERROR_QUIET
)

if(_cdb_result EQUAL 0 AND EXISTS "${_cdb_tidy_db_src}")
    file(COPY_FILE "${_cdb_tidy_db_src}" "${_cdb_source_root_dst}")
    message(STATUS "[CompileDB] Generated and synced compile_commands.json → source root")
else()
    message(WARNING "[CompileDB] Failed to generate compile_commands.json. "
                    "Install Ninja generator or manually run: cmake -G Ninja -B build/tidy-db")
endif()
