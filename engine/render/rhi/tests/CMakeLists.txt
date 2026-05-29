cd_add_test(rhi
  SOURCES test_rhi.cpp
  DEPS    cd::rhi cd::core
)

# Marathon Run 26 X6 slice: lock the RT descriptor ABI surface.
cd_add_test(rt_descriptors
  SOURCES test_rt_descriptors.cpp
  DEPS    cd::rhi cd::core
)

# Marathon Run 28 X4 slice: lock the Vulkan + D3D12 parity surface.
cd_add_test(backend_parity
  SOURCES test_backend_parity.cpp
  DEPS    cd::rhi cd::core
)
