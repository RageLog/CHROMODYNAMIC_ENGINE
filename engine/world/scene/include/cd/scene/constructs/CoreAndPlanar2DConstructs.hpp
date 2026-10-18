// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/CoreAndPlanar2DConstructs.hpp
// Umbrella Header & Factory for 71 Standalone C++23 Construct Classes
// =============================================================================
#pragma once

#include <cd/scene/constructs/IChromaConstruct.hpp>

#include <cd/scene/constructs/core/ChromaElement.hpp>
#include <cd/scene/constructs/core/StageViewportTarget.hpp>
#include <cd/scene/constructs/core/NestedStagePortal.hpp>
#include <cd/scene/constructs/core/NativeOsWindowVessel.hpp>
#include <cd/scene/constructs/core/UiModalConfirmCapsule.hpp>
#include <cd/scene/constructs/core/UiDualChoiceModalCapsule.hpp>
#include <cd/scene/constructs/core/VfsAssetPickerModal.hpp>
#include <cd/scene/constructs/core/UiFloatingOverlayElement.hpp>
#include <cd/scene/constructs/core/UiContextCommandMenu.hpp>
#include <cd/scene/constructs/core/UiFrostedPopoverPanel.hpp>
#include <cd/scene/constructs/core/PlanarDrawElement.hpp>
#include <cd/scene/constructs/core/HudStageLayer.hpp>
#include <cd/scene/constructs/core/DspSoundEmitterCapsule.hpp>
#include <cd/scene/constructs/core/TimelinePulseSequencer.hpp>
#include <cd/scene/constructs/core/InertialAnimBlendTree.hpp>
#include <cd/scene/constructs/core/AsyncRestFetchCapsule.hpp>
#include <cd/scene/constructs/core/NetAuthorityVesselSpawner.hpp>
#include <cd/scene/constructs/core/NetDeltaStateSynchronizer.hpp>
#include <cd/scene/constructs/core/AsyncVfsPakStreamer.hpp>
#include <cd/scene/constructs/core/GluonGlobalUniformBus.hpp>
#include <cd/scene/constructs/core/DeterministicPulseTimer.hpp>
#include <cd/scene/constructs/planar2d/Planar2DElement.hpp>
#include <cd/scene/constructs/planar2d/PlanarLens2D.hpp>
#include <cd/scene/constructs/planar2d/QuadSprite2DElement.hpp>
#include <cd/scene/constructs/planar2d/FlipbookSprite2DElement.hpp>
#include <cd/scene/constructs/planar2d/PlanarCollisionElement2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarPhysicsVessel2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarHullCapsule2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarConcavePolyCapsule2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarAnchorBody2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarKinematicMover2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarKineticBody2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarAvatarVessel2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarXpbdConstraint2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarSpringDamperCapsule2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarPrismaticTrackCapsule2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarRevolutePinCapsule2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarSensorZone2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarAudioEar2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarSoundSource2D.hpp>
#include <cd/scene/constructs/planar2d/SimdPlanarSwarm2D.hpp>
#include <cd/scene/constructs/planar2d/ComputeSparkSwarm2D.hpp>
#include <cd/scene/constructs/planar2d/TileCanvas2DElement.hpp>
#include <cd/scene/constructs/planar2d/PlanarRadianceTint2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarLightBase2D.hpp>
#include <cd/scene/constructs/planar2d/RadialLight2DElement.hpp>
#include <cd/scene/constructs/planar2d/SolarLight2DElement.hpp>
#include <cd/scene/constructs/planar2d/SdfShadowOccluder2D.hpp>
#include <cd/scene/constructs/planar2d/GpuRibbonStroke2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarBeacon2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarSurfaceMesh2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarIndirectSwarmMesh2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarNavField2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarNavJumpPortal2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarRvoAvoidanceHull2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarWayfinderPilot2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarDepthStage2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarParallaxStrata2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarBezierSpline2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarSplineFollowerCapsule2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarDelaunayPolygon2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarRayProbeCapsule2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarSweepProbeCapsule2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarTransformLinkCapsule2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarBoneRig2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarJointNode2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarRagdollBone2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarFrustumSensor2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarFrustumSleepGate2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarCompositeLayer2D.hpp>
#include <cd/scene/constructs/planar2d/PlanarFrameHistoryTap2D.hpp>

