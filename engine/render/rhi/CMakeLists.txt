# engine/render/rhi — RHI abstraction + all backends
#
# ADR-001 — RHI abstraction. The interface library (cd::rhi) is header-only.
# Concrete backends (cd::rhi_d3d12, cd::rhi_vulkan, cd::rhi_metal,
# cd::rhi_opengl, cd::rhi_native) live in src/<backend>/ and are defined
# below. Tests live in tests/ (interface tests) and tests/<backend>/.
#
# Layout (phase418):
#   include/cd/rhi/          — public interface headers
#     d3d12/                 — D3D12 public headers
#     vulkan/                — Vulkan public headers
#     metal/                 — Metal public headers
#     opengl/                — OpenGL public headers
#   src/
#     d3d12/                 — D3D12 implementation
#     vulkan/                — Vulkan implementation (+ internal headers)
#     metal/                 — Metal implementation
#     opengl/                — OpenGL implementation
#     native/                — Native dispatcher
#   tests/
#     <interface tests>
#     vulkan/                — Vulkan backend tests

# ── cd::rhi (INTERFACE — header-only abstraction) ──────────────────────────
cd_add_library(rhi
  TYPE INTERFACE
  PUBLIC_DEPS cd::core cd::math
)

# ── cd::rhi_d3d12 ──────────────────────────────────────────────────────────
# Windows D3D12 backend. Phase 12.B boot-only (device + adapter + queue +
# swapchain + clear-color present). v0.27.x patches add PSO, draw, upload.
cd_add_library(rhi_d3d12
  TYPE STATIC
  EXCLUDE_FROM_INSTALL  # boot-only; v0.27.x patches complete the surface
  SOURCES
    src/d3d12/D3D12Device.cpp
    src/d3d12/D3D12ShaderCompile.cpp
  PUBLIC_DEPS cd::core cd::rhi
)

# Windows-only link path. System SDK provides d3d12/dxgi/dxguid/d3dcompiler/dxcompiler.
if(WIN32)
  target_link_libraries(cd_rhi_d3d12
    PRIVATE d3d12 dxgi dxguid d3dcompiler dxcompiler)

  set(_dxc_dll_dir "$ENV{WindowsSdkDir}bin/$ENV{WindowsSDKVersion}x64")
  string(REGEX REPLACE "\\\\" "/" _dxc_dll_dir "${_dxc_dll_dir}")
endif()

# ── cd::rhi_vulkan ─────────────────────────────────────────────────────────
# Sprint S3.3 — Vulkan backend. FetchContent: Vulkan-Headers + volk + VMA.
option(CD_ENABLE_VULKAN "Build cd::rhi Vulkan backend" ON)
if(CD_ENABLE_VULKAN)
  include(FetchContent)

  FetchContent_Declare(
    vulkan_headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG        vulkan-sdk-1.3.290.0
    GIT_SHALLOW    TRUE
  )

  FetchContent_Declare(
    volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG        vulkan-sdk-1.3.290.0
    GIT_SHALLOW    TRUE
  )

  FetchContent_Declare(
    vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        v3.1.0
    GIT_SHALLOW    TRUE
  )

  # Vulkan-Headers C++20 module not consumed — we use classic <vulkan/vulkan.h>.
  set(VULKAN_HEADERS_ENABLE_MODULE OFF CACHE BOOL "" FORCE)
  set(VULKAN_HEADERS_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

  FetchContent_MakeAvailable(vulkan_headers volk vma)

  if(TARGET Vulkan-Module)
    set_target_properties(Vulkan-Module PROPERTIES EXCLUDE_FROM_ALL TRUE)
  endif()

  option(CD_RHI_PREFER_X11 "Force VK_KHR_xlib_surface even when Wayland is available" OFF)

  if(TARGET volk)
    if(MSVC)
      target_compile_options(volk PRIVATE /W0)
    else()
      target_compile_options(volk PRIVATE -w)
    endif()
    target_link_libraries(volk PUBLIC Vulkan::Headers)
    if(WIN32)
      target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_WIN32_KHR)
    elseif(APPLE)
      target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_METAL_EXT)
    elseif(UNIX AND NOT ANDROID)
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
      src/vulkan/VulkanInstance.cpp
      src/vulkan/VulkanDevice.cpp
      src/vulkan/VulkanDeviceFactory.cpp
      src/vulkan/VulkanCommandBuffer.cpp
      src/vulkan/VulkanVma.cpp
      src/vulkan/NativeHandles.cpp
    PUBLIC_DEPS  cd::rhi
  )

  # Internal headers live alongside .cpp in src/vulkan/
  target_include_directories(cd_rhi_vulkan PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vulkan
  )

  target_link_libraries(cd_rhi_vulkan PUBLIC volk Vulkan::Headers GPUOpen::VulkanMemoryAllocator)
  target_compile_definitions(cd_rhi_vulkan PRIVATE
    VK_NO_PROTOTYPES   # volk owns the function pointers
  )

  option(CD_RHI_VULKAN_14 "Request Vulkan 1.4 features at device creation" OFF)
  target_compile_definitions(cd_rhi_vulkan PRIVATE
    CD_RHI_VULKAN_14=$<BOOL:${CD_RHI_VULKAN_14}>
  )

  if(WIN32)
    target_compile_definitions(cd_rhi_vulkan PRIVATE
      VK_USE_PLATFORM_WIN32_KHR
      WIN32_LEAN_AND_MEAN
      NOMINMAX
    )
  elseif(APPLE)
    target_compile_definitions(cd_rhi_vulkan PRIVATE VK_USE_PLATFORM_METAL_EXT)
  elseif(UNIX AND NOT ANDROID)
    if(CD_RHI_PREFER_X11)
      target_compile_definitions(cd_rhi_vulkan PRIVATE VK_USE_PLATFORM_XLIB_KHR)
    else()
      target_compile_definitions(cd_rhi_vulkan PRIVATE VK_USE_PLATFORM_WAYLAND_KHR)
    endif()
  endif()

  cd_mark_target_system_includes(Vulkan::Headers)
  cd_mark_target_system_includes(volk)
  cd_mark_target_system_includes(GPUOpen::VulkanMemoryAllocator)
