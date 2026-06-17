# Band-6 to-100: cd::ibl_gpu previously shipped 0 tests. This suite verifies
# the host-side pure helpers (float_to_half / byte-layout / mip-count) always
# and the RHI upload path end-to-end behind a Vulkan device gate (GTEST_SKIPs
# at runtime when no ICD is present, mirroring restir_di_dispatch) so a host
# without a GPU stays green.
if(CD_ENABLE_VULKAN AND TARGET cd_rhi_vulkan)
  cd_add_test(ibl_gpu
    SOURCES test_ibl_gpu.cpp
    DEPS    cd::ibl_gpu cd::ibl cd::rhi_vulkan
  )
endif()
