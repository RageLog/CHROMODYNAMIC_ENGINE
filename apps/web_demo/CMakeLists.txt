# =============================================================================
# apps/web_demo — CHROMODYNAMIC Web Demo (Emscripten / WebGPU)
# Phase 773 (FINALE-7 W2C Sprint-1)
#
# Builds the web_demo WASM executable that exercises:
#   - cd::platform::web::WebWindow (HTMLCanvasElement wrapper)
#   - cd::platform::web::WebInputBridge (Emscripten input → OSEvent)
#   - Emscripten requestAnimationFrame main loop (main_web.cpp)
#
# Gate: only compiled under Emscripten. Win11 / desktop CI sees an early
# return and produces no targets, so the native build stays untouched.
#
# Sprint-2+ additions (when they land):
#   - cd::rhi::webgpu link (Dawn / wgpu_native WASM backend)
#   - -sUSE_WEBGPU=1 linker flag
#   - --shell-file pointing at shell_web_demo.html
#   - EXPORTED_FUNCTIONS for JS interop
# =============================================================================

if(NOT EMSCRIPTEN)
  # Not a web build — skip gracefully.
  return()
endif()

# ---------------------------------------------------------------------------
# web_demo executable
# ---------------------------------------------------------------------------
add_executable(web_demo
  ../../engine/foundation/platform/src/platform/web/main_web.cpp
)

target_include_directories(web_demo
  PRIVATE
    ../../engine/foundation/platform/include
)

target_link_libraries(web_demo
  PRIVATE
    cd::platform     # pulls in WebWindow + WebInputBridge sources
    cd::core         # CD_LOG, Defines, ErrorCode
)

# ---------------------------------------------------------------------------
# Emscripten link flags
# ---------------------------------------------------------------------------
target_link_options(web_demo
  PRIVATE
    # WebAssembly output
    -sWASM=1
    -sWASM_BIGINT=1
    # Memory
    -sINITIAL_MEMORY=67108864    # 64 MiB initial heap
    -sMAXIMUM_MEMORY=268435456   # 256 MiB cap (ALLOW_MEMORY_GROWTH implied)
    -sALLOW_MEMORY_GROWTH=1
    # Filesystem (IDBFS for asset caching in Sprint-2+)
    -sFILESYSTEM=1
    # HTML shell with canvas + WebGPU detection
    --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell_web_demo.html
    # Export name for JS module
    -sEXPORT_NAME=ChromodynamicWebDemo
    # Exit behaviour (let emscripten_cancel_main_loop clean up)
    -sEXIT_RUNTIME=1
    # Sprint-2+: enable WebGPU backend
    # -sUSE_WEBGPU=1
    # Sprint-2+: export specific functions for JS interop
    # -sEXPORTED_FUNCTIONS=["_main"]
    $<$<CONFIG:Debug>:-g3 -sDEMANGLE_SUPPORT=1 -sSAFE_HEAP=1 -sASSERTIONS=2>
    $<$<CONFIG:Release>:-O2 --closure 1>
)

set_target_properties(web_demo
  PROPERTIES
    OUTPUT_NAME          "chromodynamic_web"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/web_demo"
    SUFFIX               ".html"   # Emscripten produces .html + .js + .wasm
)

# ---------------------------------------------------------------------------
# Copy the HTML shell to the output directory (for local dev-server use)
# ---------------------------------------------------------------------------
add_custom_command(TARGET web_demo POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/shell_web_demo.html"
    "${CMAKE_BINARY_DIR}/web_demo/shell_web_demo.html"
  COMMENT "[web_demo] Copying HTML shell to build output"
)
