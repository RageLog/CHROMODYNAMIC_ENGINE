cd_add_sample(hello_triangle
  SOURCES main.cpp
  DEPS    cd::platform cd::rhi cd::rhi_vulkan cd::shader cd::material cd::render cd::core cd::sample_common
)

# WIN32-subsystem entry isn't required: cd::platform::create_window owns the
# message pump. We stick with the console subsystem so stdout/stderr surface
# diagnostics during early bring-up.
