// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/Spatial3DVesselPhysicsAndGiConstructs.hpp
// Umbrella Header & Factory for 44 Standalone C++23 Construct Classes
// =============================================================================
#pragma once

#include <cd/scene/constructs/IChromaConstruct.hpp>

#include <cd/scene/constructs/spatial3d/KinematicMoverElement3D.hpp>
#include <cd/scene/constructs/spatial3d/KineticBody3D.hpp>
#include <cd/scene/constructs/spatial3d/AvatarVessel3D.hpp>
#include <cd/scene/constructs/spatial3d/SensorVolume3D.hpp>
#include <cd/scene/constructs/spatial3d/CameraLens3D.hpp>
#include <cd/scene/constructs/spatial3d/SpringArmCapsule3D.hpp>
#include <cd/scene/constructs/spatial3d/RoverVessel3D.hpp>
#include <cd/scene/constructs/spatial3d/SuspensionWheelCapsule3D.hpp>
#include <cd/scene/constructs/spatial3d/XpbdConstraintBase3D.hpp>
#include <cd/scene/constructs/spatial3d/RevoluteHingeCapsule3D.hpp>
#include <cd/scene/constructs/spatial3d/RagdollConeTwistCapsule3D.hpp>
#include <cd/scene/constructs/spatial3d/PrismaticPistonCapsule3D.hpp>
#include <cd/scene/constructs/spatial3d/BallSocketPinCapsule3D.hpp>
#include <cd/scene/constructs/spatial3d/SixDofMotorConstraint3D.hpp>
#include <cd/scene/constructs/spatial3d/RiggedSkeleton3D.hpp>
#include <cd/scene/constructs/spatial3d/NestedBoneSocket3D.hpp>
#include <cd/scene/constructs/spatial3d/RagdollBoneLink3D.hpp>
#include <cd/scene/constructs/spatial3d/AnalyticalTwoBoneIkCapsule3D.hpp>
#include <cd/scene/constructs/spatial3d/RootTrajectoryVisualizer3D.hpp>
#include <cd/scene/constructs/spatial3d/IrradianceField3D.hpp>
#include <cd/scene/constructs/spatial3d/GpuBakedLightmapField3D.hpp>
#include <cd/scene/constructs/spatial3d/SphericalHarmonicProbe3D.hpp>
#include <cd/scene/constructs/spatial3d/RadianceProbe3D.hpp>
#include <cd/scene/constructs/spatial3d/HiZVisibilitySensor3D.hpp>
#include <cd/scene/constructs/spatial3d/HiZSimulationGateCapsule3D.hpp>
#include <cd/scene/constructs/spatial3d/VoxelTilePaletteMap3D.hpp>
#include <cd/scene/constructs/spatial3d/AudioEar3D.hpp>
#include <cd/scene/constructs/spatial3d/SoundSource3D.hpp>
#include <cd/scene/constructs/spatial3d/NavMeshZone3D.hpp>
#include <cd/scene/constructs/spatial3d/DynamicRvoObstacle3D.hpp>
#include <cd/scene/constructs/spatial3d/WayfinderAgent3D.hpp>
#include <cd/scene/constructs/spatial3d/OffMeshTraversalLink3D.hpp>
#include <cd/scene/constructs/spatial3d/HiZOccluderHull3D.hpp>
#include <cd/scene/constructs/spatial3d/SplineTrack3D.hpp>
#include <cd/scene/constructs/spatial3d/SplineCarriageCapsule3D.hpp>
#include <cd/scene/constructs/spatial3d/RayProbe3D.hpp>
#include <cd/scene/constructs/spatial3d/ConvexSweepProbe3D.hpp>
#include <cd/scene/constructs/spatial3d/TransformRelayCapsule3D.hpp>
#include <cd/scene/constructs/spatial3d/OpenXrSpatialElement3D.hpp>
#include <cd/scene/constructs/spatial3d/OpenXrStereoLens3D.hpp>
#include <cd/scene/constructs/spatial3d/OpenXrStageOrigin3D.hpp>
#include <cd/scene/constructs/spatial3d/OpenXrHandPilot3D.hpp>
#include <cd/scene/constructs/spatial3d/OpenXrSpatialAnchor3D.hpp>
#include <cd/scene/constructs/spatial3d/RagdollChain3D.hpp>

namespace cd::scene
{

[[nodiscard]] inline std::unique_ptr<IChromaConstruct> instantiate_spatial3d_part_b_construct(
    std::uint16_t id)
{
    switch (id)
    {
        case 115: return std::make_unique<KinematicMoverElement3D>();
        case 116: return std::make_unique<KineticBody3D>();
        case 117: return std::make_unique<AvatarVessel3D>();
        case 118: return std::make_unique<SensorVolume3D>();
        case 119: return std::make_unique<CameraLens3D>();
        case 120: return std::make_unique<SpringArmCapsule3D>();
        case 121: return std::make_unique<RoverVessel3D>();
        case 122: return std::make_unique<SuspensionWheelCapsule3D>();
        case 123: return std::make_unique<XpbdConstraintBase3D>();
        case 124: return std::make_unique<RevoluteHingeCapsule3D>();
        case 125: return std::make_unique<RagdollConeTwistCapsule3D>();
        case 126: return std::make_unique<PrismaticPistonCapsule3D>();
        case 127: return std::make_unique<BallSocketPinCapsule3D>();
        case 128: return std::make_unique<SixDofMotorConstraint3D>();
        case 129: return std::make_unique<RiggedSkeleton3D>();
        case 130: return std::make_unique<NestedBoneSocket3D>();
        case 131: return std::make_unique<RagdollBoneLink3D>();
        case 132: return std::make_unique<AnalyticalTwoBoneIkCapsule3D>();
        case 133: return std::make_unique<RootTrajectoryVisualizer3D>();
        case 134: return std::make_unique<IrradianceField3D>();
        case 135: return std::make_unique<GpuBakedLightmapField3D>();
        case 136: return std::make_unique<SphericalHarmonicProbe3D>();
        case 137: return std::make_unique<RadianceProbe3D>();
        case 138: return std::make_unique<HiZVisibilitySensor3D>();
        case 139: return std::make_unique<HiZSimulationGateCapsule3D>();
        case 140: return std::make_unique<VoxelTilePaletteMap3D>();
        case 141: return std::make_unique<AudioEar3D>();
        case 142: return std::make_unique<SoundSource3D>();
        case 143: return std::make_unique<NavMeshZone3D>();
        case 144: return std::make_unique<DynamicRvoObstacle3D>();
        case 145: return std::make_unique<WayfinderAgent3D>();
        case 146: return std::make_unique<OffMeshTraversalLink3D>();
        case 147: return std::make_unique<HiZOccluderHull3D>();
        case 148: return std::make_unique<SplineTrack3D>();
        case 149: return std::make_unique<SplineCarriageCapsule3D>();
        case 150: return std::make_unique<RayProbe3D>();
        case 151: return std::make_unique<ConvexSweepProbe3D>();
        case 152: return std::make_unique<TransformRelayCapsule3D>();
        case 153: return std::make_unique<OpenXrSpatialElement3D>();
        case 154: return std::make_unique<OpenXrStereoLens3D>();
        case 155: return std::make_unique<OpenXrStageOrigin3D>();
        case 156: return std::make_unique<OpenXrHandPilot3D>();
        case 157: return std::make_unique<OpenXrSpatialAnchor3D>();
        case 158: return std::make_unique<RagdollChain3D>();
        default:  return nullptr;
    }
}

} // namespace cd::scene
