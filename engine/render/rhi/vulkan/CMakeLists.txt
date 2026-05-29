# engine/render/rhi/vulkan — cd::rhi::vulkan
# Sprint S3.3 — Vulkan backend for cd::rhi.
#
# Dependencies (fetched if not already provided by the parent build):
#   * Khronos Vulkan-Headers — header-only; provides <vulkan/vulkan.h>
#   * volk — runtime symbol loader so we never link against vulkan-1.dll.
#           Falls back gracefully on machines without a Vulkan driver because
#           initialisation just returns an error instead of crashing the loader.
#   * VulkanMemoryAllocator (VMA) — AMD GPUOpen; industry-standard sub-allocator.
#           Header-only, VMA_IMPLEMENTATION defined in src/VulkanVma.cpp.

include(FetchContent)

# vulkan-headers (SDK-versioned; tracking a recent stable tag).
FetchContent_Declare(
  vulkan_headers
  GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
  GIT_TAG        vulkan-sdk-1.3.290.0
  GIT_SHALLOW    TRUE
)

# volk — header + small .c for runtime loading.
FetchContent_Declare(
  volk
  GIT_REPOSITORY https://github.com/zeux/volk.git
  GIT_TAG        vulkan-sdk-1.3.290.0
  GIT_SHALLOW    TRUE
)

# VMA — AMD GPUOpen Vulkan Memory Allocator. Header-only, INTERFACE-style.
FetchContent_Declare(
  vma
  GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
  GIT_TAG        v3.1.0
  GIT_SHALLOW    TRUE
)

# Vulkan-Headers ships a C++20 module target (vulkan.cppm → Vulkan-Module
# library). MSVC's cl.exe can scan it; clang-cl + Ninja cannot ("the compiler
# does not provide a way to discover the import graph dependencies"). We do
# not consume the module — we use the classic <vulkan/vulkan.h> headers via
# volk — so we opt out of building it. This keeps the configure portable
# across MSVC / clang-cl / GCC without a per-compiler conditional.
set(VULKAN_HEADERS_ENABLE_MODULE OFF CACHE BOOL "" FORCE)
set(VULKAN_HEADERS_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

# Make available; volk's CMake links against Vulkan::Headers.
FetchContent_MakeAvailable(vulkan_headers volk vma)

# Belt-and-suspenders: if the upstream cache var name changes, kill any
# Vulkan-Module target that did get created — we never link it.
if(TARGET Vulkan-Module)
  set_target_properties(Vulkan-Module PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()

# Platform options declared early so the volk block below can reference them.
option(CD_RHI_PREFER_X11 "Force VK_KHR_xlib_surface even when Wayland is available" OFF)

# Quiet -Werror noise from the vendored projects on our strict flags.
if(TARGET volk)
  if(MSVC)
    target_compile_options(volk PRIVATE /W0)
  else()
    target_compile_options(volk PRIVATE -w)
  endif()
  # volk's own translation unit needs <vulkan/vulkan.h> from our fetched
  # headers (its CMake doesn't link Vulkan::Headers by default).
  target_link_libraries(volk PUBLIC Vulkan::Headers)
  # Platform define MUST be set when volk.c is compiled so its PFN_ table
  # includes vkCreate*SurfaceKHR / equivalents. Without this, surface
  # creation link-errors at the consumer (cd_rhi_vulkan).
  if(WIN32)
    target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_WIN32_KHR)
  elseif(APPLE)
    target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_METAL_EXT)
  elseif(UNIX AND NOT ANDROID)
    # Mirror the option the consumer target uses (default: Wayland).
    if(CD_RHI_PREFER_X11)
      target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_XLIB_KHR)
    else()
      target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_WAYLAND_KHR)
    endif()
  endif()
endif()

cd_add_library(rhi_vulkan
  TYPE STATIC
  EXCLUDE_FROM_INSTALL  # vendored volk + Vulkan-Headers + VMA via FetchContent
  SOURCES
    src/VulkanInstance.cpp
    src/VulkanDevice.cpp
    src/VulkanDeviceFactory.cpp
    src/VulkanCommandBuffer.cpp
    src/VulkanVma.cpp
    src/NativeHandles.cpp
  PUBLIC_DEPS  cd::rhi
)

target_link_libraries(cd_rhi_vulkan PUBLIC volk Vulkan::Headers GPUOpen::VulkanMemoryAllocator)
target_compile_definitions(cd_rhi_vulkan PRIVATE
  VK_NO_PROTOTYPES   # volk owns the function pointers
)

# Vulkan 1.4 feature opt-in. Default OFF because the engine's S3.x baseline
# is Vulkan 1.3; enabling this requires bumping vulkan-headers / volk to
# 1.4 SDK tags and a driver that reports apiVersion ≥ 1.4. Forward-compat
# plumbing for engines that adopt 1.4 features (push_descriptor core,
# maintenance5/6, dynamic_rendering_local_read).
option(CD_RHI_VULKAN_14 "Request Vulkan 1.4 features at device creation" OFF)
target_compile_definitions(cd_rhi_vulkan PRIVATE
  CD_RHI_VULKAN_14=$<BOOL:${CD_RHI_VULKAN_14}>
)
if(WIN32)
  # VK_USE_PLATFORM_WIN32_KHR unlocks VkWin32SurfaceCreateInfoKHR + the
  # VK_KHR_win32_surface symbols inside <vulkan/vulkan.h>. WIN32_LEAN_AND_MEAN
  # + NOMINMAX avoid the windows.h ceremonial pollution that vulkan-win32.h
  # pulls in transitively.
  target_compile_definitions(cd_rhi_vulkan PRIVATE
    VK_USE_PLATFORM_WIN32_KHR
    WIN32_LEAN_AND_MEAN
    NOMINMAX
  )
elseif(APPLE)
  # macOS uses Metal under the hood (MoltenVK). The user supplies a
  # CAMetalLayer* via SwapchainDesc::window_handle.
  target_compile_definitions(cd_rhi_vulkan PRIVATE VK_USE_PLATFORM_METAL_EXT)
elseif(UNIX AND NOT ANDROID)
  # Linux: prefer Wayland (modern) over Xlib. The CD_RHI_PREFER_X11 option
  # (declared earlier) forces the Xlib path when needed.
  if(CD_RHI_PREFER_X11)
    target_compile_definitions(cd_rhi_vulkan PRIVATE VK_USE_PLATFORM_XLIB_KHR)
  else()
    target_compile_definitions(cd_rhi_vulkan PRIVATE VK_USE_PLATFORM_WAYLAND_KHR)
  endif()
endif()

# Vulkan-Headers and volk are vendor C code; mark their includes SYSTEM so
# warnings emitted inside those headers are auto-silenced under all target
# compilers. Helper lives in cmake/CDVendor.cmake (project-wide).
# Our own TUs still pass the strict warning set — VulkanMacros.hpp shims the
# Vulkan macros that contain C-style casts.
cd_mark_target_system_includes(Vulkan::Headers)
cd_mark_target_system_includes(volk)
cd_mark_target_system_includes(GPUOpen::VulkanMemoryAllocator)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
