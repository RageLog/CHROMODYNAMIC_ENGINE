// =============================================================================
// StudioIblBaker.hpp
// -----------------------------------------------------------------------------
// chromodynamic_studio-local R1 IBL boot bake aggregate + driver. Lifted out
// of main() in Marathon Run 11 phase N12.
//
// CPU bake: analytical-sky env cube (with HDR sun disk injection) ->
// diffuse irradiance + prefiltered specular + BRDF LUT, plus the
// parallel procedural Earth textures (albedo / normal / metallic-rough).
// Uses cd::concurrency::WorkStealingThreadPool + JobGraph (X1C boot
// graph) for CPU concurrency. GPU upload + sampler creation stay
// serial because cd::rhi::IDevice is not documented as thread-safe
// today (see ADR-20260528).
//
// W8-AW chrome-mirror quality bake parameters preserved verbatim:
// env 128, spec base 128 / 6 mips / 32 samples, diff 16 / 16 samples,
// BRDF 64x64 / 256 samples. Earth textures: 512 albedo, 512 normal,
// 256 MR.
// =============================================================================
#pragma once

#include <cd/concurrency/JobGraph.hpp>
#include <cd/concurrency/WorkStealingThreadPool.hpp>
#include <cd/ibl/BrdfLut.hpp>
#include <cd/ibl/Cubemap.hpp>
#include <cd/ibl/IrradianceConvolution.hpp>
#include <cd/ibl/PrefilteredSpecular.hpp>
#include <cd/ibl_gpu/Upload.hpp>
#include <cd/material/AnalyticalSkyMaterial.hpp>
#include <cd/math/Vector.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/texture_synth/Earth.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <utility>
#include <vector>

namespace cd_studio {

struct IblBakeCpu
{
    cd::ibl::CubeMapRgbF             env_cube;
    cd::ibl::CubeMapRgbF             diff_cube;
    cd::ibl::PrefilteredSpecularCube spec_cube;
    cd::ibl::BrdfLut                 brdf_lut;
    std::vector<std::uint8_t>        earth_albedo;
    std::vector<std::uint8_t>        earth_normal;
    std::vector<std::uint8_t>        earth_mr;
    std::uint32_t                    earth_albedo_size { 64 };
    std::uint32_t                    earth_normal_size { 64 };
    std::uint32_t                    earth_mr_size     { 64 };
    bool                             ok { false };
};

struct IblBakeGpu
{
    cd::ibl_gpu::GpuCubemap gpu_spec_cube;
    cd::ibl_gpu::GpuCubemap                 gpu_diff_cube;
    cd::ibl_gpu::GpuLut2D                 gpu_brdf_lut;
    cd::rhi::SamplerHandle                  ibl_sampler;
};

inline constexpr std::uint32_t kStudioIblBakerEarthAlbedoSize = 512;
inline constexpr std::uint32_t kStudioIblBakerEarthNormalSize = 512;
inline constexpr std::uint32_t kStudioIblBakerEarthMrSize     = 256;

// ---- bake_ibl_cpu ----------------------------------------------------------
// Runs the X1C boot JobGraph: env-cube -> {diff, spec} + brdf-lut +
// default albedo / normal / mr in parallel on a WorkStealingThreadPool.
// Returns ok=false if the JobGraph reports any failed nodes.
[[nodiscard]] inline IblBakeCpu
bake_ibl_cpu(cd::math::Vec3f sun_dir_unit, bool eager_showcase_bake = false)
{
    IblBakeCpu out;
    out.earth_albedo_size = eager_showcase_bake ? kStudioIblBakerEarthAlbedoSize : 64u;
    out.earth_normal_size = eager_showcase_bake ? kStudioIblBakerEarthNormalSize : 64u;
    out.earth_mr_size     = eager_showcase_bake ? kStudioIblBakerEarthMrSize     : 64u;
    const std::uint32_t env_res      = eager_showcase_bake ? 128u : 64u;
    const std::uint32_t spec_mips    = eager_showcase_bake ? 6u   : 5u;
    const std::uint32_t spec_samples = eager_showcase_bake ? 32u  : 16u;
    const std::uint32_t brdf_samples = eager_showcase_bake ? 256u : 64u;

    auto bake_sky_with_sun = [sun_dir_unit](cd::math::Vec3f dir) noexcept
    {
        return cd::material::sky_with_sun_cpu(dir, sun_dir_unit);
    };
    cd::concurrency::WorkStealingThreadPool boot_pool { 0 };
    cd::concurrency::JobGraph boot_graph;
    const auto a = boot_graph.add(
        [&]
        {
            out.env_cube = cd::ibl::bake_sky_cube(env_res, bake_sky_with_sun);
        });
    const auto b = boot_graph.add(
        [&]
        {
            out.diff_cube =
                cd::ibl::convolve_irradiance(out.env_cube, 16, 16.0F);
        },
        { a });
    const auto c = boot_graph.add(
        [&]
        {
            out.spec_cube =
                cd::ibl::prefilter_specular(out.env_cube, env_res, spec_mips, spec_samples);
        },
        { a });
    const auto d = boot_graph.add(
        [&]
        {
            out.brdf_lut = cd::ibl::bake_brdf_lut(64, 64, brdf_samples);
        });
    const auto e = boot_graph.add(
        [&]
        {
            out.earth_albedo = cd::texture_synth::bake_earth_albedo_rgba8(
                out.earth_albedo_size);
        });
    const auto fnode = boot_graph.add(
        [&]
        {
            out.earth_normal = cd::texture_synth::bake_earth_normal_rgba8(
                out.earth_normal_size);
        });
    const auto g = boot_graph.add(
        [&]
        {
            out.earth_mr = cd::texture_synth::bake_earth_mr_rgba8(
                out.earth_mr_size);
        });
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)fnode; (void)g;
    const bool ok = boot_graph.run(boot_pool);
    if (!ok || boot_graph.failed_nodes() != 0)
    {
        std::fprintf(stderr,
            "[boot] FATAL: bake graph run failed "
            "(ok=%d, failed_nodes=%llu)\n",
            ok ? 1 : 0,
            static_cast<unsigned long long>(boot_graph.failed_nodes()));
        out.ok = false;
        return out;
    }
    out.ok = true;
    return out;
}

