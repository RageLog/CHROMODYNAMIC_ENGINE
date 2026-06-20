# 80->100 marathon — GPU-gated cluster compute pipeline tests.
# Registered only when the Vulkan backend is enabled; each case GTEST_SKIPs at
# runtime on hosts without a Vulkan ICD. Drives the two-pass count/write
# pipeline against the CPU reference simulator (parity + negative paths).
# Golden-safe (no rendered-output change).
if(CD_ENABLE_VULKAN AND TARGET cd_rhi_vulkan)
  cd_add_test(cluster_gpu
    SOURCES test_cluster_gpu.cpp
    DEPS    cd::cluster_gpu cd::cluster cd::rhi cd::rhi_vulkan cd::core
  )
endif()
