# CDWarnings.cmake — Zero Warning Policy
# ADR-005 §A (Build/ABI) + CLAUDE.md §6
#
# Zero-tolerance: -Wall -Werror -Wextra -Wshadow -Wnon-virtual-dtor -Wpedantic -Wconversion

include_guard(GLOBAL)

function(cd_apply_warnings target)
  if(NOT CD_ENABLE_WARNINGS)
    return()
  endif()

  if(MSVC)
    # Common flags accepted by both real MSVC and clang-cl.
    target_compile_options(${target} PRIVATE
      /W4
      /permissive-
      /Zc:__cplusplus
      /Zc:inline
      /utf-8
      /diagnostics:caret
    )
    # MSVC-only options (clang-cl warns "unused argument" → -Werror trips).
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      target_compile_options(${target} PRIVATE
        /Zc:preprocessor
        /MP
      )
    else()
      # clang-cl: silence unused-flag noise from VS toolchain defaults so
      # -Werror does not abort 3rd-party headers we don't control.
      target_compile_options(${target} PRIVATE
        -Wno-unused-command-line-argument
      )
    endif()
    if(CD_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()

    # Disable specific noisy warnings on MSVC (intentional, documented)
    target_compile_options(${target} PRIVATE
      /wd4251  # 'identifier': class 'type' needs DLL interface (PIMPL across DLL — by design)
    )

    # Treat every angle-bracket include as an external/system header so
    # warnings from vendor (Vulkan-Headers, volk, gtest, fmt, …) never trip
    # /WX. Production-stable since VS 2019 16.10; the /experimental: prefix is
    # historical and remains the documented spelling through VS 17.x.
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      target_compile_options(${target} PRIVATE
        /experimental:external
        /external:anglebrackets
        /external:W0
      )
    endif()
  else()
    # GCC + Clang + Clang-cl (in some modes)
    target_compile_options(${target} PRIVATE
      -Wall
      -Wextra
      -Wpedantic
      -Wshadow
      -Wnon-virtual-dtor
      -Wold-style-cast
      -Wcast-align
      -Wunused
      -Woverloaded-virtual
      -Wconversion
      -Wsign-conversion
      -Wnull-dereference
      -Wdouble-promotion
      -Wformat=2
      -Wmissing-declarations
    )
    if(CD_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      target_compile_options(${target} PRIVATE
        -Wmisleading-indentation
        -Wduplicated-cond
        -Wduplicated-branches
        -Wlogical-op
        -Wuseless-cast
      )
    endif()

    # Clang-cl already gets MSVC flags above where applicable
  endif()
endfunction()
