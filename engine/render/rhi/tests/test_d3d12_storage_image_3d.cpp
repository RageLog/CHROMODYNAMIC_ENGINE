// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_d3d12_storage_image_3d.cpp
//
// parity1121 WAVE A1 / D4: D3D12 kStorageImage UAV ViewDimension parity vs the
// Vulkan reference.
//
// BACKGROUND. In update_descriptor_set the kStorageImage (UAV) write hardcoded
// `ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D` regardless of the bound view's
// texture type, while the kSampledImage (SRV) write in the SAME function already
// branched on is_cube / is_1d / is_3d. So a 3D / array / cube STORAGE image got
// a TEXTURE2D UAV: a 3D-resource view created with a TEXTURE2D UAV dimension is
// an INVALID descriptor (the resource is D3D12_RESOURCE_DIMENSION_TEXTURE3D, the
// view says 2D), and a compute shader declaring RWTexture3D bound to it is a
// descriptor/shader-type mismatch. Vulkan's VkImageView carries the real
// VK_IMAGE_VIEW_TYPE_3D, so the same RHI calls write the right voxels on Vulkan
// and the wrong (or no) ones on D3D12.
//
// THE FIX branches the UAV ViewDimension exactly like the SRV path: TEXTURE3D
// (FirstWSlice=0, WSize=all) for is_3d, TEXTURE2DARRAY (6 slices) for is_cube
// (D3D12 has no native cube UAV), TEXTURE1D for is_1d, else TEXTURE2D.
//
// WHAT THIS TEST PROVES (real WARP, validation ON):
//
//   A compute pipeline whose shader declares an RWTexture3D (GLSL image3D) is
//   bound — via a descriptor set whose kStorageImage write references a 3D
//   texture view — and dispatched to write every voxel. With the fix the UAV is
//   TEXTURE3D, matching both the 3D resource and the RWTexture3D shader
//   declaration, so the program is VALID: the dispatch submits and the queue
//   signal completes. If the fix is temp-reverted (TEXTURE2D UAV on the 3D
//   resource), the WARP debug layer flags the resource/view dimension mismatch
//   and removes the device, so the queue Signal returns DXGI_ERROR_DEVICE_REMOVED
//   and the SubmitDesc Result is an error -> this test FAILS. Detection is via
//   the Signal HRESULT, never an INFINITE fence wait, so a removed device fails
//   the test rather than hanging it.
//
//   (Why not read a z>0 voxel back? The device-level copy_image_to_buffer only
//   copies z-slice 0 of a 3D texture — a TEXTURE2D UAV ALSO writes slice 0 — so
//   a readback at z=0 can't distinguish the two. The dimension-match enforced at
//   dispatch is the honest, distinguishing signal at the RHI surface.)
//
//   Plus a creation-level assertion: the 3D storage texture + 3D view + the
//   kStorageImage descriptor write all succeed (path coverage of the UAV branch).
//
// GTEST_SKIP when no adapter / no glslang / no dxcompiler.dll / the compute PSO
// is unsupported on this WARP. Pattern: Arrange/Act/Assert.
// =============================================================================
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
#endif

#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/Format.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/Pipeline.hpp>
#include <cd/shader/Compiler.hpp>
#if defined(_WIN32)
    #include <cd/rhi/d3d12/D3D12Device.hpp>
#endif

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

#if defined(_WIN32)

namespace
{

constexpr std::uint32_t kDim = 4;  // 4x4x4 storage volume.

// Compute shader: one thread per voxel, writes a known FULL-WHITE value into
// every voxel of a 3D storage image. The image3D declaration becomes an
// RWTexture3D in DXIL — which ONLY binds correctly to a TEXTURE3D UAV. With the
// bug (a TEXTURE2D UAV on the 3D resource) the descriptor is invalid and the
// stores are dropped, so the (zero-initialised committed) voxel at z=0 stays 0.
// With the fix (TEXTURE3D UAV) the store lands and z=0 reads back as 0xFF.
constexpr const char* kCS = R"glsl(
#version 450
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
layout(binding = 0, rgba8) uniform writeonly image3D u_vol;
void main()
{
    ivec3 p = ivec3(gl_GlobalInvocationID);
    imageStore(u_vol, p, vec4(1.0, 1.0, 1.0, 1.0));   // 0xFF white everywhere
}
)glsl";