endif() # CD_ENABLE_VULKAN

# ── cd::rhi_metal ──────────────────────────────────────────────────────────
# macOS / iOS Metal backend. Full Metal lands in Phase 9 Sprint 1;
# this wave (Wave 97) gives a factory stub.
cd_add_library(rhi_metal
  TYPE STATIC
  SOURCES src/metal/MetalDevice.cpp
  PUBLIC_DEPS cd::core cd::rhi
)

if(APPLE)
  find_library(CD_METAL_FRAMEWORK Metal)
  find_library(CD_FOUNDATION_FRAMEWORK Foundation)
  if(CD_METAL_FRAMEWORK AND CD_FOUNDATION_FRAMEWORK)
    target_link_libraries(cd_rhi_metal PRIVATE
      ${CD_METAL_FRAMEWORK}
      ${CD_FOUNDATION_FRAMEWORK})
  endif()
endif()

# ── cd::rhi_opengl ─────────────────────────────────────────────────────────
# OpenGL 4.6 backend. Phase 18.B boot-only. 3rd API priority.
cd_add_library(rhi_opengl
  TYPE STATIC
  EXCLUDE_FROM_INSTALL  # boot-only; promotes to install when complete
  SOURCES
    src/opengl/OpenGLDevice.cpp
  PUBLIC_DEPS
    cd::core
    cd::rhi
)

if(WIN32)
  target_link_libraries(cd_rhi_opengl PRIVATE opengl32 gdi32)
elseif(APPLE)
  find_library(_cd_gl OpenGL)
  if(_cd_gl)
    target_link_libraries(cd_rhi_opengl PRIVATE ${_cd_gl})
  endif()
elseif(UNIX)
  target_link_libraries(cd_rhi_opengl PRIVATE GL)
endif()

# ── cd::rhi_native ─────────────────────────────────────────────────────────
# Unified GPU backend dispatcher (Wave 97). Links available concrete backends
# and exposes cd::rhi::make_native_device() selecting the best for the host.
set(_RHI_NATIVE_DEPS cd::core cd::rhi cd::rhi_metal cd::rhi_d3d12)
if(CD_ENABLE_VULKAN AND TARGET cd_rhi_vulkan)
  list(APPEND _RHI_NATIVE_DEPS cd::rhi_vulkan)
endif()

cd_add_library(rhi_native
  TYPE STATIC
  EXCLUDE_FROM_INSTALL  # transitive on cd::rhi_vulkan (vendored volk/VMA)
  SOURCES src/native/NativeDevice.cpp
  PUBLIC_DEPS ${_RHI_NATIVE_DEPS}
)

# ── Tests ──────────────────────────────────────────────────────────────────
if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
  if(CD_ENABLE_VULKAN AND TARGET cd_rhi_vulkan)
    add_subdirectory(tests/vulkan)
  endif()
endif()
