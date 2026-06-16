// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_d3d12_blas_indexed.cpp
//
// C-D3D12-FIXES / D-BLAS-INDEXED: the D3D12 BLAS build honors the geometry's
// index buffer vs the Vulkan reference.
//
// BACKGROUND. The BLAS geometry-build path historically hardcoded
//   g.Triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;
//   g.Triangles.IndexBuffer = 0;
// so the AccelTriangleGeometry's index_buffer / index_offset / index_count were
// DROPPED — every indexed BLAS was silently built as NON-indexed (the BLAS-geo-
// cap silent-truncation bug class). The Vulkan reference maps
// IndexType -> VkIndexType and feeds indexData.deviceAddress
// (VulkanDevice.cpp:4022). The fix resolves the index buffer GPU-VA, maps
// kUInt16/kUInt32 -> R16_UINT/R32_UINT, and sets IndexCount.
//
// WHAT THIS TEST PROVES (real WARP/hardware DXR; descriptor-level, no full ray
// trace needed):
//   * BIDIRECTIONAL: an indexed geometry whose index_count > 0 but whose
//     index_buffer handle is INVALID must be REJECTED (kInvalidArgument). That
//     rejection EXISTS ONLY because the fix now LOOKS UP the index buffer; the
//     pre-fix code never touched index_buffer, so it built the BLAS fine
//     (dropping the indices). So this assertion FLIPS on temp-revert — the
//     decisive proof the index path is wired.
//   * POSITIVE: a fully-valid indexed BLAS (real index buffer + count + type)
//     builds successfully — the fix accepts the indexed path it now honors.
//
// Honest-SKIP when no D3D12 adapter or the adapter lacks DXR (features()
// .ray_tracing == false). Pattern: Arrange/Act/Assert.
// =============================================================================
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
#endif

#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/IDevice.hpp>
#if defined(_WIN32)
    #include <cd/rhi/d3d12/D3D12Device.hpp>
#endif

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>

#if defined(_WIN32)

namespace
{

[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> make_d3d12_device_or_null()
{
    cd::rhi::d3d12::D3D12CreateInfo ci {};
    ci.enable_validation = false;
    auto r = cd::rhi::d3d12::create_d3d12_device(ci);
    if (!r.has_value())
        return nullptr;
    return std::move(*r);
}

[[nodiscard]] cd::rhi::BufferHandle
make_buffer(cd::rhi::IDevice& dev, std::uint64_t bytes, cd::rhi::BufferUsage usage)
{
    cd::rhi::BufferDesc bd {};
    bd.size   = bytes;
    bd.usage  = usage;
    bd.memory = cd::rhi::MemoryUsage::kGpuOnly;
    auto r = dev.create_buffer(bd);
    return r.has_value() ? *r : cd::rhi::BufferHandle {};
}

}  // namespace

// ---- D-BLAS-INDEXED (bidirectional): an invalid index buffer is REJECTED ----
TEST(D3D12BlasIndexed, IndexedGeometryWithBadIndexBufferIsRejected)
{
    auto dev = make_d3d12_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "No D3D12 adapter (WARP/hardware) on this host";
    auto& d = *dev;
    if (!d.features().ray_tracing)
        GTEST_SKIP() << "adapter lacks DXR (ray_tracing feature off)";

    // A real vertex buffer (3 verts * 12 bytes) but a DELIBERATELY UNKNOWN
    // index buffer handle with index_count > 0. The handle is non-null (so
    // is_valid() is true and the index path engages) but was never created,
    // so the fixed build's `buffers_.find` lookup fails -> kInvalidArgument.
    // The pre-fix code never touched index_buffer at all, so it would build
    // the BLAS fine (dropping the indices) -> the assertion flips on revert.
    const auto vb = make_buffer(d, std::uint64_t { 3u } * 12u, cd::rhi::BufferUsage::kVertex);
    ASSERT_TRUE(vb.is_valid());

    cd::rhi::AccelTriangleGeometry geo {};
    geo.vertex_buffer = vb;
    geo.vertex_count  = 3;
    geo.vertex_stride = 12;
    geo.index_buffer  = cd::rhi::BufferHandle { 0x7FFFFFFFu, 1u };  // unknown
    geo.index_count   = 3;                          // > 0 → index path engaged
    geo.index_type    = cd::rhi::IndexType::kUInt32;

    const std::array<cd::rhi::AccelTriangleGeometry, 1> geos { geo };
    cd::rhi::AccelStructureDesc asd {};
    asd.kind      = cd::rhi::AccelStructureKind::kBottomLevel;
    asd.triangles = std::span<const cd::rhi::AccelTriangleGeometry>(geos.data(), 1);

    auto r = d.create_acceleration_structure(asd);
    EXPECT_FALSE(r.has_value())
        << "BUG: an indexed BLAS with index_count>0 and an INVALID index buffer "
           "was ACCEPTED — the build is not looking up index_buffer, i.e. it is "
           "DROPPING the index data (the silent-truncation bug Vulkan does not "
           "have).";
    if (r.has_value())
        d.destroy_acceleration_structure(*r);

    d.destroy_buffer(vb);
}

// ---- D-BLAS-INDEXED (positive): a fully-valid indexed BLAS builds -----------
TEST(D3D12BlasIndexed, ValidIndexedBlasBuilds)
{
    auto dev = make_d3d12_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "No D3D12 adapter (WARP/hardware) on this host";
    auto& d = *dev;
    if (!d.features().ray_tracing)
        GTEST_SKIP() << "adapter lacks DXR (ray_tracing feature off)";

    const auto vb = make_buffer(d, std::uint64_t { 4u } * 12u, cd::rhi::BufferUsage::kVertex);
    const auto ib = make_buffer(d, std::uint64_t { 6u } * 4u, cd::rhi::BufferUsage::kIndex);
    ASSERT_TRUE(vb.is_valid());
    ASSERT_TRUE(ib.is_valid());

    cd::rhi::AccelTriangleGeometry geo {};
    geo.vertex_buffer = vb;
    geo.vertex_count  = 4;
    geo.vertex_stride = 12;
    geo.index_buffer  = ib;
    geo.index_offset  = 0;
    geo.index_count   = 6;  // two triangles
    geo.index_type    = cd::rhi::IndexType::kUInt32;

    const std::array<cd::rhi::AccelTriangleGeometry, 1> geos { geo };
    cd::rhi::AccelStructureDesc asd {};
    asd.kind      = cd::rhi::AccelStructureKind::kBottomLevel;
    asd.triangles = std::span<const cd::rhi::AccelTriangleGeometry>(geos.data(), 1);

    auto r = d.create_acceleration_structure(asd);
    EXPECT_TRUE(r.has_value())
        << "a valid indexed BLAS (real index buffer + count + type) must build: "
        << (r.has_value() ? std::string {}
                          : std::string(r.error().message.begin(),
                                        r.error().message.end()));
    if (r.has_value())
        d.destroy_acceleration_structure(*r);

    d.destroy_buffer(vb);
    d.destroy_buffer(ib);
}

#endif  // _WIN32
