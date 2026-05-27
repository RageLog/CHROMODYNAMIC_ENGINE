cd_add_sample(hello_d3d12_triangle
  SOURCES main.cpp
  DEPS    cd::core cd::math cd::rhi cd::rhi_d3d12
)

if(WIN32)
  target_link_libraries(cd_sample_hello_d3d12_triangle PRIVATE user32 gdi32)
endif()
