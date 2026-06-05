cd_add_test(virtual_geometry
  SOURCES test_virtual_geometry.cpp
  DEPS    cd::virtual_geometry
)

cd_add_test(virtual_geometry_cluster_dag
  SOURCES test_cluster_dag.cpp
  DEPS    cd::virtual_geometry
)

cd_add_test(virtual_geometry_gpu_dispatch
  SOURCES test_gpu_dispatch.cpp
  DEPS    cd::virtual_geometry cd::camera cd::rhi
)
