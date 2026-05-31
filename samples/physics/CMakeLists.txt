# samples/physics — Phase 530 / T3.3 physics demo bundle.
#
# Five console-only samples exercising cd::physics (built-in Euler integrator,
# aligned with the cd::physics_jolt stub API from Phase 525 / W4). Each sample
# is headless and always compiled regardless of CD_ENABLE_VULKAN.
#
# Samples:
#   hello_falling_box        — dynamic box + static plane, manual bounce
#   hello_pile               — 50 dynamic balls settling with damping
#   hello_ragdoll            — 6-segment spring-joint constraint chain
#   hello_character_controller — kinematic capsule on slopes + steps
#   hello_raycast            — ray queries vs static AABBs + dynamic spheres

add_subdirectory(hello_falling_box)
add_subdirectory(hello_pile)
add_subdirectory(hello_ragdoll)
add_subdirectory(hello_character_controller)
add_subdirectory(hello_raycast)