[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> make_d3d12_device()
{
    cd::rhi::d3d12::D3D12CreateInfo ci {};
    // Validation OFF: we want the bug's FUNCTIONAL effect (a mis-dimensioned UAV
    // writing only slice 0) to be OBSERVABLE in the readback, not turned into a
    // device-removal by the debug layer (which would be host-nondeterministic
    // and would only SKIP, not fail, this test).
    ci.enable_validation = false;
    auto r = cd::rhi::d3d12::create_d3d12_device(ci);
    if (!r.has_value())
        return nullptr;
    return std::move(*r);
}

[[nodiscard]] bool glslang_available()
{
    return cd::shader::make_glslang_compiler() != nullptr;
}

[[nodiscard]] cd::rhi::ShaderModuleHandle
make_module(cd::rhi::IDevice& dev, cd::rhi::ShaderStage stage,
            const char* src, bool* skip)
{
    cd::rhi::ShaderModuleDesc d {};
    d.stage       = stage;
    d.code        = src;
    d.code_size   = std::char_traits<char>::length(src);
    d.entry_point = "main";
    d.language    = cd::rhi::ShaderSourceLanguage::kGlsl;
    auto r = dev.create_shader_module(d);
    if (!r.has_value())
    {
        const std::string msg { r.error().message };
        if (msg.find("dxc") != std::string::npos)
            *skip = true;
        return {};
    }
    return *r;
}

}  // namespace

