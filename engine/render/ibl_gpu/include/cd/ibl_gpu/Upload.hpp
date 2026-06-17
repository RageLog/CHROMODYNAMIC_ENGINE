// =============================================================================
// CHROMODYNAMIC — cd/ibl_gpu/Upload.hpp
//
// upload-helper-v1 (sealed in
// docs/ADR/ADR-20260616-band6-render-misc-scope.md §1).
//
// LIBRARY BOUNDARY: cd::ibl bakes IBL products on the CPU (B3 — irradiance,
// prefiltered specular, split-sum BRDF LUT, equirect->cube). cd::ibl_gpu
// uploads those baked products to a live cd::rhi device. Bake = ibl;
// upload = ibl_gpu. This header owns ONLY the upload side.
//
// GPU upload of cd::ibl CPU-baked products. Three helpers:
//
//   upload_cubemap_rgba16f       — single-mip CubeMapRgbF -> kCube
//   upload_prefiltered_specular  — multi-mip PrefilteredSpecularCube -> kCube
//   upload_brdf_lut              — BrdfLut -> k2D RG16Float
//
// Output is a small POD: { TextureHandle, TextureViewHandle, mip_count }.
// Caller is responsible for destroying the texture + view at shutdown.
//
// VERIFIED: tests/test_ibl_gpu.cpp checks float_to_half / byte-layout /
// mip-count host-side (always) + an upload->readback->compare round-trip of
// upload_brdf_lut on a live Vulkan device (RTX 3080; GTEST_SKIP without ICD).
// =============================================================================
#pragma once

#include <cd/ibl/BrdfLut.hpp>
#include <cd/ibl/Cubemap.hpp>
#include <cd/ibl/PrefilteredSpecular.hpp>

