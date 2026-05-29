// =============================================================================
// CHROMODYNAMIC -- engine/render/rhi/tests/test_backend_parity.cpp
//
// RHI backend parity smoke: locks the descriptor / handle / enum
// surface so the Vulkan + D3D12 backends consume the EXACT SAME
// types.  No actual device instantiation -- this is a CPU-side
// surface contract test runnable in any CI lane (including the GH-
// hosted Linux runners that have no D3D12 driver).
//
// Marathon Run 28 X4 slice.  See ADR-20260529-X4-d3d12-parity-status.md.
//
// Invariants asserted:
//   * Backend enum has both Vulkan + D3D12 entries.
//   * TextureType enum covers k1D / k2D / k3D / kCube (the D3D12
//     backend currently implements only k2D + kCube; the test
//     proves the type space is complete so the gap is in
//     implementation, not the public API).
//   * QueueType enum covers kGraphics / kCompute / kTransfer.
//   * Common DescriptorType values exist (kSampledImage, kStorageImage,
//     kUniformBuffer, kStorageBuffer, kAccelerationStructure, ...).
//   * Sentinel value: AccelInstance == 64 B (must match both
//     VkAccelerationStructureInstanceKHR + D3D12_RAYTRACING_INSTANCE_DESC).
// =============================================================================
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/IDevice.hpp>

#include <gtest/gtest.h>

#include <cstdint>

namespace rhi = cd::rhi;

// ---- Backend enum --------------------------------------------------------
TEST(BackendParity, BackendEnumLists)
{
    // Confirm both Vulkan + D3D12 are integer-codable from the
    // enum class without surprises.  Stability matters because
    // create_*_device() factories return a unique_ptr<IDevice>
    // whose backend() method exposes this enum to consumers.
    EXPECT_NE(static_cast<int>(rhi::Backend::kVulkan),
              static_cast<int>(rhi::Backend::kD3D12));
}

// ---- TextureType covers full 1D/2D/3D/Cube space -------------------------
// Even though the D3D12 backend currently rejects k1D / k3D
// with kNotImplemented, the type space MUST exist at the RHI
// level so the public API is portable and the backend gap is
// visible / measurable.
TEST(BackendParity, TextureTypeFullSpaceIsExposed)
{
    EXPECT_NE(static_cast<int>(rhi::TextureType::k1D),
              static_cast<int>(rhi::TextureType::k2D));
    EXPECT_NE(static_cast<int>(rhi::TextureType::k2D),
              static_cast<int>(rhi::TextureType::k3D));
    EXPECT_NE(static_cast<int>(rhi::TextureType::k3D),
              static_cast<int>(rhi::TextureType::kCube));
}

// ---- QueueType has 3 entries ---------------------------------------------
TEST(BackendParity, QueueTypeListsGraphicsComputeTransfer)
{
    auto g = static_cast<int>(rhi::QueueType::kGraphics);
    auto c = static_cast<int>(rhi::QueueType::kCompute);
    auto t = static_cast<int>(rhi::QueueType::kTransfer);
    EXPECT_NE(g, c);
    EXPECT_NE(c, t);
    EXPECT_NE(g, t);
}

// ---- DescriptorType breadth ----------------------------------------------
// The descriptor type space must match Vulkan VK_DESCRIPTOR_TYPE_*
// values 1:1.  D3D12 maps them via the SRV / UAV / CBV / sampler
// heap split; the abstraction at the RHI tier hides that mapping.
TEST(BackendParity, DescriptorTypeBreadth)
{
    EXPECT_NE(static_cast<int>(rhi::DescriptorType::kSampler),
              static_cast<int>(rhi::DescriptorType::kSampledImage));
    EXPECT_NE(static_cast<int>(rhi::DescriptorType::kSampledImage),
              static_cast<int>(rhi::DescriptorType::kStorageImage));
    EXPECT_NE(static_cast<int>(rhi::DescriptorType::kStorageImage),
              static_cast<int>(rhi::DescriptorType::kUniformBuffer));
    EXPECT_NE(static_cast<int>(rhi::DescriptorType::kUniformBuffer),
              static_cast<int>(rhi::DescriptorType::kStorageBuffer));
    EXPECT_NE(static_cast<int>(rhi::DescriptorType::kStorageBuffer),
              static_cast<int>(rhi::DescriptorType::kAccelerationStructure));
}

// ---- AccelInstance bit-exact match for both vendor structs ---------------
// VkAccelerationStructureInstanceKHR == 64 B.
// D3D12_RAYTRACING_INSTANCE_DESC == 64 B.
// Our AccelInstance MUST exactly match so the same struct can be
// uploaded directly to either driver-side instance buffer.
TEST(BackendParity, AccelInstanceMatchesBothVendorStructs)
{
    static_assert(sizeof(rhi::AccelInstance) == 64,
                  "AccelInstance must match both Vk + D3D12 RT instance descs");
    EXPECT_EQ(sizeof(rhi::AccelInstance), 64U);
}

// ---- SbtRegion is at least 32 B ------------------------------------------
TEST(BackendParity, SbtRegionFitsBothVendorRegionDescs)
{
    static_assert(sizeof(rhi::SbtRegion) >= 32,
                  "SbtRegion must fit Vk (32 B) and DXR (24 B) region descs");
    EXPECT_GE(sizeof(rhi::SbtRegion), 32U);
}

// ---- Handle tagging stays distinct across resource kinds -----------------
// Phantom tagging on cd::core::Handle<Tag> means BufferHandle is
// not implicitly convertible to TextureHandle.  Re-asserting at
// the test level locks the contract from accidental loosening.
TEST(BackendParity, HandleTagsAreDistinctTypes)
{
    static_assert(!std::is_same_v<rhi::BufferHandle, rhi::TextureHandle>,
                  "BufferHandle and TextureHandle must be distinct phantom types");
    static_assert(!std::is_same_v<rhi::TextureHandle, rhi::SamplerHandle>,
                  "TextureHandle and SamplerHandle must be distinct phantom types");
    static_assert(!std::is_same_v<rhi::AccelStructureHandle, rhi::BufferHandle>,
                  "AccelStructureHandle and BufferHandle must be distinct phantom types");
    static_assert(!std::is_same_v<rhi::RtPipelineHandle, rhi::GraphicsPipelineHandle>,
                  "RtPipelineHandle and GraphicsPipelineHandle must be distinct phantom types");
    // No runtime EXPECT needed -- static_asserts above gate at compile.
    SUCCEED();
}

// ---- Error-domain kNotImplemented is reachable + distinct ----------------
// Both backends use kNotImplemented to surface "feature exists at
// the RHI surface but this backend has no impl yet".  The code
// value must be stable so callers can branch deterministically.
TEST(BackendParity, NotImplementedCodeIsStable)
{
    auto err = rhi::rhi_errors::make(
        rhi::rhi_errors::Code::kNotImplemented, "test");
    EXPECT_EQ(err.code, 7U);
    EXPECT_NE(err.code, static_cast<std::uint32_t>(rhi::rhi_errors::Code::kInvalidArgument));
    EXPECT_NE(err.code, static_cast<std::uint32_t>(rhi::rhi_errors::Code::kBackendInitFailed));
}
