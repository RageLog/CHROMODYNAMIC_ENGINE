// =============================================================================
// CHROMODYNAMIC — HelloBootPostfx.hpp
// phase1125 (on_boot extraction, batch 3): post-fx boot instances
// (composite / bloom chain / auto-exposure / bloom down-up) plus the
// bloom + composite descriptor binders. The binders were previously
// DUPLICATED as local lambdas in on_boot AND on_frame (resize path);
// both sites now share these definitions.
// =============================================================================
#pragma once

#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>
#include <cd/material/Material.hpp>
#include <cd/post/bloom/Bloom.hpp>
#include <cd/post/exposure/Setup.hpp>
#include <cd/rhi/IDevice.hpp>

#include <array>
#include <cstdint>
#include <expected>

namespace cd_sample
{

/// Rebind the bloom prefilter/down/up instances to the current HDR +
/// bloom-chain views. Called at boot and from the on_frame resize path.
template <typename StateT>
inline void bind_bloom_descriptors(StateT& s)
{
    auto wone = [&s](cd::material::MaterialInstance& inst,
                      cd::rhi::TextureViewHandle src)
    {
        std::array<cd::rhi::DescriptorWrite, 1> w {
            cd::rhi::DescriptorWrite { .binding = 0, .array_element = 0,
                .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                .view = src, .sampler = s.albedo_sampler }
        };
        (void)inst.update(w);
    };
    wone(s.bloom_prefilter_inst, s.rts.hdr.view);
    wone(s.bloom_down_insts[0],  s.bloom_chain.mips[0].view);
    wone(s.bloom_down_insts[1],  s.bloom_chain.mips[1].view);
    wone(s.bloom_down_insts[2],  s.bloom_chain.mips[2].view);
    wone(s.bloom_up_insts[0],    s.bloom_chain.mips[3].view);
    wone(s.bloom_up_insts[1],    s.bloom_chain.mips[2].view);
    wone(s.bloom_up_insts[2],    s.bloom_chain.mips[1].view);
}

/// Rebind both composite instances to the current HDR/depth/G-Buffer/
/// history/velocity views. Called at boot and from the resize path.
template <typename StateT>
inline void bind_composite_hdr_descriptors(StateT& s)
{
    for (std::uint32_t i = 0; i < 2; ++i)
    {
        std::array<cd::rhi::DescriptorWrite, 6> ws {
            cd::rhi::DescriptorWrite { .binding = 0, .array_element = 0,
                .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                .view = s.rts.hdr.view, .sampler = s.albedo_sampler },
            cd::rhi::DescriptorWrite { .binding = 1, .array_element = 0,
                .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                .view = s.bloom_chain.mips[0].view, .sampler = s.albedo_sampler },
            cd::rhi::DescriptorWrite { .binding = 2, .array_element = 0,
                .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                .view = s.rts.depth.view, .sampler = s.albedo_sampler },
            cd::rhi::DescriptorWrite { .binding = 3, .array_element = 0,
                .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                .view = s.rts.gbuf_normal.view, .sampler = s.albedo_sampler },
            cd::rhi::DescriptorWrite { .binding = 4, .array_element = 0,
                .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                .view = s.rts.history[i].view, .sampler = s.albedo_sampler },
            cd::rhi::DescriptorWrite { .binding = 5, .array_element = 0,
                .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                .view = s.rts.gbuf_velocity.view, .sampler = s.albedo_sampler }
        };
        (void)s.composite_insts[i].update(ws);
    }
}

/// phase1125: composite + bloom + auto-exposure instance creation lifted
/// verbatim from on_boot. `extent` is the window extent the bloom chain
/// is sized against.
template <typename StateT, typename DeviceT>
[[nodiscard]] inline cd::core::Result<void>
setup_boot_postfx_instances(StateT& s, DeviceT& device, cd::rhi::Extent2D extent)
{
    // Composite material instances
    for (std::uint32_t i = 0; i < 2; ++i)
    {
        auto r = cd::material::MaterialInstance::create(device, s.materials.composite);
        if (!r.has_value())
            return std::unexpected(cd::core::ErrorCode { 0, 33, "composite inst" });
        s.composite_insts[i] = std::move(*r);
    }

    // Bloom mip chain
    if (!cd::post::bloom::create_bloom_chain(device, extent, s.bloom_chain))
        return std::unexpected(cd::core::ErrorCode { 0, 43, "bloom chain" });
    {
        auto r = cd::material::MaterialInstance::create(
            device, s.materials.bloom_prefilter);
        if (!r.has_value())
            return std::unexpected(cd::core::ErrorCode { 0, 44, "bloom prefilter inst" });
        s.bloom_prefilter_inst = std::move(*r);
    }

    // Phase 511 — auto-exposure helper. Allocate the GpuReduction against a
    // small downsampled-HDR extent (<=128x128 so the 256-partial-slot budget
    // is respected; engine should pre-downsample its HDR before feeding the
    // reduction). create() failure is *non-fatal*: the helper degrades to
    // default-constructed (current_ev() == 0, bloom prefilter sees ev=0 =
    // identical to pre-phase-511 behaviour). This keeps the integration
    // safe to enable on backends that lack compute support and on headless
    // smoke runs that don't bind a live HDR descriptor.
    {
        auto ae_r = cd::post::exposure::Setup::create(
            device, cd::rhi::Extent2D { 128U, 128U });
        if (ae_r.has_value()) s.auto_exposure = *ae_r;  // trivially copyable; move was a no-op
        // Else: keep default-constructed; current_ev() returns 0.0F.
    }
    for (std::uint32_t i = 0; i < 3; ++i)
    {
        auto r = cd::material::MaterialInstance::create(
            device, s.materials.bloom_downsample);
        if (!r.has_value())
            return std::unexpected(cd::core::ErrorCode { 0, 45, "bloom down inst" });
        s.bloom_down_insts[i] = std::move(*r);
    }
    for (std::uint32_t i = 0; i < 3; ++i)
    {
        auto r = cd::material::MaterialInstance::create(
            device, s.materials.bloom_upsample);
        if (!r.has_value())
            return std::unexpected(cd::core::ErrorCode { 0, 46, "bloom up inst" });
        s.bloom_up_insts[i] = std::move(*r);
    }
    return {};
}

}  // namespace cd_sample
