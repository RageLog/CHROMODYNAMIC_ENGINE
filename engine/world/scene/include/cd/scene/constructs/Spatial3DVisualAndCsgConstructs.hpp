// =============================================================================
// CHROMODYNAMIC — cd/scene/constructs/Spatial3DVisualAndCsgConstructs.hpp
// Umbrella Header & Factory for 44 Standalone C++23 Construct Classes
// =============================================================================
#pragma once

#include <cd/scene/constructs/IChromaConstruct.hpp>

#include <cd/scene/constructs/spatial3d/Spatial3DElement.hpp>
#include <cd/scene/constructs/spatial3d/SpawnBeacon3D.hpp>
#include <cd/scene/constructs/spatial3d/VisualRadianceElement3D.hpp>
#include <cd/scene/constructs/spatial3d/RenderableSurfaceElement3D.hpp>
#include <cd/scene/constructs/spatial3d/SurfaceMesh3D.hpp>
#include <cd/scene/constructs/spatial3d/SwarmMesh3D.hpp>
#include <cd/scene/constructs/spatial3d/CookedAssetMesh3D.hpp>
#include <cd/scene/constructs/spatial3d/WorldMsdfLabel3D.hpp>
#include <cd/scene/constructs/spatial3d/ProjectionDecal3D.hpp>
#include <cd/scene/constructs/spatial3d/BillboardQuadBase3D.hpp>
#include <cd/scene/constructs/spatial3d/BillboardSprite3D.hpp>
#include <cd/scene/constructs/spatial3d/BillboardFlipbook3D.hpp>
#include <cd/scene/constructs/spatial3d/OpticsAndAetherStage3D.hpp>
#include <cd/scene/constructs/spatial3d/FogPocket3D.hpp>
#include <cd/scene/constructs/spatial3d/SparkSwarm3D.hpp>
#include <cd/scene/constructs/spatial3d/DeterministicSimdSwarm3D.hpp>
#include <cd/scene/constructs/spatial3d/SwarmForceFieldBase3D.hpp>
#include <cd/scene/constructs/spatial3d/SwarmBoxVortexField3D.hpp>
#include <cd/scene/constructs/spatial3d/SwarmGravityWell3D.hpp>
#include <cd/scene/constructs/spatial3d/SwarmCurlNoiseField3D.hpp>
#include <cd/scene/constructs/spatial3d/SwarmColliderBase3D.hpp>
#include <cd/scene/constructs/spatial3d/SwarmObbDeflector3D.hpp>
#include <cd/scene/constructs/spatial3d/SwarmSphereDeflector3D.hpp>
#include <cd/scene/constructs/spatial3d/SwarmSdfVolumeCollider3D.hpp>
#include <cd/scene/constructs/spatial3d/SwarmDepthTerrainCollider3D.hpp>
#include <cd/scene/constructs/spatial3d/RadianceEmitterBase3D.hpp>
#include <cd/scene/constructs/spatial3d/RadialLight3D.hpp>
#include <cd/scene/constructs/spatial3d/ConeLight3D.hpp>
#include <cd/scene/constructs/spatial3d/SolarLight3D.hpp>
#include <cd/scene/constructs/spatial3d/BooleanSolidBase3D.hpp>
#include <cd/scene/constructs/spatial3d/BooleanPrimitive3D.hpp>
#include <cd/scene/constructs/spatial3d/BooleanBoxSolid3D.hpp>
#include <cd/scene/constructs/spatial3d/BooleanSphereSolid3D.hpp>
#include <cd/scene/constructs/spatial3d/BooleanCylinderSolid3D.hpp>
#include <cd/scene/constructs/spatial3d/BooleanTorusSolid3D.hpp>
#include <cd/scene/constructs/spatial3d/BooleanExtrudeSweep3D.hpp>
#include <cd/scene/constructs/spatial3d/BooleanCustomMesh3D.hpp>
#include <cd/scene/constructs/spatial3d/NestedBooleanAssembly3D.hpp>
#include <cd/scene/constructs/spatial3d/ClothSurface3D.hpp>
#include <cd/scene/constructs/spatial3d/SpatialCollisionElement3D.hpp>
#include <cd/scene/constructs/spatial3d/PhysicalVesselBase3D.hpp>
#include <cd/scene/constructs/spatial3d/CollisionHullCapsule3D.hpp>
#include <cd/scene/constructs/spatial3d/ConcavePolygonHullCapsule3D.hpp>
#include <cd/scene/constructs/spatial3d/AnchorBody3D.hpp>

