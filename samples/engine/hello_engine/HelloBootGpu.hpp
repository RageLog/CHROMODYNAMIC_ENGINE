// =============================================================================
// CHROMODYNAMIC — HelloBootGpu.hpp
// phase1129 (on_boot extraction, batch 5): boot-time GPU buffer / shadow /
// IBL resource creation lifted verbatim out of HelloEngineApp::on_boot():
//   * 2048² D32 shadow target + clamp-to-border sampler + matrix UBO
//   * multi-light UBO (16-byte header + kMaxLights × 80-byte slots)
//   * W8-BC instance-material SSBO
//   * CPU IBL bake + GPU upload (cd_sample::bake_ibl_cpu / upload_ibl_gpu)
//
// The byte-size constants live HERE so on_boot, on_frame and the
// descriptor helpers (HelloBootBindless.hpp) share one definition.
// =============================================================================
#pragma once

#include "HelloIbl.hpp"            // bake_ibl_cpu / upload_ibl_gpu
#include "HelloRayQuery.hpp"       // cd::hello_engine::kInstMatBytes
#include "HelloRenderTargets.hpp"  // kDepthFormat

#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>
#include <cd/math/Vector.hpp>
#include <cd/rhi/IDevice.hpp>

#include <cstdint>
#include <expected>

namespace cd_sample
{

inline constexpr cd::rhi::Extent2D kBootShadowMapSize { 2048, 2048 };
inline constexpr std::uint32_t kBootLightSlotBytes = 80;
inline constexpr std::uint32_t kBootLightUboBytes =
    16 + cd::hello_engine::kMaxLights * kBootLightSlotBytes;

/// phase1129: shadow-map resources + multi-light UBO + instance-material
/// SSBO + IBL bake/upload. Error-code domains/values preserved verbatim
/// from on_boot.
template <typename StateT, typename DeviceT>
[[nodiscard]] inline cd::core::Result<void>
setup_boot_gpu_buffers(StateT& s, DeviceT& device)
{
    // Shadow-map resources
    if (!create_depth_target(device, kBootShadowMapSize, kDepthFormat,
                             s.shadow_target,
                             cd::rhi::TextureUsage::kSampled))
        return std::unexpected(cd::core::ErrorCode { 0, 11, "shadow target" });

    cd::rhi::SamplerDesc shad_sd {};
    shad_sd.mag_filter   = cd::rhi::SamplerFilter::kLinear;
    shad_sd.min_filter   = cd::rhi::SamplerFilter::kLinear;
    shad_sd.mipmap_mode  = cd::rhi::SamplerMipmapMode::kNearest;
    shad_sd.address_u    = cd::rhi::SamplerAddressMode::kClampToBorder;
    shad_sd.address_v    = cd::rhi::SamplerAddressMode::kClampToBorder;
    shad_sd.address_w    = cd::rhi::SamplerAddressMode::kClampToBorder;
    shad_sd.border_color = cd::rhi::BorderColor::kFloatOpaqueWhite;
    shad_sd.max_lod      = 1.0F;
    if (auto r = device.create_sampler(shad_sd); !r.has_value())
        return std::unexpected(cd::core::ErrorCode { 0, 12, "shadow sampler" });
    else s.shadow_sampler = *r;

    cd::rhi::BufferDesc shad_ubo_d {};
    shad_ubo_d.size   = sizeof(cd::math::Mat4f);
    shad_ubo_d.usage  = cd::rhi::BufferUsage::kUniform;
    shad_ubo_d.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    if (auto r = device.create_buffer(shad_ubo_d); !r.has_value())
        return std::unexpected(cd::core::ErrorCode { 0, 13, "shadow ubo" });
    else s.shadow_ubo = *r;

    // Multi-light UBO
    cd::rhi::BufferDesc lights_d {};
    lights_d.size   = kBootLightUboBytes;
    lights_d.usage  = cd::rhi::BufferUsage::kUniform;
    lights_d.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    if (auto r = device.create_buffer(lights_d); !r.has_value())
        return std::unexpected(cd::core::ErrorCode { 0, 16, "lights ubo" });
    else s.lights_ubo = *r;

    // Instance-material SSBO (W8-BC)
    cd::rhi::BufferDesc inst_d {};
    inst_d.size   = cd::hello_engine::kInstMatBytes;
    inst_d.usage  = cd::rhi::BufferUsage::kStorage | cd::rhi::BufferUsage::kTransferDst;
    inst_d.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    if (auto r = device.create_buffer(inst_d); !r.has_value())
        return std::unexpected(cd::core::ErrorCode { 0, 16, "inst mat ssbo" });
    else s.inst_mat_ssbo = *r;

    // IBL bake
    constexpr cd::math::Vec3f kIblSunDirToward { 0.3F, 0.9F, 0.2F };
    s.ibl_cpu = cd_sample::bake_ibl_cpu(cd_sample::normalize_dir(kIblSunDirToward));
    if (!s.ibl_cpu.ok)
        return std::unexpected(cd::core::ErrorCode { 0, 23, "ibl bake" });
    if (auto r = cd_sample::upload_ibl_gpu(device, s.ibl_cpu); !r.has_value())
        return std::unexpected(
            cd::core::ErrorCode { 0, static_cast<std::uint32_t>(r.error()), "ibl gpu" });
    else s.ibl_gpu = *r;

    return {};
}

}  // namespace cd_sample