// ---- upload_ibl_gpu --------------------------------------------------------
// Uploads the CPU-baked IBL textures to GPU and creates the shared
// clamp-to-edge IBL sampler whose max_lod tracks the spec cube mip
// count.
[[nodiscard]] inline std::expected<IblBakeGpu, int>
upload_ibl_gpu(cd::rhi::IDevice& device, const IblBakeCpu& cpu)
{
    std::fprintf(stderr, "[ibl] uploading to GPU...\n");
    IblBakeGpu out;
    out.gpu_spec_cube =
        cd::ibl_gpu::upload_prefiltered_specular(device, cpu.spec_cube);
    out.gpu_diff_cube =
        cd::ibl_gpu::upload_cubemap_rgba16f(device, cpu.diff_cube);
    out.gpu_brdf_lut =
        cd::ibl_gpu::upload_brdf_lut(device, cpu.brdf_lut);
    std::fprintf(stderr,
        "[ibl] done (spec %u mips, diff 16, brdf 64x64)\n",
        out.gpu_spec_cube.mip_count);

    cd::rhi::SamplerDesc ibl_sd {};
    ibl_sd.mag_filter  = cd::rhi::SamplerFilter::kLinear;
    ibl_sd.min_filter  = cd::rhi::SamplerFilter::kLinear;
    ibl_sd.mipmap_mode = cd::rhi::SamplerMipmapMode::kLinear;
    ibl_sd.address_u   = cd::rhi::SamplerAddressMode::kClampToEdge;
    ibl_sd.address_v   = cd::rhi::SamplerAddressMode::kClampToEdge;
    ibl_sd.address_w   = cd::rhi::SamplerAddressMode::kClampToEdge;
    ibl_sd.max_lod     = static_cast<float>(out.gpu_spec_cube.mip_count);
    auto ibl_samp_r = device.create_sampler(ibl_sd);
    if (!ibl_samp_r.has_value())
        return std::unexpected(23);
    out.ibl_sampler = *ibl_samp_r;
    return out;
}

// ---- normalize_dir ---------------------------------------------------------
// Small helper so callers don't have to inline the manual length+divide
// when feeding bake_ibl_cpu a non-normalised toward-sun direction.
[[nodiscard]] inline cd::math::Vec3f
normalize_dir(cd::math::Vec3f d) noexcept
{
    const float len =
        std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (len <= 0.0F)
        return d;
    return cd::math::Vec3f { d.x / len, d.y / len, d.z / len };
}

}  // namespace cd_studio
