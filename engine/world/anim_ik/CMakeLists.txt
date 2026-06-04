# engine/world/anim_ik — Phase 727
# cd::animation::ik — CCD Inverse Kinematics solver (Sprint-2).
#
# Sidecar relationship:
#   cd::anim           — FK clip playback: skeleton + animation + pose + LBS.
#   cd::animation::ik  — WHERE joints point: CCD IK (different responsibility).
#
# Sprint-1 (phase 702): pure CCD, no joint angle constraints.  Single-threaded.
# Sprint-2 (phase 727): JointLimits per-joint Euler clamp + JointLimitPresets factory.
# Sprint-3 queue: FABRIK alternative solver + multi-chain + swing/twist decomposition.
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
