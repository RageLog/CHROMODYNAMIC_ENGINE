# samples/lib_smokes — CPU-reference parity smokes for the queued
# render libs (Phase 232-233). Each sample exercises the library's
# public API + verifies the same math the GPU compute kernel runs at
# scale. Pattern is borrowed from hello_compute_cluster.
add_subdirectory(hello_volumetric_fog)
add_subdirectory(hello_volumetric_clouds)
add_subdirectory(hello_gpu_particles)
add_subdirectory(hello_decals)
add_subdirectory(hello_ddgi)
add_subdirectory(hello_restir)
add_subdirectory(hello_mesh_shader)
add_subdirectory(hello_nrc)
add_subdirectory(hello_virtual_geometry)
add_subdirectory(hello_virtual_textures)
