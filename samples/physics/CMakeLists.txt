# samples/physics — Phase 530 / T3.3 physics demo bundle (retired).
#
# phase912-sample-consolidation: per
# [[feedback-sample-consolidation-and-per-feature-demo]], all 5 headless
# physics console smokes have been folded into the source libraries'
# gtests. Source directories deleted in this commit.
#
# Retired in phase 912:
#   * hello_falling_box        -> engine/world/physics/tests/test_physics.cpp
#       (GravityIntegratesDynamicBody + ImpulseChangesVelocityWithMassInverse
#        + StaticBodyIgnoresImpulse) + engine/world/physics_jolt/tests/
#       test_jolt_world.cpp (StaticBoxAndDynamicBallFallsUnderGravity).
#   * hello_pile               -> engine/world/physics/tests/test_physics.cpp
#       (LinearDampingReducesVelocityOverTime + GravityIntegratesDynamicBody
#        x N) + JoltWorld.TwoBodiesWithRestitutionExchangeImpulse.
#   * hello_ragdoll            -> engine/world/physics/tests/test_physics.cpp
#       (SpringJoint.DefaultStiffnessSane) + JoltWorld.HingeJointRegistration*.
#       NOTE: sample's manual enforce_spring impulse pattern is documented
#       only inline -- if a future user needs the multi-body spring-chain
#       recipe, lift it into engine/world/physics/docs/ or a hello_engine
#       physics-demo panel (per feature-demo half of the consolidation rule).
#   * hello_character_controller -> JoltWorld.KinematicCapsuleStaysGroundedOnFlatStep
#       + Physics.KinematicBodyMovesByVelocityNotByImpulse.
#   * hello_raycast            -> engine/world/physics/tests/test_physics.cpp
#       (Ray.IntersectsAabbFromOutside / .MissesAabb / .InsideAabbReturnsZero,
#        plus RaySphere.HitsSphereInFront / .MissesWhenOffset / .BehindOriginReturnsNullopt
#        / .InsideSphereReturnsZero).
#
# samples/physics/ is now empty (CMakeLists header documents retirements).
