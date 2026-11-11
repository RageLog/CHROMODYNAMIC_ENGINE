// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/core/StudioState.cpp
// =============================================================================
#include "core/StudioState.hpp"

#include <utility>

namespace cd::studio
{

StudioEngineState::StudioEngineState(StudioConfig cfg)
    : config(std::move(cfg))
{
}

StudioEngineState::~StudioEngineState() = default;

void StudioEngineState::push_log(std::string msg)
{
    log.emplace_back(std::move(msg));
    while (log.size() > 128U)
    {
        log.pop_front();
    }
}

const cd::render::GpuMesh& StudioEngineState::mesh_for(PrimitiveKind k) const noexcept
{
    switch (k)
    {
        case PrimitiveKind::kSphere:    return meshes.sphere;
        case PrimitiveKind::kCone:      return meshes.cone;
        case PrimitiveKind::kCylinder:  return meshes.cyl;
        case PrimitiveKind::kTorus:     return meshes.torus;
        case PrimitiveKind::kTorusKnot: return meshes.knot;
        case PrimitiveKind::kCustomGltf:
            return custom_gltf_mesh.vb.is_valid() ? custom_gltf_mesh : meshes.knot;
        case PrimitiveKind::kSponza:
            return meshes.gltf.vb.is_valid() ? meshes.gltf : meshes.knot;
        case PrimitiveKind::kGltf:
            return meshes.gltf_cesium.vb.is_valid() ? meshes.gltf_cesium : meshes.knot;
        case PrimitiveKind::kCube:
            return meshes.cube;
    }
    return meshes.cube;
}

cd::rhi::AccelStructureHandle StudioEngineState::blas_for_kind(PrimitiveKind k) const noexcept
{
    switch (k)
    {
        case PrimitiveKind::kSphere:     return meshes.blas_sphere;
        case PrimitiveKind::kCone:       return meshes.blas_cone;
        case PrimitiveKind::kCylinder:   return meshes.blas_cyl;
        case PrimitiveKind::kTorus:      return meshes.blas_torus;
        case PrimitiveKind::kTorusKnot:  return meshes.blas_knot;
        case PrimitiveKind::kCustomGltf:
            return blas_custom_gltf.is_valid() ? blas_custom_gltf : meshes.blas_knot;
        case PrimitiveKind::kSponza:     return meshes.blas_gltf;
        case PrimitiveKind::kGltf:       return meshes.blas_cesium;
        case PrimitiveKind::kCube:       return meshes.blas_cube;
    }
    return meshes.blas_cube;
}

std::span<const cd_studio::GltfPrimRange> StudioEngineState::prim_ranges_for(PrimitiveKind k) const noexcept
{
    if (k == PrimitiveKind::kSponza) return meshes.gltf_prim_ranges;
    if (k == PrimitiveKind::kGltf)   return meshes.gltf_cesium_prim_ranges;
    return {};
}

}  // namespace cd::studio
