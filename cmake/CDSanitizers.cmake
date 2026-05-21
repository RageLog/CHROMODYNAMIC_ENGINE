# CDSanitizers.cmake — Runtime sanitizer instrumentation
# ADR-005 §G + ADR-015 (Concurrency)
#
# Driven by CD_SANITIZER cache variable:
#   "none"   — no sanitizers (default Release)
#   "asan"   — AddressSanitizer + UBSan (recommended Debug)
#   "tsan"   — ThreadSanitizer (separate preset; mutually exclusive with asan)
#   "msan"   — MemorySanitizer (Clang only)

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
