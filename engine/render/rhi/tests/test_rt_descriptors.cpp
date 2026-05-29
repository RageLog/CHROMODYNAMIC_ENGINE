// =============================================================================
// CHROMODYNAMIC -- engine/render/rhi/tests/test_rt_descriptors.cpp
//
// CPU-side smoke test for the RT pipeline + dispatch_rays descriptor
// surface (cd::rhi::RtPipelineDesc, RtShaderEntry, RtShaderStage,
// DispatchRaysDesc, SbtRegion, AccelInstance).  No Vulkan / D3D12
// device required -- the test only constructs the descriptor POD
// types and asserts size + ABI invariants.
//
// Marathon Run 26 X6 slice: locks the X6 surface contract under
// regression so backend wiring changes (DXR, future Metal RT) catch
// breakage at compile + smoke time.
//
// Full end-to-end verification (RT pipeline create + dispatch on a
// real adapter) lives in samples/rhi/hello_rt (Phase 141).
// =============================================================================
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <span>

namespace rhi = cd::rhi;

// ---- ABI-critical size invariants -----------------------------------------
// These sizes are observed by both backends + the GPU driver via
// VkAccelerationStructureInstanceKHR (64 B); SbtRegion is mirrored to
// VkStridedDeviceAddressRegionKHR; AccelInstance MUST match the
// driver-side struct bit for bit.
TEST(RtDescriptors, AccelInstanceIs64Bytes)
{
    static_assert(sizeof(rhi::AccelInstance) == 64,
                  "AccelInstance must match VkAccelerationStructureInstanceKHR layout");
    EXPECT_EQ(sizeof(rhi::AccelInstance), 64U);
}

TEST(RtDescriptors, AccelInstanceTransformIs48Bytes)
{
    rhi::AccelInstance inst {};
    EXPECT_EQ(sizeof(inst.transform), 12U * sizeof(float));
}

TEST(RtDescriptors, SbtRegionFitsFourMembers)
{
    static_assert(sizeof(rhi::SbtRegion) >= 32,
                  "SbtRegion should fit four 8-byte members");
    EXPECT_GE(sizeof(rhi::SbtRegion), 32U);
}

// ---- RtShaderStage enum stability -----------------------------------------
// Backend code (Vulkan VK_RAY_TRACING_SHADER_GROUP_TYPE_*_KHR) maps
// integer codes 1:1 to these enum values.  Locking the numbering
// prevents a silent reordering from breaking the SBT shader-group
// classification in VulkanDevice::create_rt_pipeline.
TEST(RtDescriptors, RtShaderStageNumberingIsLocked)
{
    EXPECT_EQ(static_cast<int>(rhi::RtShaderStage::kRaygen),      0);
    EXPECT_EQ(static_cast<int>(rhi::RtShaderStage::kMiss),        1);
    EXPECT_EQ(static_cast<int>(rhi::RtShaderStage::kClosestHit),  2);
    EXPECT_EQ(static_cast<int>(rhi::RtShaderStage::kAnyHit),      3);
    EXPECT_EQ(static_cast<int>(rhi::RtShaderStage::kIntersection),4);
    EXPECT_EQ(static_cast<int>(rhi::RtShaderStage::kCallable),    5);
}

// ---- Default-construct shape ----------------------------------------------
TEST(RtDescriptors, RtPipelineDescDefaultIsEmpty)
{
    rhi::RtPipelineDesc d {};
    EXPECT_TRUE(d.shaders.empty());
}

TEST(RtDescriptors, DispatchRaysDescDefaultIsZero)
{
    rhi::DispatchRaysDesc d {};
    EXPECT_EQ(d.width,  0U);
    EXPECT_EQ(d.height, 0U);
    EXPECT_EQ(d.depth,  1U);  // 2D dispatch default; layered RT lives at z=1
}

// ---- RtShaderEntry assembles cleanly ---------------------------------------
TEST(RtDescriptors, RtShaderEntryShapeIsAssemblable)
{
    rhi::ShaderModuleHandle module { 1u, 1u, 0u };

    rhi::RtShaderEntry rg {};
    rg.stage  = rhi::RtShaderStage::kRaygen;
    rg.module = module;

    rhi::RtShaderEntry ms {};
    ms.stage  = rhi::RtShaderStage::kMiss;
    ms.module = module;

    rhi::RtShaderEntry ch {};
    ch.stage  = rhi::RtShaderStage::kClosestHit;
    ch.module = module;

    const std::array<rhi::RtShaderEntry, 3> entries { rg, ms, ch };
    rhi::RtPipelineDesc desc {};
    desc.shaders = std::span<const rhi::RtShaderEntry>(entries);

    EXPECT_EQ(desc.shaders.size(), 3U);
    EXPECT_EQ(desc.shaders[0].stage, rhi::RtShaderStage::kRaygen);
    EXPECT_EQ(desc.shaders[1].stage, rhi::RtShaderStage::kMiss);
    EXPECT_EQ(desc.shaders[2].stage, rhi::RtShaderStage::kClosestHit);
}

// ---- DispatchRaysDesc holds 3 SBT regions + dimensions --------------------
TEST(RtDescriptors, DispatchRaysDescAssemblesThreeSbtRegions)
{
    rhi::BufferHandle sbt { 7u, 1u, 0u };

    rhi::SbtRegion raygen {};
    raygen.buffer       = sbt;
    raygen.offset       = 0;
    raygen.stride_bytes = 32;
    raygen.size_bytes   = 32;

    rhi::SbtRegion miss {};
    miss.buffer       = sbt;
    miss.offset       = 64;
    miss.stride_bytes = 32;
    miss.size_bytes   = 32;

    rhi::SbtRegion hit {};
    hit.buffer       = sbt;
    hit.offset       = 128;
    hit.stride_bytes = 32;
    hit.size_bytes   = 32;

    rhi::DispatchRaysDesc d {};
    d.raygen = raygen;
    d.miss   = miss;
    d.hit    = hit;
    d.width  = 256;
    d.height = 256;
    d.depth  = 1;

    EXPECT_EQ(d.raygen.buffer.value(), sbt.value());
    EXPECT_EQ(d.miss.offset, 64U);
    EXPECT_EQ(d.hit.size_bytes, 32U);
    EXPECT_EQ(d.width, 256U);
    EXPECT_TRUE(d.callable.buffer.is_null());
}