#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace cd::ibl_gpu
{

struct GpuCubemap
{
    cd::rhi::TextureHandle image {};
    cd::rhi::TextureViewHandle view {};
    std::uint32_t mip_count { 1 };
};

struct GpuLut2D
{
    cd::rhi::TextureHandle image {};
    cd::rhi::TextureViewHandle view {};
};

namespace detail
{

/// IEEE-754 binary32 -> binary16 conversion. Branchless except the
/// denormal/overflow guards.
[[nodiscard]] inline std::uint16_t float_to_half(float f) noexcept
{
    union { float f; std::uint32_t u; } v { f };
    const std::uint32_t s = (v.u >> 16) & 0x8000U;
    std::int32_t e = static_cast<std::int32_t>((v.u >> 23) & 0xFFU) - 112;
    std::uint32_t m = v.u & 0x7FFFFFU;
    if (e <= 0) return static_cast<std::uint16_t>(s);
    if (e >= 31) return static_cast<std::uint16_t>(s | 0x7BFFU);
    return static_cast<std::uint16_t>(
        s | (static_cast<std::uint32_t>(e) << 10) | (m >> 13));
}

}  // namespace detail

/// Upload a single-mip CPU cubemap as an RGBA16Float kCube texture.
[[nodiscard]] inline GpuCubemap
upload_cubemap_rgba16f(cd::rhi::IDevice& dev, const cd::ibl::CubeMapRgbF& src)
{
    GpuCubemap out {};
    if (src.face_size == 0) return out;
    const std::uint32_t size = src.face_size;

    cd::rhi::TextureDesc td {};
    td.type = cd::rhi::TextureType::kCube;
    td.format = cd::rhi::Format::kRGBA16Float;
    td.extent = { size, size, 1 };
    td.mip_levels = 1;
    td.array_layers = 6;
    td.usage = cd::rhi::TextureUsage::kSampled | cd::rhi::TextureUsage::kTransferDst;
    td.memory = cd::rhi::MemoryUsage::kGpuOnly;
    auto img = dev.create_texture(td);
    if (!img.has_value()) return out;
    out.image = *img;

    const std::size_t face_pixels = static_cast<std::size_t>(size) * size;
    const std::size_t face_bytes  = face_pixels * static_cast<std::size_t>(4) * sizeof(std::uint16_t);
    const std::size_t total_bytes = face_bytes * static_cast<std::size_t>(6);
    std::vector<std::uint16_t> staging(total_bytes / sizeof(std::uint16_t));

    for (std::uint8_t f = 0; f < cd::ibl::kCubeFaceCount; ++f)
    {
        const auto& src_face = src.faces[f];
        std::uint16_t* dst = staging.data() + (face_pixels * 4 * f);
        for (std::size_t i = 0; i < face_pixels; ++i)
        {
            dst[i*4 + 0] = detail::float_to_half(src_face[i*3 + 0]);
            dst[i*4 + 1] = detail::float_to_half(src_face[i*3 + 1]);
            dst[i*4 + 2] = detail::float_to_half(src_face[i*3 + 2]);
            dst[i*4 + 3] = detail::float_to_half(1.0F);
        }
    }

    cd::rhi::BufferDesc sd {};
    sd.size = total_bytes;
    sd.usage = cd::rhi::BufferUsage::kTransferSrc;
    sd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto staging_r = dev.create_buffer(sd);
    if (!staging_r.has_value()) { dev.destroy_texture(out.image); return {}; }
    const auto staging_buf = *staging_r;
    (void)dev.upload_buffer(staging_buf, 0,
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(staging.data()), total_bytes));

    auto cmd = dev.create_command_buffer(cd::rhi::QueueType::kGraphics);
    if (cmd == nullptr) {
        dev.destroy_buffer(staging_buf);
        dev.destroy_texture(out.image);
        return {};
    }
    cmd->begin();
    std::array<cd::rhi::TextureBarrier, 1> tb_dst { cd::rhi::TextureBarrier {
        .texture = out.image,
        .from = cd::rhi::ResourceState::kUndefined,
        .to   = cd::rhi::ResourceState::kTransferDst,
        .range = { 0, 1, 0, 6 } } };
    cmd->barrier({}, tb_dst);
    std::array<cd::rhi::BufferImageCopyRegion, 6> regs {};
    for (std::uint32_t f = 0; f < 6; ++f) {
        regs[f] = cd::rhi::BufferImageCopyRegion {
            .buffer_offset = face_bytes * f,
            .mip_level = 0,
            .base_layer = f,
            .layer_count = 1,
            .image_offset = { 0, 0, 0 },
            .image_extent = { size, size, 1 } };
    }
    cmd->copy_buffer_to_image(staging_buf, out.image, regs);
    std::array<cd::rhi::TextureBarrier, 1> tb_read { cd::rhi::TextureBarrier {
        .texture = out.image,
        .from = cd::rhi::ResourceState::kTransferDst,
        .to   = cd::rhi::ResourceState::kShaderResource,
        .range = { 0, 1, 0, 6 } } };
    cmd->barrier({}, tb_read);
    cmd->end();
    cd::rhi::SubmitDesc sub {};
    std::array<cd::rhi::ICommandBuffer*, 1> cbs { cmd.get() };
    sub.command_buffers = cbs;
    (void)dev.submit(sub);
    dev.wait_idle();
    dev.destroy_buffer(staging_buf);

    cd::rhi::TextureViewDesc vd {};
    vd.texture = out.image;
    vd.type = cd::rhi::TextureType::kCube;
    vd.format = cd::rhi::Format::kRGBA16Float;
    vd.base_mip = 0; vd.mip_count = 1;
    vd.base_layer = 0; vd.layer_count = 6;
    auto v = dev.create_texture_view(vd);
    if (!v.has_value()) { dev.destroy_texture(out.image); return {}; }
    out.view = *v;
    out.mip_count = 1;
    return out;
}

