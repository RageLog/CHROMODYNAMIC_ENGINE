# hello_metal — M12 (B2 — ADR-20260615): windowed Metal host with a
# Windows-compilable #else stub. The TARGET builds on every platform.
#
# main.cpp gates its windowed Cocoa/Metal/QuartzCore body behind
#   `#if defined(__APPLE__) && CD_RHI_METAL_ENABLED`
# and links cd::rhi_metal (the real .mm backend when CD_RHI_METAL_ENABLED=ON on
# Apple; the kBackendInitFailed plain-C++ stub everywhere else). On Win11 the
# #else stub compiles cleanly under -Werror.
cd_add_sample(hello_metal
  SOURCES main.cpp
  DEPS    cd::core cd::rhi cd::rhi_metal
)

if(APPLE AND CD_RHI_METAL_ENABLED)
  # The windowed body uses Objective-C++ (NSWindow / NSView / CAMetalLayer), so
  # compile main.cpp as Objective-C++ and link the Apple frameworks. ARC is
  # requested explicitly (matches cd_rhi_metal). The `#else` branch is dead on
  # this platform, so the whole TU is the windowed host.
  set_source_files_properties(main.cpp PROPERTIES LANGUAGE OBJCXX)
  target_compile_options(cd_sample_hello_metal PRIVATE -fobjc-arc)

  find_library(CD_SAMPLE_COCOA_FRAMEWORK      Cocoa      REQUIRED)
  find_library(CD_SAMPLE_METAL_FRAMEWORK      Metal      REQUIRED)
  find_library(CD_SAMPLE_QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
  target_link_libraries(cd_sample_hello_metal PRIVATE
    ${CD_SAMPLE_COCOA_FRAMEWORK}
    ${CD_SAMPLE_METAL_FRAMEWORK}
    ${CD_SAMPLE_QUARTZCORE_FRAMEWORK})
endif()
