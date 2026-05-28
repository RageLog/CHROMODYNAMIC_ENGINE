// =============================================================================
// CHROMODYNAMIC - cd/render/MeshUpload.hpp
//
// Phase 290 (Marathon Run 7 extract pass) - GPU mesh holder + upload /
// destroy helpers extracted from samples/engine/hello_engine/main.cpp.
//
// `GpuMesh` is a tiny POD wrapping a vertex + index buffer handle pair and
// the counts needed by the BLAS builder. `upload_mesh` writes a CPU-side
// `(vertices, indices)` pair into freshly-allocated buffers with the usage
// flags every marathon sample wants (kVertex | kStorage | kTransferDst for
// the vb so the raytracing BLAS builder can read positions; kIndex |
// kStorage | kTransferDst for the ib for the same reason).
//
// The helper is a function template so cd::render does not have to depend
// on cd::asset for `PrimitiveMesh` / `PrimitiveVertex`. Any mesh struct
// exposing `.vertices` (contiguous, trivially copyable) and `.indices`
// (contiguous, trivially copyable) works.
//
// Header-only on purpose: zero TU cost, one-and-done per sample.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/Handles.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace cd::render
{

// =============================================================================
// GpuMesh
// =============================================================================
//
// Owns a vertex + index buffer handle pair sized for the marathon raster +
// raytracing paths. Lifetime is managed by the caller; call `destroy_mesh`
// at shutdown.
// =============================================================================
struct GpuMesh
{
    cd::rhi::BufferHandle vb {};
    cd::rhi::BufferHandle ib {};
    std::uint32_t         vertex_count { 0 };  ///< BLAS reads positions from vb
    std::uint32_t         index_count  { 0 };
};

// =============================================================================
// upload_mesh
// =============================================================================
//
// Allocate vb + ib and copy vertices / indices into them. Returns an
// empty `GpuMesh` (default-constructed handles) on any failure so the
// caller can detect via `vb.is_valid()`. `MeshT` must expose `.vertices`
// (a contiguous container with `.data()` / `.size()`) and `.indices`
// (likewise; typically std::uint16_t element type).
//
// Buffer usage:
//   vb : kVertex | kStorage | kTransferDst   (Storage required for BLAS)
//   ib : kIndex  | kStorage | kTransferDst   (Storage required for BLAS)
//   memory : kCpuToGpu (host-visible upload, marathon-friendly default)
// =============================================================================
template <typename MeshT>
[[nodiscard]] GpuMesh upload_mesh(cd::rhi::IDevice& dev, const MeshT& m)
{
    using VertexT = std::remove_cvref_t<decltype(*m.vertices.data())>;
    using IndexT  = std::remove_cvref_t<decltype(*m.indices.data())>;

    GpuMesh out {};

    cd::rhi::BufferDesc vbd {};
    vbd.size  = m.vertices.size() * sizeof(VertexT);
    vbd.usage = cd::rhi::BufferUsage::kVertex
              | cd::rhi::BufferUsage::kStorage
              | cd::rhi::BufferUsage::kTransferDst;
    vbd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto vb_r = dev.create_buffer(vbd);
    if (!vb_r.has_value()) return out;
    (void)dev.upload_buffer(
        *vb_r, 0,
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(m.vertices.data()), vbd.size));

    cd::rhi::BufferDesc ibd {};
    ibd.size  = m.indices.size() * sizeof(IndexT);
    ibd.usage = cd::rhi::BufferUsage::kIndex
              | cd::rhi::BufferUsage::kStorage
              | cd::rhi::BufferUsage::kTransferDst;
    ibd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto ib_r = dev.create_buffer(ibd);
    if (!ib_r.has_value())
    {
        dev.destroy_buffer(*vb_r);
        return out;
    }
    (void)dev.upload_buffer(
        *ib_r, 0,
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(m.indices.data()), ibd.size));

    out.vb           = *vb_r;
    out.ib           = *ib_r;
    out.vertex_count = static_cast<std::uint32_t>(m.vertices.size());
    out.index_count  = static_cast<std::uint32_t>(m.indices.size());
    return out;
}

// =============================================================================
// destroy_mesh
// =============================================================================
//
// Releases vb + ib (each only if valid) and resets `m` to the
// default-constructed state. Idempotent.
// =============================================================================
inline void destroy_mesh(cd::rhi::IDevice& dev, GpuMesh& m) noexcept
{
    if (m.vb.is_valid()) dev.destroy_buffer(m.vb);
    if (m.ib.is_valid()) dev.destroy_buffer(m.ib);
    m = {};
}

}  // namespace cd::render
