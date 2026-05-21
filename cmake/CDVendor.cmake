# CDVendor.cmake — vendor dependency isolation helpers
# Used by every subdirectory that brings third-party code into the build
# (vulkan-headers, volk, gtest, fmt, ...). The single responsibility is to
# mark vendor headers as SYSTEM so their warnings never trip our /WX bar.
#
# Why a function instead of `FetchContent_Declare(... SYSTEM)`?
#   * FetchContent's SYSTEM keyword (CMake 3.25+) only marks the fetched
#     project's *own* source files as system — it does not retroactively
#     change the INTERFACE_INCLUDE_DIRECTORIES that downstream consumers see.
#   * find_package()-sourced targets (vcpkg GTest, fmt) can't take a SYSTEM
#     keyword at all — the config-file package decides.
# Both paths are covered by this helper.

include_guard(GLOBAL)

function(cd_mark_target_system_includes target)
  if(NOT TARGET ${target})
    return()
  endif()

  # Resolve ALIASED_TARGET (e.g. Vulkan::Headers → Vulkan-Headers).
  get_target_property(_aliased ${target} ALIASED_TARGET)
  if(_aliased)
    set(_real ${_aliased})
  else()
    set(_real ${target})
  endif()

  get_target_property(_imported ${_real} IMPORTED)
  get_target_property(_inc ${_real} INTERFACE_INCLUDE_DIRECTORIES)
  if(NOT _inc)
    return()
  endif()

  # vcpkg's config-file packages typically already set this; appending the
  # same paths is a no-op. For FetchContent / vendored tiers it is the actual
  # fix. set_property APPEND tolerates the property not pre-existing.
  set_property(TARGET ${_real} APPEND PROPERTY
               INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_inc}")
endfunction()