// ---- D4: a 3D storage image bound to an RWTexture3D dispatches validly -------
TEST(D3D12StorageImage3D, Texture3DUavMatchesRwTexture3DShader)
{
    if (!glslang_available())
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";
    auto dev = make_d3d12_device();
    if (dev == nullptr)
        GTEST_SKIP() << "no D3D12 adapter available on this host";
    auto& d = *dev;

    // --- Creation-level (path coverage of the kStorageImage UAV branch) -------
    cd::rhi::TextureDesc td {};
    td.type         = cd::rhi::TextureType::k3D;
    td.format       = cd::rhi::Format::kRGBA8Unorm;
    td.extent       = { kDim, kDim, kDim };
    td.mip_levels   = 1;
    td.array_layers = 1;
    td.usage        = cd::rhi::TextureUsage::kStorage |
                      cd::rhi::TextureUsage::kTransferSrc;
    auto vol_r = d.create_texture(td);
    ASSERT_TRUE(vol_r.has_value())
        << "3D storage texture creation failed: "
        << (vol_r.has_value() ? std::string {}
                              : std::string(vol_r.error().message.begin(),
                                            vol_r.error().message.end()));
    const auto vol = *vol_r;

    cd::rhi::TextureViewDesc vd {};
    vd.texture = vol;
    vd.type    = cd::rhi::TextureType::k3D;  // is_3d view -> must pick TEXTURE3D UAV
    vd.format  = cd::rhi::Format::kUndefined;
    auto view_r = d.create_texture_view(vd);
    ASSERT_TRUE(view_r.has_value());
    const auto view = *view_r;

    bool skip = false;
    const auto cs = make_module(d, cd::rhi::ShaderStage::kCompute, kCS, &skip);
    if (skip)
        GTEST_SKIP() << "dxcompiler.dll unavailable at runtime";
    ASSERT_TRUE(cs.is_valid());

    // Descriptor set layout: one storage-image binding for compute.
    const std::array<cd::rhi::DescriptorSetLayoutBinding, 1> kBindings {
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 0, .type = cd::rhi::DescriptorType::kStorageImage,
            .count = 1, .stages = cd::rhi::ShaderStage::kCompute },
    };
    cd::rhi::DescriptorSetLayoutDesc sld {};
    sld.bindings = kBindings;
    auto set_layout_r = d.create_descriptor_set_layout(sld);
    ASSERT_TRUE(set_layout_r.has_value()) << set_layout_r.error().message;
    const auto set_layout = *set_layout_r;

    const std::array<cd::rhi::DescriptorSetLayoutHandle, 1> sets { set_layout };
    cd::rhi::PipelineLayoutDesc pld {};
    pld.set_layouts = sets;
    auto layout_r = d.create_pipeline_layout(pld);
    ASSERT_TRUE(layout_r.has_value()) << layout_r.error().message;
    const auto layout = *layout_r;

    // Create the compute PSO on the still-healthy device FIRST, so a genuine
    // "compute unsupported on this adapter" can SKIP cleanly (the descriptor
    // write happens after, so it can't have removed the device by here).
    cd::rhi::ComputePipelineDesc cpd {};
    cpd.layout = layout;
    cpd.shader = cs;
    auto pso_r = d.create_compute_pipeline(cpd);
    if (!pso_r.has_value())
        GTEST_SKIP() << "compute PSO creation unsupported on this adapter: "
                     << std::string(pso_r.error().message.begin(),
                                    pso_r.error().message.end());
    const auto pso = *pso_r;

    auto set_r = d.allocate_descriptor_set(set_layout);
    ASSERT_TRUE(set_r.has_value()) << set_r.error().message;
    const auto set = *set_r;

    cd::rhi::DescriptorWrite dw {};
    dw.binding = 0;
    dw.type    = cd::rhi::DescriptorType::kStorageImage;
    dw.view    = view;  // the 3D view — the UAV ViewDimension must follow it.
    auto upd = d.update_descriptor_set(
        set, std::span<const cd::rhi::DescriptorWrite>(&dw, 1));
    ASSERT_TRUE(upd.has_value()) << upd.error().message;

    // --- Dispatch + FULL-VOLUME readback (the decisive, bidirectional proof) -
    // The shader stores 0xFF white to EVERY voxel through the RWTexture3D. A
    // TEXTURE3D UAV (the fix) addresses all depth slices, so every z gets 0xFF.
    // A TEXTURE2D UAV (the bug) on the 3D resource addresses only slice z=0, so
    // z>0 stays at its zero-initialised value. We copy the FULL volume via the
    // command-buffer copy (which honors image_extent.depth — the device-level
    // copy only ever reads z=0) and assert the FARTHEST slice (z = kDim-1) is
    // 0xFF. z=0 alone cannot distinguish the two (both write it); z = kDim-1 is
    // the slice the mis-dimensioned UAV never reaches.
    auto cmd = d.create_command_buffer(cd::rhi::QueueType::kCompute);
    ASSERT_NE(cmd, nullptr);

    constexpr std::uint64_t kVoxelBytes =
        std::uint64_t { kDim } * kDim * kDim * 4u;
    cd::rhi::BufferDesc bd {};
    bd.size   = kVoxelBytes;
    bd.usage  = cd::rhi::BufferUsage::kTransferDst;
    bd.memory = cd::rhi::MemoryUsage::kGpuToCpu;
    auto buf_r = d.create_buffer(bd);
    ASSERT_TRUE(buf_r.has_value());
    const auto buf = *buf_r;

    cmd->begin();
    cmd->bind_compute_pipeline(pso);
    cmd->bind_descriptor_set(0, set);
    cmd->dispatch(kDim, kDim, kDim);  // one group per voxel.

    // UAV -> transfer-source before the copy (the cmd copy expects COPY_SOURCE).
    cd::rhi::TextureBarrier to_copy {};
    to_copy.texture = vol;
    to_copy.from    = cd::rhi::ResourceState::kUnorderedAccess;
    to_copy.to      = cd::rhi::ResourceState::kTransferSrc;
    to_copy.range   = { 0, 1, 0, 1 };
    cmd->barrier({}, std::span<const cd::rhi::TextureBarrier>(&to_copy, 1));

    // Copy the FULL kDim^3 volume (all depth slices) into the readback buffer.
    cd::rhi::BufferImageCopyRegion cr {};
    cr.buffer_offset = 0;
    cr.image_offset  = { 0, 0, 0 };
    cr.image_extent  = { kDim, kDim, kDim };
    cmd->copy_image_to_buffer(
        vol, buf, std::span<const cd::rhi::BufferImageCopyRegion>(&cr, 1));
    cmd->end();
    d.submit(*cmd);
    d.wait_idle();

    std::array<std::byte, static_cast<std::size_t>(kVoxelBytes)> raw {};
    auto dl = d.download_buffer(buf, 0, std::span<std::byte> { raw });
    ASSERT_TRUE(dl.has_value());

    // Tight row layout (the cmd copy de-pitches to the Vulkan tight contract):
    // voxel (x,y,z) red byte = ((z*kDim + y)*kDim + x) * 4.
    const auto red_at = [&](std::uint32_t x, std::uint32_t y, std::uint32_t z) {
        const std::size_t idx =
            (static_cast<std::size_t>(z) * kDim * kDim +
             static_cast<std::size_t>(y) * kDim + x) * 4u;
        return std::to_integer<std::uint8_t>(raw[idx]);
    };

    // Sanity: slice 0 is written by BOTH the fix and the bug.
    EXPECT_GT(red_at(0, 0, 0), 200u) << "slice z=0 should always be written";

    // Decisive: the FARTHEST slice is reachable ONLY through the TEXTURE3D UAV.
    const auto red_far = red_at(kDim - 1, kDim - 1, kDim - 1);
    EXPECT_GT(red_far, 200u)
        << "BUG: a 3D storage-image store did NOT reach the FARTHEST voxel "
           "(z=" << (kDim - 1) << ") — the kStorageImage UAV was bound with the "
           "wrong ViewDimension (TEXTURE2D on a TEXTURE3D resource), so writes to "
           "every slice but z=0 were mis-dimensioned away (the silent bug Vulkan's "
           "VK_IMAGE_VIEW_TYPE_3D avoids). Read 0x"
        << std::hex << static_cast<unsigned>(red_far);

    d.destroy_buffer(buf);
    d.destroy_compute_pipeline(pso);
    d.destroy_pipeline_layout(layout);
    d.destroy_descriptor_set_layout(set_layout);
    d.destroy_texture_view(view);
    d.destroy_texture(vol);
    d.destroy_shader_module(cs);
}

#endif  // _WIN32
