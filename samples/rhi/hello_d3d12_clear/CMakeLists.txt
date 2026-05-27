cd_add_sample(hello_d3d12_clear
  SOURCES main.cpp
  DEPS    cd::core cd::rhi cd::rhi_d3d12
)

# Win32 GUI surface (CreateWindowExW + GetMessageW). cd_add_sample
# prefixes the target with cd_sample_; user32/gdi32 are typically
# already linked by the MSVC default libs, but be explicit so
# clang-cl / mingw cross builds also pick them up.
if(WIN32)
  target_link_libraries(cd_sample_hello_d3d12_clear PRIVATE user32 gdi32)
endif()
