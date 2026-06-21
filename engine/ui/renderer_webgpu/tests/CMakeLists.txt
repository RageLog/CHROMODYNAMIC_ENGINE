cd_add_test(webgpu_submitter
  SOURCES test_webgpu_submitter.cpp
  DEPS    cd::ui_renderer_webgpu cd::ui_renderer cd::core
)

# ---- Headless Dawn render+readback test (real GPU pipeline) ------------------
# Only built when Dawn is actually present (CD_UI_WEBGPU_HAVE_DAWN propagated
# from the parent renderer_webgpu CMakeLists).  It creates a HEADLESS device
# via dawn::native::Instance, renders a UI quad into an offscreen RGBA8 texture
# and reads the pixels back — proving the live pipeline rasterizes.  GTEST_SKIPs
# when no adapter/device can be created on the host.
if(CD_UI_WEBGPU_HAVE_DAWN)
  cd_add_test(webgpu_dawn_device
    SOURCES test_webgpu_dawn_device.cpp
    DEPS    cd::ui_renderer_webgpu cd::ui_renderer cd::core
  )

  # The Dawn runtime DLL must sit next to the test exe so it loads at run time.
  add_custom_command(TARGET cd_test_webgpu_dawn_device POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:dawn::webgpu_dawn>
            $<TARGET_FILE_DIR:cd_test_webgpu_dawn_device>
    VERBATIM)
endif()
