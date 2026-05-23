# engine/render/rhi_opengl — cd::rhi_opengl
#
# OpenGL 4.6 backend for cd::rhi. Phase 18.B opens with boot-only:
# a device that wraps an OpenGL context + reports adapter info via
# glGetString. Resource creation (buffer/texture/swapchain/pipeline)
# lands incrementally in follow-up waves, same shape as the D3D12
# v0.27.0 → v0.36.0 progression.
#
# Why OpenGL alongside Vulkan + D3D12 + Metal: per user direction
# (2026-05-24), OpenGL is the 3rd API priority — it covers downstream
# integration into legacy desktop apps and lets us ship to platforms
# (older Linux drivers, certain industrial OS images) where Vulkan
# isn't reliably available.

cd_add_library(rhi_opengl
  TYPE STATIC
  EXCLUDE_FROM_INSTALL  # boot-only; promotes to install when complete
  SOURCES
    src/OpenGLDevice.cpp
  PUBLIC_DEPS
    cd::core
    cd::rhi
)

# OpenGL is provided by the platform SDK. We use the system loader
# directly (wgl on Windows, glx/egl on Linux, agl on macOS) for the
# v0.49.0 boot wave; a vendored GLAD loader joins in the follow-up
# alongside the buffer/texture surface.
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