/// Upload a prefiltered specular cube (mip chain).
[[nodiscard]] inline GpuCubemap
upload_prefiltered_specular(cd::rhi::IDevice& dev,
                            const cd::ibl::PrefilteredSpecularCube& src)
{
    GpuCubemap out {};
    if (src.mip_count == 0) return out;
    const std::uint32_t base = src.mips[0].face_size;
    cd::rhi::TextureDesc td {};
    td.type = cd::rhi::TextureType::kCube;
    td.format = cd::rhi::Format::kRGBA16Float;
    td.extent = { base, base, 1 };
    td.mip_levels = src.mip_count;
    td.array_layers = 6;
    td.usage = cd::rhi::TextureUsage::kSampled | cd::rhi::TextureUsage::kTransferDst;
    td.memory = cd::rhi::MemoryUsage::kGpuOnly;
    auto img = dev.create_texture(td);
    if (!img.has_value()) return out;
    out.image = *img;

    std::size_t total_bytes = 0;
    std::array<std::size_t, cd::ibl::kMaxSpecularMips> mip_byte_offset {};
    for (std::uint32_t m = 0; m < src.mip_count; ++m) {
        mip_byte_offset[m] = total_bytes;
        const std::uint32_t s = src.mips[m].face_size;
        total_bytes += static_cast<std::size_t>(s) * s * 4 * sizeof(std::uint16_t) * 6;
    }
    std::vector<std::uint16_t> staging(total_bytes / sizeof(std::uint16_t));

    for (std::uint32_t m = 0; m < src.mip_count; ++m) {
        const auto& cm = src.mips[m];
        const std::uint32_t s = cm.face_size;
        const std::size_t face_pixels = static_cast<std::size_t>(s) * s;
        for (std::uint8_t f = 0; f < cd::ibl::kCubeFaceCount; ++f) {
            const auto& src_face = cm.faces[f];
            std::uint16_t* dst = staging.data() +
                (mip_byte_offset[m] / sizeof(std::uint16_t)) +
                (face_pixels * 4 * f);
            for (std::size_t i = 0; i < face_pixels; ++i) {
                dst[i*4 + 0] = detail::float_to_half(src_face[i*3 + 0]);
                dst[i*4 + 1] = detail::float_to_half(src_face[i*3 + 1]);
                dst[i*4 + 2] = detail::float_to_half(src_face[i*3 + 2]);
                dst[i*4 + 3] = detail::float_to_half(1.0F);
            }
        }
    }

    cd::rhi::BufferDesc sd {};
    sd.size = total_bytes;
    sd.usage = cd::rhi::BufferUsage::kTransferSrc;
    sd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto staging_r = dev.create_buffer(sd);
    if (!staging_r.has_value()) { dev.destroy_texture(out.image); return {}; }
    const auto staging_buf = *staging_r;
    (void)dev.upload_buffer(staging_buf, 0,
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(staging.data()), total_bytes));

    auto cmd = dev.create_command_buffer(cd::rhi::QueueType::kGraphics);
    if (cmd == nullptr) {
        dev.destroy_buffer(staging_buf);
        dev.destroy_texture(out.image);
        return {};
    }
    cmd->begin();
    std::array<cd::rhi::TextureBarrier, 1> tb_dst { cd::rhi::TextureBarrier {
        .texture = out.image,
        .from = cd::rhi::ResourceState::kUndefined,
        .to   = cd::rhi::ResourceState::kTransferDst,
        .range = { 0, src.mip_count, 0, 6 } } };
    cmd->barrier({}, tb_dst);

    std::vector<cd::rhi::BufferImageCopyRegion> regs;
    regs.reserve(static_cast<std::size_t>(src.mip_count) * 6U);
    for (std::uint32_t m = 0; m < src.mip_count; ++m) {
        const std::uint32_t s = src.mips[m].face_size;
        const std::size_t face_pixels = static_cast<std::size_t>(s) * s;
        const std::size_t face_bytes  = face_pixels * static_cast<std::size_t>(4) * sizeof(std::uint16_t);
        for (std::uint32_t f = 0; f < 6; ++f) {
            regs.push_back(cd::rhi::BufferImageCopyRegion {
                .buffer_offset = mip_byte_offset[m] + face_bytes * f,
                .mip_level = m,
                .base_layer = f,
                .layer_count = 1,
                .image_offset = { 0, 0, 0 },
                .image_extent = { s, s, 1 } });
        }
    }
    cmd->copy_buffer_to_image(staging_buf, out.image, regs);
    std::array<cd::rhi::TextureBarrier, 1> tb_read { cd::rhi::TextureBarrier {
        .texture = out.image,
        .from = cd::rhi::ResourceState::kTransferDst,
        .to   = cd::rhi::ResourceState::kShaderResource,
        .range = { 0, src.mip_count, 0, 6 } } };
    cmd->barrier({}, tb_read);
    cmd->end();
    cd::rhi::SubmitDesc sub {};
    std::array<cd::rhi::ICommandBuffer*, 1> cbs { cmd.get() };
    sub.command_buffers = cbs;
    (void)dev.submit(sub);
    dev.wait_idle();
    dev.destroy_buffer(staging_buf);

    cd::rhi::TextureViewDesc vd {};
    vd.texture = out.image;
    vd.type = cd::rhi::TextureType::kCube;
    vd.format = cd::rhi::Format::kRGBA16Float;
    vd.base_mip = 0; vd.mip_count = src.mip_count;
    vd.base_layer = 0; vd.layer_count = 6;
    auto v = dev.create_texture_view(vd);
    if (!v.has_value()) { dev.destroy_texture(out.image); return {}; }
    out.view = *v;
    out.mip_count = src.mip_count;
    return out;
}

