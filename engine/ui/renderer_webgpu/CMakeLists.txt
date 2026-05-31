# engine/ui/renderer_webgpu — WebGPU UI batcher bridge (ADR-20260530, Phase 5, phase522)
#
# cd::ui_renderer_webgpu — bridges cd::ui::renderer::DrawBatcher to a WebGPU
# device (Dawn / google/dawn).  Mirrors cd::ui_renderer_rhi (Phase 1.2b) but
# targets the WGPUDevice / WGPUCommandEncoder API instead of cd::rhi.
#
# ---- Integration status -------------------------------------------------------
# Dawn is available in the vcpkg registry (port "dawn", version
# 20251202.213730, BSD-3-Clause) but is NOT installed by default because:
#
#   1. It requires Abseil + Python host tooling (~800 MB build).
#   2. Most CI tiers and developer machines lack a WebGPU-capable device.
#   3. The stub backend exercises the full API surface without any GPU.
#
# To enable real Dawn integration:
#
#   1. Add chromodynamic[webgpu] to your vcpkg install invocation, OR run:
#        vcpkg install dawn
#   2. Configure with -DCD_ENABLE_WEBGPU=ON
#
# The option and find_package wiring below are intentionally structured so
# that a future "promote to vcpkg.json feature" migration is a one-liner
# (add the feature + flip the find_package to REQUIRED).
# -------------------------------------------------------------------------------

# ---- Option ------------------------------------------------------------------
option(CD_ENABLE_WEBGPU
    "Enable real Dawn WebGPU backend for cd::ui_renderer_webgpu (requires Dawn vcpkg port)"
    OFF)

# ---- Dawn probe (when opted in) ---------------------------------------------
set(CD_UI_WEBGPU_HAVE_DAWN 0)

if(CD_ENABLE_WEBGPU)
    # Dawn installs as "dawn" with components webgpu_dawn + webgpu_cpp.
    # find_package(Dawn CONFIG) is the canonical path once `vcpkg install dawn`
    # has been run.  We do a QUIET probe first; if it fails we emit a
    # NOTICE (not a fatal error) and fall through to stub mode so the rest
    # of the build is unaffected.
    find_package(Dawn CONFIG QUIET)

    if(Dawn_FOUND)
        set(CD_UI_WEBGPU_HAVE_DAWN 1)
        message(STATUS "[cd::ui_renderer_webgpu] Dawn found — real WebGPU backend enabled")
    else()
        message(STATUS "[cd::ui_renderer_webgpu] CD_ENABLE_WEBGPU=ON but Dawn not found — "
                       "falling back to stub. Run 'vcpkg install dawn' and reconfigure.")
    endif()
else()
    message(STATUS "[cd::ui_renderer_webgpu] Stub backend (CD_ENABLE_WEBGPU=OFF)")
endif()

# ---- Library -----------------------------------------------------------------
cd_add_library(ui_renderer_webgpu
    TYPE STATIC
    SOURCES
        src/Submitter.cpp
    PUBLIC_DEPS
        cd::core
        cd::ui_renderer
)

# Propagate the Dawn availability flag into the compilation unit.
target_compile_definitions(cd_ui_renderer_webgpu
    PUBLIC
        CD_UI_WEBGPU_HAVE_DAWN=${CD_UI_WEBGPU_HAVE_DAWN}
)

if(CD_UI_WEBGPU_HAVE_DAWN)
    # Dawn exposes webgpu_dawn (the C API) and webgpu_cpp (the C++ wrapper).
    # Link webgpu_cpp for the header-included wgpu:: namespace used in
    # Submitter.hpp + Submitter.cpp.
    target_link_libraries(cd_ui_renderer_webgpu
        PUBLIC
            Dawn::webgpu_cpp
    )
endif()

# ---- Tests -------------------------------------------------------------------
if(CD_ENABLE_TESTING)
    add_subdirectory(tests)
endif()
