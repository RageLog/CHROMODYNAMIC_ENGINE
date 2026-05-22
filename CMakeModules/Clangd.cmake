# =============================================================================
# CHROMODYNAMIC — Clangd.cmake
# Generates the repo-root .clangd from .clangd.in by substituting the
# active preset's compiler driver, C++ std flag, and build directory.
#
# Why per-preset generation: clangd's compiler-driver convention diverges
# between MSVC (clang-cl + /std:c++NN) and Unix-style clang/gcc (clang++/
# g++ + -std=c++NN). A static .clangd that hard-codes one breaks the
# other; configure-time substitution keeps the file in lockstep with the
# preset you actually built last.
#
# Inputs:
#   .clangd.in                tracked template
#   CMAKE_CXX_COMPILER_ID     GNU / Clang / MSVC
#   MSVC, MINGW               canonical CMake driver flags
#   CMAKE_CXX_STANDARD        engine baseline (23)
#   CMAKE_BINARY_DIR          per-preset build dir (FetchContent _deps)
#
# Output:
#   .clangd                   regenerated each configure; gitignored
# =============================================================================
include_guard(GLOBAL)

set(_cd_clangd_in  "${CMAKE_SOURCE_DIR}/.clangd.in")
set(_cd_clangd_out "${CMAKE_SOURCE_DIR}/.clangd")

if(NOT EXISTS "${_cd_clangd_in}")
    message(STATUS "[cd] .clangd.in not found — skipping clangd config generation")
    return()
endif()

# Pick the driver clangd should mimic. Match the active build toolchain so
# clangd parses with the same dialect the real compiler uses.
if(MSVC)
    # clang-cl ALWAYS (never raw `cl`). clangd's parser is the upstream
    # clang front-end; in plain cl driver mode it cannot expand MSVC
    # <functional>'s _Enable_if_callable_t / _Func_class internal SFINAE.
    # clang-cl driver mode enables the MSVC compatibility shims clang
    # ships, so the same headers parse cleanly. Build still uses cl.exe;
    # clangd just wears a different hat for static analysis.
    set(CD_CLANGD_COMPILER "clang-cl")
elseif(MINGW OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CD_CLANGD_COMPILER "g++")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CD_CLANGD_COMPILER "clang++")
else()
    set(CD_CLANGD_COMPILER "")
endif()

# Render the "Compiler:" YAML line so the template stays valid when no
# driver is selected (just leaves a leading comment).
if(CD_CLANGD_COMPILER)
    set(CD_CLANGD_COMPILER_LINE "  Compiler: ${CD_CLANGD_COMPILER}")
else()
    set(CD_CLANGD_COMPILER_LINE "  # Compiler: <auto-detect>")
endif()

# C++ std flag must match the active driver. clang-cl rejects -std=c++NN
# (unknown flag) and silently falls back to C++98 if no /std:c++NN is
# accepted — that breaks parsing of every C++23 feature. g++/clang++
# reject /std:c++NN. So we pick per driver.
if(MSVC)
    set(CD_CLANGD_STDFLAG "/std:c++${CMAKE_CXX_STANDARD}")
else()
    set(CD_CLANGD_STDFLAG "-std=c++${CMAKE_CXX_STANDARD}")
endif()

# Per-preset build dir for FetchContent _deps lookups (volk/vulkan-headers
# /vma/glslang/googletest live under build/<preset>/_deps/<dep>-src).
set(CD_CLANGD_BUILD_DIR "${CMAKE_BINARY_DIR}")

configure_file("${_cd_clangd_in}" "${_cd_clangd_out}" @ONLY)

message(STATUS "[cd] .clangd regenerated (driver=${CD_CLANGD_COMPILER} std=${CD_CLANGD_STDFLAG})")