/// Upload the BRDF LUT as a 2D RG16Float texture.
[[nodiscard]] inline GpuLut2D
upload_brdf_lut(cd::rhi::IDevice& dev, const cd::ibl::BrdfLut& src)
{
    GpuLut2D out {};
    if (src.width == 0 || src.height == 0) return out;
    cd::rhi::TextureDesc td {};
    td.type = cd::rhi::TextureType::k2D;
    td.format = cd::rhi::Format::kRG16Float;
    td.extent = { src.width, src.height, 1 };
    td.mip_levels = 1;
    td.array_layers = 1;
    td.usage = cd::rhi::TextureUsage::kSampled | cd::rhi::TextureUsage::kTransferDst;
    td.memory = cd::rhi::MemoryUsage::kGpuOnly;
    auto img = dev.create_texture(td);
    if (!img.has_value()) return out;
    out.image = *img;

    const std::size_t pixels = static_cast<std::size_t>(src.width) * src.height;
    std::vector<std::uint16_t> staging(pixels * 2);
    for (std::size_t i = 0; i < pixels; ++i) {
        staging[i*2 + 0] = detail::float_to_half(src.rg[i*2 + 0]);
        staging[i*2 + 1] = detail::float_to_half(src.rg[i*2 + 1]);
    }
    const std::size_t bytes = pixels * static_cast<std::size_t>(2) * sizeof(std::uint16_t);

    cd::rhi::BufferDesc sd {};
    sd.size = bytes;
    sd.usage = cd::rhi::BufferUsage::kTransferSrc;
    sd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto staging_r = dev.create_buffer(sd);
    if (!staging_r.has_value()) { dev.destroy_texture(out.image); return {}; }
    const auto staging_buf = *staging_r;
    (void)dev.upload_buffer(staging_buf, 0,
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(staging.data()), bytes));
    auto cmd = dev.create_command_buffer(cd::rhi::QueueType::kGraphics);
    if (cmd == nullptr) { dev.destroy_buffer(staging_buf); dev.destroy_texture(out.image); return {}; }
    cmd->begin();
    std::array<cd::rhi::TextureBarrier, 1> tb_dst { cd::rhi::TextureBarrier {
        .texture = out.image,
        .from = cd::rhi::ResourceState::kUndefined,
        .to   = cd::rhi::ResourceState::kTransferDst,
        .range = { 0, 1, 0, 1 } } };
    cmd->barrier({}, tb_dst);
    std::array<cd::rhi::BufferImageCopyRegion, 1> regs { cd::rhi::BufferImageCopyRegion {
        .buffer_offset = 0, .mip_level = 0, .base_layer = 0, .layer_count = 1,
        .image_offset = { 0, 0, 0 }, .image_extent = { src.width, src.height, 1 } } };
    cmd->copy_buffer_to_image(staging_buf, out.image, regs);
    std::array<cd::rhi::TextureBarrier, 1> tb_read { cd::rhi::TextureBarrier {
        .texture = out.image,
        .from = cd::rhi::ResourceState::kTransferDst,
        .to   = cd::rhi::ResourceState::kShaderResource,
        .range = { 0, 1, 0, 1 } } };
    cmd->barrier({}, tb_read);
    cmd->end();
    cd::rhi::SubmitDesc sub {};
    std::array<cd::rhi::ICommandBuffer*, 1> cbs { cmd.get() };
    sub.command_buffers = cbs;
    (void)dev.submit(sub);
    dev.wait_idle();
    dev.destroy_buffer(staging_buf);

    cd::rhi::TextureViewDesc vd {};
    vd.texture = out.image;
    vd.type = cd::rhi::TextureType::k2D;
    vd.format = cd::rhi::Format::kRG16Float;
    vd.base_mip = 0; vd.mip_count = 1;
    vd.base_layer = 0; vd.layer_count = 1;
    auto v = dev.create_texture_view(vd);
    if (!v.has_value()) { dev.destroy_texture(out.image); return {}; }
    out.view = *v;
    return out;
}

}  // namespace cd::ibl_gpu
