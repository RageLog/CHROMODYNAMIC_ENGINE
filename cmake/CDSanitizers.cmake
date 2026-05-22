# CDSanitizers.cmake — Runtime sanitizer instrumentation
# ADR-005 §G + ADR-015 (Concurrency)
#
# Driven by CD_SANITIZER cache variable:
#   "none"   — no sanitizers (default Release)
#   "asan"   — AddressSanitizer + UBSan (recommended Debug)
#   "tsan"   — ThreadSanitizer (separate preset; mutually exclusive with asan)
#   "msan"   — MemorySanitizer (Clang only)
#
# Platform notes:
#   * Linux/clang and Linux/gcc: works as expected with per-target flags.
#     The CI `sanitizers` job exercises this path (ninja-base-asan +
#     ninja-base-tsan presets on ubuntu-24.04).
#   * Windows/clang (clang.exe targeting MSVC ABI) — lld-link refuses to
#     mix ASAN-instrumented engine TUs with un-instrumented vendored TUs
#     (tinygltf, glslang, volk, vma). Symptom:
#       lld-link: error: /failifmismatch: mismatch detected for 'annotate_string'
#     Workarounds (NONE currently applied):
#       a) Force ASAN at the directory level so vendored code also gets
#          instrumented — risks breaking vendored builds.
#       b) Build the vendored deps separately without ASAN, link them via
#          import libraries — increases build matrix complexity.
#     Decision: run sanitizer regression on Linux only (CI handles it).
#     The Windows-Clang preset still configures, but the link step for
#     samples/tests will fail by design.

include_guard(GLOBAL)

set(CD_SANITIZER "none" CACHE STRING "Sanitizer mode: none|asan|tsan|msan")
set_property(CACHE CD_SANITIZER PROPERTY STRINGS none asan tsan msan)

function(cd_apply_sanitizers target)
  if(CD_SANITIZER STREQUAL "none")
    return()
  endif()

  if(MSVC AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if(CD_SANITIZER STREQUAL "asan")
      target_compile_options(${target} PRIVATE /fsanitize=address)
      # MSVC AddressSanitizer requires linking against asan runtime;
      # CMake handles this when /fsanitize=address is in compile options.
    else()
      message(WARNING "[cd] MSVC supports only asan; CD_SANITIZER=${CD_SANITIZER} ignored on this compiler")
    endif()
    return()
  endif()

  # GCC + Clang + clang-cl on Linux/macOS
  if(CD_SANITIZER STREQUAL "asan")
    target_compile_options(${target} PRIVATE
      -fsanitize=address
      -fsanitize=undefined
      -fno-omit-frame-pointer
      -fno-sanitize-recover=undefined
    )
    target_link_options(${target} PRIVATE
      -fsanitize=address
      -fsanitize=undefined
    )
  elseif(CD_SANITIZER STREQUAL "tsan")
    target_compile_options(${target} PRIVATE
      -fsanitize=thread
      -fno-omit-frame-pointer
    )
    target_link_options(${target} PRIVATE -fsanitize=thread)
  elseif(CD_SANITIZER STREQUAL "msan")
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      target_compile_options(${target} PRIVATE
        -fsanitize=memory
        -fno-omit-frame-pointer
        -fsanitize-memory-track-origins=2
      )
      target_link_options(${target} PRIVATE -fsanitize=memory)
    else()
      message(WARNING "[cd] MSan only supported by Clang; ignored")
    endif()
  endif()
endfunction()
