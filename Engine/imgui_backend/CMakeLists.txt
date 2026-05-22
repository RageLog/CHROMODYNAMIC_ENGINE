# engine/imgui_backend — cd::imgui_backend
#
# Dear ImGui (docking branch) + Vulkan dynamic-rendering backend + Win32
# input bridge, wrapped behind a thin `cd::imgui::Context` class. Pulls
# ImGui as a single FetchContent dep; compiles the necessary .cpp files
# directly (no upstream CMake — they don't ship one for the imgui_impl_*
# backends, just core).

include(FetchContent)

FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        docking
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

cd_add_library(imgui_backend
  TYPE STATIC
  SOURCES
    src/Context.cpp
  PUBLIC_DEPS
    cd::core
    cd::platform
    cd::rhi
    cd::rhi_vulkan
)

# Compile the ImGui core + Vulkan/Win32 backends directly into our static
# lib. Add them as source files (not a separate target) so their symbols
# end up in cd_imgui_backend.lib with no extra linker glue.
set(_cd_imgui_sources
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
if(WIN32)
  list(APPEND _cd_imgui_sources ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp)
endif()
target_sources(cd_imgui_backend PRIVATE ${_cd_imgui_sources})

# Public include — consumers do `#include <imgui.h>` directly for the
# normal ImGui API. SYSTEM so the engine's -Werror policy doesn't bounce
# on ImGui-internal warnings.
target_include_directories(cd_imgui_backend SYSTEM PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)

# volk integration: our engine NEVER links vulkan-1.dll, every Vulkan
# entry point is loaded at runtime via volk. ImGui's Vulkan backend has
# a convenience define for exactly this case:
#   IMGUI_IMPL_VULKAN_USE_VOLK  → backend pulls volk.h directly and uses
#                                  the volk-loaded function pointers.
# Without this define, the backend assumes the global vkXxx() prototypes
# exist (they don't in our build) → all backend calls jump to nullptr
# function pointers and the process segfaults on the first frame.
target_compile_definitions(cd_imgui_backend PRIVATE IMGUI_IMPL_VULKAN_USE_VOLK)

# ImGui's Win32 backend uses DWM (DwmIsCompositionEnabled etc.). MSVC
# autolinks dwmapi.lib via pragma comment; MinGW does not, so we link it
# explicitly. Marked PUBLIC because the demos that include imgui_impl_win32
# transitively need it too.
if(WIN32 AND (MINGW OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
  target_link_libraries(cd_imgui_backend PUBLIC dwmapi imm32)
endif()

# Drop warnings on the vendored ImGui sources to keep -Werror sane.
foreach(_src IN LISTS _cd_imgui_sources)
  if(MSVC AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set_source_files_properties(${_src} PROPERTIES
      COMPILE_OPTIONS "/wd4100;/wd4189;/wd4244;/wd4267;/wd4456;/wd4505;/wd4996;/wd4127")
  else()
    set_source_files_properties(${_src} PROPERTIES COMPILE_OPTIONS "-w")
  endif()
endforeach()
