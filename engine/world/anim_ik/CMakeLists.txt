# engine/world/anim_ik — Phase 702
# cd::animation::ik — CCD Inverse Kinematics solver (Sprint-1).
#
# Sidecar relationship:
#   cd::anim           — FK clip playback: skeleton + animation + pose + LBS.
#   cd::animation::ik  — WHERE joints point: CCD IK (different responsibility).
#
# Sprint-1: pure CCD, no joint angle constraints.  Single-threaded.
# Sprint-2 queue: per-joint swing/twist limits + FABRIK alternative.
#
# No dependency on cd::anim — pure math sidecar.  No OS API calls.
cd_add_library(anim_ik
  TYPE STATIC
  SOURCES
    src/Ik.cpp
  PUBLIC_DEPS cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