namespace cd::scene
{

[[nodiscard]] inline std::unique_ptr<IChromaConstruct> instantiate_spatial3d_part_a_construct(
    std::uint16_t id)
{
    switch (id)
    {
        case 71:  return std::make_unique<Spatial3DElement>();
        case 72:  return std::make_unique<SpawnBeacon3D>();
        case 73:  return std::make_unique<VisualRadianceElement3D>();
        case 74:  return std::make_unique<RenderableSurfaceElement3D>();
        case 75:  return std::make_unique<SurfaceMesh3D>();
        case 76:  return std::make_unique<SwarmMesh3D>();
        case 77:  return std::make_unique<CookedAssetMesh3D>();
        case 78:  return std::make_unique<WorldMsdfLabel3D>();
        case 79:  return std::make_unique<ProjectionDecal3D>();
        case 80:  return std::make_unique<BillboardQuadBase3D>();
        case 81:  return std::make_unique<BillboardSprite3D>();
        case 82:  return std::make_unique<BillboardFlipbook3D>();
        case 83:  return std::make_unique<OpticsAndAetherStage3D>();
        case 84:  return std::make_unique<FogPocket3D>();
        case 85:  return std::make_unique<SparkSwarm3D>();
        case 86:  return std::make_unique<DeterministicSimdSwarm3D>();
        case 87:  return std::make_unique<SwarmForceFieldBase3D>();
        case 88:  return std::make_unique<SwarmBoxVortexField3D>();
        case 89:  return std::make_unique<SwarmGravityWell3D>();
        case 90:  return std::make_unique<SwarmCurlNoiseField3D>();
        case 91:  return std::make_unique<SwarmColliderBase3D>();
        case 92:  return std::make_unique<SwarmObbDeflector3D>();
        case 93:  return std::make_unique<SwarmSphereDeflector3D>();
        case 94:  return std::make_unique<SwarmSdfVolumeCollider3D>();
        case 95:  return std::make_unique<SwarmDepthTerrainCollider3D>();
        case 96:  return std::make_unique<RadianceEmitterBase3D>();
        case 97:  return std::make_unique<RadialLight3D>();
        case 98:  return std::make_unique<ConeLight3D>();
        case 99:  return std::make_unique<SolarLight3D>();
        case 100: return std::make_unique<BooleanSolidBase3D>();
        case 101: return std::make_unique<BooleanPrimitive3D>();
        case 102: return std::make_unique<BooleanBoxSolid3D>();
        case 103: return std::make_unique<BooleanSphereSolid3D>();
        case 104: return std::make_unique<BooleanCylinderSolid3D>();
        case 105: return std::make_unique<BooleanTorusSolid3D>();
        case 106: return std::make_unique<BooleanExtrudeSweep3D>();
        case 107: return std::make_unique<BooleanCustomMesh3D>();
        case 108: return std::make_unique<NestedBooleanAssembly3D>();
        case 109: return std::make_unique<ClothSurface3D>();
        case 110: return std::make_unique<SpatialCollisionElement3D>();
        case 111: return std::make_unique<PhysicalVesselBase3D>();
        case 112: return std::make_unique<CollisionHullCapsule3D>();
        case 113: return std::make_unique<ConcavePolygonHullCapsule3D>();
        case 114: return std::make_unique<AnchorBody3D>();
        default:  return nullptr;
    }
}

} // namespace cd::scene