namespace cd::scene
{

[[nodiscard]] inline std::unique_ptr<IChromaConstruct> instantiate_core_or_planar2d_construct(
    std::uint16_t id)
{
    switch (id)
    {
        case 0:  return std::make_unique<ChromaElement>();
        case 1:  return std::make_unique<StageViewportTarget>();
        case 2:  return std::make_unique<NestedStagePortal>();
        case 3:  return std::make_unique<NativeOsWindowVessel>();
        case 4:  return std::make_unique<UiModalConfirmCapsule>();
        case 5:  return std::make_unique<UiDualChoiceModalCapsule>();
        case 6:  return std::make_unique<VfsAssetPickerModal>();
        case 7:  return std::make_unique<UiFloatingOverlayElement>();
        case 8:  return std::make_unique<UiContextCommandMenu>();
        case 9:  return std::make_unique<UiFrostedPopoverPanel>();
        case 10: return std::make_unique<PlanarDrawElement>();
        case 11: return std::make_unique<HudStageLayer>();
        case 12: return std::make_unique<DspSoundEmitterCapsule>();
        case 13: return std::make_unique<TimelinePulseSequencer>();
        case 14: return std::make_unique<InertialAnimBlendTree>();
        case 15: return std::make_unique<AsyncRestFetchCapsule>();
        case 16: return std::make_unique<NetAuthorityVesselSpawner>();
        case 17: return std::make_unique<NetDeltaStateSynchronizer>();
        case 18: return std::make_unique<AsyncVfsPakStreamer>();
        case 19: return std::make_unique<GluonGlobalUniformBus>();
        case 20: return std::make_unique<DeterministicPulseTimer>();
        case 21: return std::make_unique<Planar2DElement>();
        case 22: return std::make_unique<PlanarLens2D>();
        case 23: return std::make_unique<QuadSprite2DElement>();
        case 24: return std::make_unique<FlipbookSprite2DElement>();
        case 25: return std::make_unique<PlanarCollisionElement2D>();
        case 26: return std::make_unique<PlanarPhysicsVessel2D>();
        case 27: return std::make_unique<PlanarHullCapsule2D>();
        case 28: return std::make_unique<PlanarConcavePolyCapsule2D>();
        case 29: return std::make_unique<PlanarAnchorBody2D>();
        case 30: return std::make_unique<PlanarKinematicMover2D>();
        case 31: return std::make_unique<PlanarKineticBody2D>();
        case 32: return std::make_unique<PlanarAvatarVessel2D>();
        case 33: return std::make_unique<PlanarXpbdConstraint2D>();
        case 34: return std::make_unique<PlanarSpringDamperCapsule2D>();
        case 35: return std::make_unique<PlanarPrismaticTrackCapsule2D>();
        case 36: return std::make_unique<PlanarRevolutePinCapsule2D>();
        case 37: return std::make_unique<PlanarSensorZone2D>();
        case 38: return std::make_unique<PlanarAudioEar2D>();
        case 39: return std::make_unique<PlanarSoundSource2D>();
        case 40: return std::make_unique<SimdPlanarSwarm2D>();
        case 41: return std::make_unique<ComputeSparkSwarm2D>();
        case 42: return std::make_unique<TileCanvas2DElement>();
        case 43: return std::make_unique<PlanarRadianceTint2D>();
        case 44: return std::make_unique<PlanarLightBase2D>();
        case 45: return std::make_unique<RadialLight2DElement>();
        case 46: return std::make_unique<SolarLight2DElement>();
        case 47: return std::make_unique<SdfShadowOccluder2D>();
        case 48: return std::make_unique<GpuRibbonStroke2D>();
        case 49: return std::make_unique<PlanarBeacon2D>();
        case 50: return std::make_unique<PlanarSurfaceMesh2D>();
        case 51: return std::make_unique<PlanarIndirectSwarmMesh2D>();
        case 52: return std::make_unique<PlanarNavField2D>();
        case 53: return std::make_unique<PlanarNavJumpPortal2D>();
        case 54: return std::make_unique<PlanarRvoAvoidanceHull2D>();
        case 55: return std::make_unique<PlanarWayfinderPilot2D>();
        case 56: return std::make_unique<PlanarDepthStage2D>();
        case 57: return std::make_unique<PlanarParallaxStrata2D>();
        case 58: return std::make_unique<PlanarBezierSpline2D>();
        case 59: return std::make_unique<PlanarSplineFollowerCapsule2D>();
        case 60: return std::make_unique<PlanarDelaunayPolygon2D>();
        case 61: return std::make_unique<PlanarRayProbeCapsule2D>();
        case 62: return std::make_unique<PlanarSweepProbeCapsule2D>();
        case 63: return std::make_unique<PlanarTransformLinkCapsule2D>();
        case 64: return std::make_unique<PlanarBoneRig2D>();
        case 65: return std::make_unique<PlanarJointNode2D>();
        case 66: return std::make_unique<PlanarRagdollBone2D>();
        case 67: return std::make_unique<PlanarFrustumSensor2D>();
        case 68: return std::make_unique<PlanarFrustumSleepGate2D>();
        case 69: return std::make_unique<PlanarCompositeLayer2D>();
        case 70: return std::make_unique<PlanarFrameHistoryTap2D>();
        default: return nullptr;
    }
}

} // namespace cd::scene
