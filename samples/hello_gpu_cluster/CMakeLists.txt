cd_add_sample(hello_gpu_cluster
  SOURCES main.cpp
  DEPS    cd::cluster_gpu cd::cluster cd::rhi_vulkan cd::rhi cd::math cd::core
)
