// =============================================================================
// CHROMODYNAMIC -- engine/render/restir_di/src/SvgfDenoiser.cpp
// Phase 616 / Sprint-4 -- ReSTIR DI SVGF (Schied 2017) skeleton.
//
// See cd/restir_di/SvgfDenoiser.hpp for the design rationale + the
// three-pass topology. This translation unit owns:
//   * three GLSL -> SPIR-V shader compiles (moment / variance / filter)
//   * three descriptor-set layouts + pipeline layouts + compute pipelines
//   * two filter descriptor sets (A + B) for the wavelet ping-pong
//   * three internal scratch SSBOs (moment / variance / history-length)
//
// Sprint-4 ships the skeleton ONLY. Texture-view bindings are reserved
// seam slots -- the kernels gracefully degrade to a reservoir-luminance
// edge stop when the views are null. The Sprint-5 framegraph hook-up
// will wire the proper G-buffer routes.
//
// hello_engine is NOT touched (per FROZEN constraint).
// =============================================================================
#include <cd/restir_di/SvgfDenoiser.hpp>

#include <cd/rhi/Enums.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/Pipeline.hpp>
#include <cd/shader/Compiler.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace cd::restir_di
{

namespace
{

// =============================================================================
// GLSL Pass 1 -- moment estimation
//
// Per-pixel reads the current reservoir luminance, accumulates first +
// second raw moments into the temporal `MomentBuf` with an exponential
// alpha decay, and increments the `HistoryBuf` survival counter. Sprint-4
// skeleton: the temporal reproject path is stubbed (sample(t-1) == sample(t))
// so the smoke runs without motion vectors. Sprint-5 will wire reproject.
// =============================================================================
constexpr std::string_view kRestirSvgfMomentEstimateCS = R"glsl(
#version 460
layout(local_size_x = 8, local_size_y = 8) in;

struct DiSample    { uint light_index; float target_pdf; vec3 radiance; };
struct DiReservoir { DiSample selected; float weight_sum; uint M; uint age; };

float reservoir_final_weight(DiReservoir r) {
    if (r.M == 0u || r.selected.target_pdf <= 0.0) return 0.0;
    return r.weight_sum / (float(r.M) * r.selected.target_pdf);
}

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

layout(set = 0, binding = 0, std430) readonly buffer ReservoirIn {
    DiReservoir in_res[];
};
layout(set = 0, binding = 1, std430) buffer MomentBuf {
    // .xy = (mu1, mu2) current moments, .zw = (mu1_prev, mu2_prev)
    vec4 moments[];
};
layout(set = 0, binding = 2, std430) buffer HistoryBuf {
    uint history_length[];
};
layout(push_constant) uniform PC {
    uvec2 resolution;
    float temporal_alpha;
    float _pad;
} pc;

void main() {
    uvec2 px = gl_GlobalInvocationID.xy;
    if (any(greaterThanEqual(px, pc.resolution))) return;

    uint idx = px.y * pc.resolution.x + px.x;
    DiReservoir R = in_res[idx];

    float L  = luma(R.selected.radiance) * reservoir_final_weight(R);
    float L2 = L * L;

    vec4 prev = moments[idx];

    // Sprint-4 skeleton: no reprojection yet (Sprint-5 wires motion-vec).
    // The temporal alpha clamp here is the production formula; with a
    // stubbed reproject it just exponentially decays the prior moments.
    float a = clamp(pc.temporal_alpha, 0.0, 1.0);

    vec4 cur;
    cur.x = mix(prev.z, L,  a);  // mu1
    cur.y = mix(prev.w, L2, a);  // mu2
    cur.z = cur.x;               // shift to prev slot for next frame
    cur.w = cur.y;

    moments[idx]         = cur;
    history_length[idx]  = min(history_length[idx] + 1u, 64u);
}
)glsl";

// =============================================================================
// GLSL Pass 2 -- variance estimation
//
// Reads the moment buffer + history length, emits the per-pixel variance
// into the filter input SSBO. When `history_length < kSvgfShortHistoryThreshold`,
// falls back to a 7x7 spatial luminance variance estimate (paper Section 4.1).
// =============================================================================
constexpr std::string_view kRestirSvgfVarianceCS = R"glsl(
#version 460
layout(local_size_x = 8, local_size_y = 8) in;

struct DiSample    { uint light_index; float target_pdf; vec3 radiance; };
struct DiReservoir { DiSample selected; float weight_sum; uint M; uint age; };

float reservoir_final_weight(DiReservoir r) {
    if (r.M == 0u || r.selected.target_pdf <= 0.0) return 0.0;
    return r.weight_sum / (float(r.M) * r.selected.target_pdf);
}

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

layout(set = 0, binding = 0, std430) readonly buffer ReservoirIn {
    DiReservoir in_res[];
};
layout(set = 0, binding = 1, std430) readonly buffer MomentBuf {
    vec4 moments[];
};
layout(set = 0, binding = 2, std430) readonly buffer HistoryBuf {
    uint history_length[];
};
layout(set = 0, binding = 3, std430) buffer VarianceBuf {
    float variance[];
};
layout(push_constant) uniform PC {
    uvec2 resolution;
    uint  short_history_threshold;
    uint  _pad;
} pc;

float temporal_variance(uint idx) {
    vec4 m = moments[idx];
    return max(m.y - m.x * m.x, 0.0);
}

float spatial_variance_7x7(uvec2 px) {
    // Welford running sum -- 7x7 = 49 taps. Pads OOB with clamped neighbours.
    float sum = 0.0;
    float sum2 = 0.0;
    float n = 0.0;
    for (int dy = -3; dy <= 3; ++dy) {
        for (int dx = -3; dx <= 3; ++dx) {
            ivec2 npx = clamp(ivec2(px) + ivec2(dx, dy),
                              ivec2(0),
                              ivec2(pc.resolution) - ivec2(1));
            uint nidx = uint(npx.y) * pc.resolution.x + uint(npx.x);
            float L = luma(in_res[nidx].selected.radiance)
                    * reservoir_final_weight(in_res[nidx]);
            sum  += L;
            sum2 += L * L;
            n    += 1.0;
        }
    }
    float mu = sum / max(n, 1.0);
    return max(sum2 / max(n, 1.0) - mu * mu, 0.0);
}

void main() {
    uvec2 px = gl_GlobalInvocationID.xy;
    if (any(greaterThanEqual(px, pc.resolution))) return;

    uint idx = px.y * pc.resolution.x + px.x;

    float v;
    if (history_length[idx] < pc.short_history_threshold) {
        v = spatial_variance_7x7(px);
    } else {
        v = temporal_variance(idx);
    }
    variance[idx] = v;
}
)glsl";

// =============================================================================
// GLSL Pass 3 -- edge-aware A-trous filter
//
// Same 5x5 wavelet stencil as Sprint-3 but the luminance edge-stop is now
// driven by `phi_l = phi_color * sqrt(g(variance))` (paper Eq. 5) with `g`
// a 3x3 Gaussian prefilter. Depth + normal edge stops accepted as
// push-constant `phi_z` + `phi_n`; when their accompanying textures are
// not bound, the kernel falls back to a reservoir-luminance-only path
// (Sprint-3-equivalent) so the library smoke remains scene-free.
// =============================================================================
constexpr std::string_view kRestirSvgfFilterCS = R"glsl(
#version 460
layout(local_size_x = 8, local_size_y = 8) in;

struct DiSample    { uint light_index; float target_pdf; vec3 radiance; };
struct DiReservoir { DiSample selected; float weight_sum; uint M; uint age; };

float reservoir_final_weight(DiReservoir r) {
    if (r.M == 0u || r.selected.target_pdf <= 0.0) return 0.0;
    return r.weight_sum / (float(r.M) * r.selected.target_pdf);
}

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

layout(set = 0, binding = 0, std430) readonly buffer ReservoirIn {
    DiReservoir in_res[];
};
layout(set = 0, binding = 1, std430) buffer ReservoirOut {
    DiReservoir out_res[];
};
layout(set = 0, binding = 2, std430) readonly buffer VarianceBuf {
    float variance[];
};
layout(push_constant) uniform PC {
    uvec2 resolution;
    float step_width;
    float phi_color;   // base luminance sigma scaler
    float phi_depth;   // depth edge-stop sigma
    float phi_normal;  // normal edge-stop power
    uint  iteration;
    uint  _pad;
} pc;

const float kKernel[5] = float[5](
    1.0 / 16.0,
    4.0 / 16.0,
    6.0 / 16.0,
    4.0 / 16.0,
    1.0 / 16.0
);

float gaussian_3x3_variance(uvec2 px) {
    // Paper Eq. 5 prefilter `g` over the variance buffer. Bilinearly
    // weighted 3x3 sample.
    const float w[3] = float[3](1.0/4.0, 1.0/2.0, 1.0/4.0);
    float v = 0.0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            ivec2 npx = clamp(ivec2(px) + ivec2(dx, dy),
                              ivec2(0),
                              ivec2(pc.resolution) - ivec2(1));
            uint nidx = uint(npx.y) * pc.resolution.x + uint(npx.x);
            v += variance[nidx] * w[dx + 1] * w[dy + 1];
        }
    }
    return v;
}

void main() {
    uvec2 px = gl_GlobalInvocationID.xy;
    if (any(greaterThanEqual(px, pc.resolution))) return;

    uint idx = px.y * pc.resolution.x + px.x;
    DiReservoir Rc = in_res[idx];

    float lc        = luma(Rc.selected.radiance) * reservoir_final_weight(Rc);
    float var_c     = gaussian_3x3_variance(px);
    float phi_l_inv = 1.0 / (pc.phi_color * sqrt(max(var_c, 1e-8)) + 1e-6);

    vec3  acc_radiance   = vec3(0.0);
    float acc_weight_sum = 0.0;
    float acc_target_pdf = 0.0;
    uint  acc_M          = 0u;
    float total_w        = 0.0;
    uint  best_light     = Rc.selected.light_index;
    float best_w         = -1.0;

    int step = int(max(pc.step_width, 1.0));

    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            ivec2 npx = ivec2(px) + ivec2(dx, dy) * step;
            if (any(lessThan(npx, ivec2(0))) ||
                any(greaterThanEqual(npx, ivec2(pc.resolution))))
                continue;

            uint nidx = uint(npx.y) * pc.resolution.x + uint(npx.x);
            DiReservoir Rn = in_res[nidx];

            float ln = luma(Rn.selected.radiance) * reservoir_final_weight(Rn);
            float dl = abs(lc - ln);
            float wl = exp(-dl * phi_l_inv);

            // Depth + normal edge stops are reserved seam slots in the
            // Sprint-4 skeleton; with the texture views unbound (handed
            // in by the host as null), the depth + normal sigmas degenerate
            // to a multiplicative 1.0 contribution. Sprint-5 will replace
            // these constant terms with proper texelFetch reads.
            // Reserved seams: depth + normal edge stops degenerate to 1.0
            // until Sprint-5 wires the G-buffer texture views. The push-
            // constants `pc.phi_depth` + `pc.phi_normal` are still read by
            // the compiler (kept live below so glslang does not strip the
            // block layout).
            float wz = mix(1.0, 1.0, clamp(pc.phi_depth, 0.0, 0.0));
            float wn = mix(1.0, 1.0, clamp(pc.phi_normal, 0.0, 0.0));

            float kw = kKernel[dx + 2] * kKernel[dy + 2];
            float w  = kw * wl * wz * wn;

            acc_radiance   += Rn.selected.radiance   * w;
            acc_weight_sum += Rn.weight_sum          * w;
            acc_target_pdf += Rn.selected.target_pdf * w;
            acc_M          += Rn.M;
            total_w        += w;

            if (w > best_w) {
                best_w     = w;
                best_light = Rn.selected.light_index;
            }
        }
    }

    DiReservoir Ro;
    Ro.selected.light_index = best_light;
    if (total_w > 0.0) {
        Ro.selected.radiance   = acc_radiance   / total_w;
        Ro.selected.target_pdf = acc_target_pdf / total_w;
        Ro.weight_sum          = acc_weight_sum / total_w;
    } else {
        Ro.selected.radiance   = Rc.selected.radiance;
        Ro.selected.target_pdf = Rc.selected.target_pdf;
        Ro.weight_sum          = Rc.weight_sum;
    }
    Ro.M   = Rc.M;
    Ro.age = Rc.age;
    out_res[idx] = Ro;
}
)glsl";

// =============================================================================
// Host-side constants + push-constant blocks.
// =============================================================================
constexpr std::uint32_t kGroupSizeXY = 8U;

struct MomentPC
{
    std::uint32_t resolution_x;
    std::uint32_t resolution_y;
    float         temporal_alpha;
    float         _pad;
};

struct VariancePC
{
    std::uint32_t resolution_x;
    std::uint32_t resolution_y;
    std::uint32_t short_history_threshold;
    std::uint32_t _pad;
};

struct FilterPC
{
    std::uint32_t resolution_x;
    std::uint32_t resolution_y;
    float         step_width;
    float         phi_color;
    float         phi_depth;
    float         phi_normal;
    std::uint32_t iteration;
    std::uint32_t _pad;
};

// Reservoir size mirrors `cd::restir_di::DispatchPass::reservoir_buffer_size`
// without taking a dep on that header: 32B per pixel (8 x 4B fields).
constexpr std::uint64_t kReservoirStrideBytes = 32U;

constexpr float kSvgfPhiColor = 4.0F;  // matches Schied 2017 reference

// --- helpers -----------------------------------------------------------------

cd::core::Result<cd::rhi::ShaderModuleHandle>
compile_module(cd::rhi::IDevice& device,
               std::string_view  src,
               const char*       name)
{
    using cd::rhi::rhi_errors::Code;
    using cd::rhi::rhi_errors::make;

    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr)
    {
        return std::unexpected(
            make(Code::kBackendInitFailed,
                 "restir_di::SvgfDenoiser: glslang compiler unavailable"));
    }

    cd::shader::CompileDesc d {};
    d.source      = src;
    d.stage       = cd::shader::ShaderStage::kCompute;
    d.source_name = name;
    auto compiled = compiler->compile(d);
    if (!compiled.has_value())
    {
        return std::unexpected(
            make(Code::kBackendInitFailed,
                 "restir_di::SvgfDenoiser: shader compile failed"));
    }

    cd::rhi::ShaderModuleDesc sm_desc {};
    sm_desc.stage      = cd::rhi::ShaderStage::kCompute;
    sm_desc.code       = compiled->spirv.data();
    sm_desc.code_size  = compiled->spirv.size() * sizeof(std::uint32_t);
    sm_desc.debug_name = name;
    return device.create_shader_module(sm_desc);
}

}  // namespace

// --- public static helpers ---------------------------------------------------

std::uint32_t SvgfDenoiser::group_count_x(std::uint32_t viewport_w) noexcept
{
    return (viewport_w + kGroupSizeXY - 1U) / kGroupSizeXY;
}

std::uint32_t SvgfDenoiser::group_count_y(std::uint32_t viewport_h) noexcept
{
    return (viewport_h + kGroupSizeXY - 1U) / kGroupSizeXY;
}

// --- lifecycle ---------------------------------------------------------------

SvgfDenoiser::~SvgfDenoiser()
{
    shutdown();
}

cd::core::Result<void>
SvgfDenoiser::configure(cd::rhi::IDevice& device, const SvgfDenoiserConfig& cfg)
{
    using cd::rhi::rhi_errors::Code;
    using cd::rhi::rhi_errors::make;

    if (cfg.viewport_width == 0U || cfg.viewport_height == 0U)
    {
        return std::unexpected(
            make(Code::kInvalidArgument,
                 "restir_di::SvgfDenoiser::configure: zero viewport extent"));
    }
    if (cfg.filter_iterations == 0U)
    {
        return std::unexpected(
            make(Code::kInvalidArgument,
                 "restir_di::SvgfDenoiser::configure: filter_iterations must be >= 1"));
    }

    // Idempotent re-configure.
    shutdown();

    device_ = &device;
    cfg_    = cfg;
    cfg_.temporal_alpha = std::clamp(cfg_.temporal_alpha, 1e-4F, 1.0F);

    // ---- 1. compile + module per pass --------------------------------------
    {
        auto mm = compile_module(device, kRestirSvgfMomentEstimateCS,
                                 "restir_svgf_moment_sprint4");
        if (!mm.has_value())
        {
            const auto err = mm.error();
            shutdown();
            return std::unexpected(err);
        }
        moment_module_ = *mm;
    }
    {
        auto vm = compile_module(device, kRestirSvgfVarianceCS,
                                 "restir_svgf_variance_sprint4");
        if (!vm.has_value())
        {
            const auto err = vm.error();
            shutdown();
            return std::unexpected(err);
        }
        variance_module_ = *vm;
    }
    {
        auto fm = compile_module(device, kRestirSvgfFilterCS,
                                 "restir_svgf_filter_sprint4");
        if (!fm.has_value())
        {
            const auto err = fm.error();
            shutdown();
            return std::unexpected(err);
        }
        filter_module_ = *fm;
    }

    // ---- 2. descriptor set layouts ----------------------------------------
    // Moment: in_res(0), moments(1), history(2)
    {
        const std::array<cd::rhi::DescriptorSetLayoutBinding, 3> b { {
            { .binding = 0U, .type = cd::rhi::DescriptorType::kStorageBuffer,
              .count = 1U, .stages = cd::rhi::ShaderStage::kCompute },
            { .binding = 1U, .type = cd::rhi::DescriptorType::kStorageBuffer,
              .count = 1U, .stages = cd::rhi::ShaderStage::kCompute },
            { .binding = 2U, .type = cd::rhi::DescriptorType::kStorageBuffer,
              .count = 1U, .stages = cd::rhi::ShaderStage::kCompute },
        } };
        cd::rhi::DescriptorSetLayoutDesc d {};
        d.bindings = b;
        auto h = device.create_descriptor_set_layout(d);
        if (!h.has_value())
        {
            const auto err = h.error();
            shutdown();
            return std::unexpected(err);
        }
        moment_dsl_ = *h;
    }
    // Variance: in_res(0), moments(1), history(2), variance_out(3)
    {
        const std::array<cd::rhi::DescriptorSetLayoutBinding, 4> b { {
            { .binding = 0U, .type = cd::rhi::DescriptorType::kStorageBuffer,
              .count = 1U, .stages = cd::rhi::ShaderStage::kCompute },
            { .binding = 1U, .type = cd::rhi::DescriptorType::kStorageBuffer,
              .count = 1U, .stages = cd::rhi::ShaderStage::kCompute },
            { .binding = 2U, .type = cd::rhi::DescriptorType::kStorageBuffer,
              .count = 1U, .stages = cd::rhi::ShaderStage::kCompute },
            { .binding = 3U, .type = cd::rhi::DescriptorType::kStorageBuffer,
              .count = 1U, .stages = cd::rhi::ShaderStage::kCompute },
        } };
        cd::rhi::DescriptorSetLayoutDesc d {};
        d.bindings = b;
        auto h = device.create_descriptor_set_layout(d);
        if (!h.has_value())
        {
            const auto err = h.error();
            shutdown();
            return std::unexpected(err);
        }
        variance_dsl_ = *h;
    }
    // Filter: in_res(0), out_res(1), variance(2)
    {
        const std::array<cd::rhi::DescriptorSetLayoutBinding, 3> b { {
            { .binding = 0U, .type = cd::rhi::DescriptorType::kStorageBuffer,
              .count = 1U, .stages = cd::rhi::ShaderStage::kCompute },
            { .binding = 1U, .type = cd::rhi::DescriptorType::kStorageBuffer,
              .count = 1U, .stages = cd::rhi::ShaderStage::kCompute },
            { .binding = 2U, .type = cd::rhi::DescriptorType::kStorageBuffer,
              .count = 1U, .stages = cd::rhi::ShaderStage::kCompute },
        } };
        cd::rhi::DescriptorSetLayoutDesc d {};
        d.bindings = b;
        auto h = device.create_descriptor_set_layout(d);
        if (!h.has_value())
        {
            const auto err = h.error();
            shutdown();
            return std::unexpected(err);
        }
        filter_dsl_ = *h;
    }

    // ---- 3. pipeline layouts ---------------------------------------------
    auto make_layout = [&](cd::rhi::DescriptorSetLayoutHandle dsl,
                           std::uint32_t pc_size,
                           cd::rhi::PipelineLayoutHandle& out)
        -> cd::core::Result<void>
    {
        const std::array<cd::rhi::DescriptorSetLayoutHandle, 1> set_layouts { dsl };
        const std::array<cd::rhi::PushConstantRange, 1> push_ranges { {
            { .stages = cd::rhi::ShaderStage::kCompute,
              .offset = 0U,
              .size   = pc_size },
        } };
        cd::rhi::PipelineLayoutDesc d {};
        d.set_layouts    = set_layouts;
        d.push_constants = push_ranges;
        auto h = device.create_pipeline_layout(d);
        if (!h.has_value())
            return std::unexpected(h.error());
        out = *h;
        return {};
    };

    if (auto r = make_layout(moment_dsl_, sizeof(MomentPC), moment_layout_); !r.has_value())
    { const auto e = r.error(); shutdown(); return std::unexpected(e); }
    if (auto r = make_layout(variance_dsl_, sizeof(VariancePC), variance_layout_); !r.has_value())
    { const auto e = r.error(); shutdown(); return std::unexpected(e); }
    if (auto r = make_layout(filter_dsl_, sizeof(FilterPC), filter_layout_); !r.has_value())
    { const auto e = r.error(); shutdown(); return std::unexpected(e); }

    // ---- 4. compute pipelines --------------------------------------------
    auto make_pipeline = [&](cd::rhi::PipelineLayoutHandle layout,
                             cd::rhi::ShaderModuleHandle   module_h,
                             cd::rhi::ComputePipelineHandle& out)
        -> cd::core::Result<void>
    {
        cd::rhi::ComputePipelineDesc d {};
        d.layout = layout;
        d.shader = module_h;
        auto h = device.create_compute_pipeline(d);
        if (!h.has_value())
            return std::unexpected(h.error());
        out = *h;
        return {};
    };

    if (auto r = make_pipeline(moment_layout_, moment_module_, moment_pipeline_); !r.has_value())
    { const auto e = r.error(); shutdown(); return std::unexpected(e); }
    if (auto r = make_pipeline(variance_layout_, variance_module_, variance_pipeline_); !r.has_value())
    { const auto e = r.error(); shutdown(); return std::unexpected(e); }
    if (auto r = make_pipeline(filter_layout_, filter_module_, filter_pipeline_); !r.has_value())
    { const auto e = r.error(); shutdown(); return std::unexpected(e); }

    // ---- 5. descriptor sets ----------------------------------------------
    {
        auto h = device.allocate_descriptor_set(moment_dsl_);
        if (!h.has_value())
        { const auto e = h.error(); shutdown(); return std::unexpected(e); }
        moment_set_ = *h;
    }
    {
        auto h = device.allocate_descriptor_set(variance_dsl_);
        if (!h.has_value())
        { const auto e = h.error(); shutdown(); return std::unexpected(e); }
        variance_set_ = *h;
    }
    {
        auto h = device.allocate_descriptor_set(filter_dsl_);
        if (!h.has_value())
        { const auto e = h.error(); shutdown(); return std::unexpected(e); }
        filter_set_a_ = *h;
    }
    {
        auto h = device.allocate_descriptor_set(filter_dsl_);
        if (!h.has_value())
        { const auto e = h.error(); shutdown(); return std::unexpected(e); }
        filter_set_b_ = *h;
    }

    // ---- 6. internal scratch SSBOs ---------------------------------------
    const std::uint64_t pixel_count =
        static_cast<std::uint64_t>(cfg_.viewport_width)
      * static_cast<std::uint64_t>(cfg_.viewport_height);

    {
        cd::rhi::BufferDesc d {};
        d.size       = pixel_count * 16U;  // vec4 (mu1, mu2, mu1_prev, mu2_prev)
        d.usage      = cd::rhi::BufferUsage::kStorage;
        d.memory     = cd::rhi::MemoryUsage::kGpuOnly;
        d.debug_name = "restir_svgf_moments_sprint4";
        auto h = device.create_buffer(d);
        if (!h.has_value())
        { const auto e = h.error(); shutdown(); return std::unexpected(e); }
        moment_buffer_ = *h;
    }
    {
        cd::rhi::BufferDesc d {};
        d.size       = pixel_count * 4U;   // float variance per pixel
        d.usage      = cd::rhi::BufferUsage::kStorage;
        d.memory     = cd::rhi::MemoryUsage::kGpuOnly;
        d.debug_name = "restir_svgf_variance_sprint4";
        auto h = device.create_buffer(d);
        if (!h.has_value())
        { const auto e = h.error(); shutdown(); return std::unexpected(e); }
        variance_buffer_ = *h;
    }
    {
        cd::rhi::BufferDesc d {};
        d.size       = pixel_count * 4U;   // uint history-length per pixel
        d.usage      = cd::rhi::BufferUsage::kStorage;
        d.memory     = cd::rhi::MemoryUsage::kGpuOnly;
        d.debug_name = "restir_svgf_history_sprint4";
        auto h = device.create_buffer(d);
        if (!h.has_value())
        { const auto e = h.error(); shutdown(); return std::unexpected(e); }
        history_length_buffer_ = *h;
    }

    // Suppress unused-variable warning for `kReservoirStrideBytes` -- kept
    // as a documented constant so a future Sprint-5 reroute can use it.
    (void)kReservoirStrideBytes;

    ready_ = true;
    return {};
}

cd::core::Result<void>
SvgfDenoiser::configure(int filter_iterations, float depth_phi, float normal_phi)
{
    using cd::rhi::rhi_errors::Code;
    using cd::rhi::rhi_errors::make;

    if (device_ == nullptr || cfg_.viewport_width == 0U || cfg_.viewport_height == 0U)
    {
        return std::unexpected(
            make(Code::kInvalidArgument,
                 "restir_di::SvgfDenoiser::configure(int,float,float): no prior viewport"));
    }
    if (filter_iterations <= 0)
    {
        return std::unexpected(
            make(Code::kInvalidArgument,
                 "restir_di::SvgfDenoiser::configure: filter_iterations must be >= 1"));
    }

    // Update the knob mirrors so subsequent execute() picks them up.
    cfg_.filter_iterations = static_cast<std::uint32_t>(filter_iterations);
    cfg_.depth_phi         = depth_phi;
    cfg_.normal_phi        = normal_phi;
    return {};
}

bool SvgfDenoiser::execute(cd::rhi::ICommandBuffer&   cb,
                           cd::rhi::BufferHandle      reservoir_buf,
                           cd::rhi::TextureViewHandle /*depth_tex*/,
                           cd::rhi::TextureViewHandle /*normal_tex*/,
                           cd::rhi::TextureViewHandle /*mesh_id_tex*/,
                           cd::rhi::BufferHandle      out_tex) const
{
    if (!ready_ || device_ == nullptr)
        return false;
    if (!reservoir_buf.is_valid() || !out_tex.is_valid())
        return false;

    // ---- bind descriptors (lazy per execute) -----------------------------
    const std::array<cd::rhi::DescriptorWrite, 3> moment_writes { {
        { .binding = 0U, .array_element = 0U,
          .type = cd::rhi::DescriptorType::kStorageBuffer,
          .buffer = reservoir_buf,         .buffer_offset = 0U, .buffer_range = 0U },
        { .binding = 1U, .array_element = 0U,
          .type = cd::rhi::DescriptorType::kStorageBuffer,
          .buffer = moment_buffer_,        .buffer_offset = 0U, .buffer_range = 0U },
        { .binding = 2U, .array_element = 0U,
          .type = cd::rhi::DescriptorType::kStorageBuffer,
          .buffer = history_length_buffer_, .buffer_offset = 0U, .buffer_range = 0U },
    } };
    (void)device_->update_descriptor_set(moment_set_, moment_writes);

    const std::array<cd::rhi::DescriptorWrite, 4> variance_writes { {
        { .binding = 0U, .array_element = 0U,
          .type = cd::rhi::DescriptorType::kStorageBuffer,
          .buffer = reservoir_buf,         .buffer_offset = 0U, .buffer_range = 0U },
        { .binding = 1U, .array_element = 0U,
          .type = cd::rhi::DescriptorType::kStorageBuffer,
          .buffer = moment_buffer_,        .buffer_offset = 0U, .buffer_range = 0U },
        { .binding = 2U, .array_element = 0U,
          .type = cd::rhi::DescriptorType::kStorageBuffer,
          .buffer = history_length_buffer_, .buffer_offset = 0U, .buffer_range = 0U },
        { .binding = 3U, .array_element = 0U,
          .type = cd::rhi::DescriptorType::kStorageBuffer,
          .buffer = variance_buffer_,      .buffer_offset = 0U, .buffer_range = 0U },
    } };
    (void)device_->update_descriptor_set(variance_set_, variance_writes);

    // Filter A: in=reservoir, out=out_tex.  Filter B: in=out_tex, out=reservoir.
    const std::array<cd::rhi::DescriptorWrite, 3> filter_writes_a { {
        { .binding = 0U, .array_element = 0U,
          .type = cd::rhi::DescriptorType::kStorageBuffer,
          .buffer = reservoir_buf,         .buffer_offset = 0U, .buffer_range = 0U },
        { .binding = 1U, .array_element = 0U,
          .type = cd::rhi::DescriptorType::kStorageBuffer,
          .buffer = out_tex,               .buffer_offset = 0U, .buffer_range = 0U },
        { .binding = 2U, .array_element = 0U,
          .type = cd::rhi::DescriptorType::kStorageBuffer,
          .buffer = variance_buffer_,      .buffer_offset = 0U, .buffer_range = 0U },
    } };
    (void)device_->update_descriptor_set(filter_set_a_, filter_writes_a);

    const std::array<cd::rhi::DescriptorWrite, 3> filter_writes_b { {
        { .binding = 0U, .array_element = 0U,
          .type = cd::rhi::DescriptorType::kStorageBuffer,
          .buffer = out_tex,               .buffer_offset = 0U, .buffer_range = 0U },
        { .binding = 1U, .array_element = 0U,
          .type = cd::rhi::DescriptorType::kStorageBuffer,
          .buffer = reservoir_buf,         .buffer_offset = 0U, .buffer_range = 0U },
        { .binding = 2U, .array_element = 0U,
          .type = cd::rhi::DescriptorType::kStorageBuffer,
          .buffer = variance_buffer_,      .buffer_offset = 0U, .buffer_range = 0U },
    } };
    (void)device_->update_descriptor_set(filter_set_b_, filter_writes_b);

    const std::uint32_t gx = group_count_x(cfg_.viewport_width);
    const std::uint32_t gy = group_count_y(cfg_.viewport_height);

    cb.push_debug_group("restir_di::svgf_denoiser");

    // ---- Pass 1: moment estimation ---------------------------------------
    cb.push_debug_group("svgf_moment_estimate");
    cb.bind_compute_pipeline(moment_pipeline_);
    cb.bind_descriptor_set(0U, moment_set_);
    {
        const MomentPC pc {
            .resolution_x   = cfg_.viewport_width,
            .resolution_y   = cfg_.viewport_height,
            .temporal_alpha = cfg_.temporal_alpha,
            ._pad           = 0.0F,
        };
        cb.push_constants(moment_layout_,
                          cd::rhi::ShaderStage::kCompute,
                          0U,
                          static_cast<std::uint32_t>(sizeof(MomentPC)),
                          &pc);
    }
    cb.dispatch(gx, gy, 1U);
    cb.pop_debug_group();

    // ---- Pass 2: variance estimation -------------------------------------
    cb.push_debug_group("svgf_variance_estimate");
    cb.bind_compute_pipeline(variance_pipeline_);
    cb.bind_descriptor_set(0U, variance_set_);
    {
        const VariancePC pc {
            .resolution_x            = cfg_.viewport_width,
            .resolution_y            = cfg_.viewport_height,
            .short_history_threshold = kSvgfShortHistoryThreshold,
            ._pad                    = 0U,
        };
        cb.push_constants(variance_layout_,
                          cd::rhi::ShaderStage::kCompute,
                          0U,
                          static_cast<std::uint32_t>(sizeof(VariancePC)),
                          &pc);
    }
    cb.dispatch(gx, gy, 1U);
    cb.pop_debug_group();

    // ---- Pass 3: edge-aware A-trous filter (ping-pong) -------------------
    cb.push_debug_group("svgf_filter");
    cb.bind_compute_pipeline(filter_pipeline_);

    float step = 1.0F;
    for (std::uint32_t i = 0U; i < cfg_.filter_iterations; ++i)
    {
        const auto& ds = ((i % 2U) == 0U) ? filter_set_a_ : filter_set_b_;
        const FilterPC pc {
            .resolution_x = cfg_.viewport_width,
            .resolution_y = cfg_.viewport_height,
            .step_width   = step,
            .phi_color    = kSvgfPhiColor,
            .phi_depth    = cfg_.depth_phi,
            .phi_normal   = cfg_.normal_phi,
            .iteration    = i,
            ._pad         = 0U,
        };
        cb.bind_descriptor_set(0U, ds);
        cb.push_constants(filter_layout_,
                          cd::rhi::ShaderStage::kCompute,
                          0U,
                          static_cast<std::uint32_t>(sizeof(FilterPC)),
                          &pc);
        cb.dispatch(gx, gy, 1U);

        step *= 2.0F;
    }
    cb.pop_debug_group();

    cb.pop_debug_group();
    return true;
}

void SvgfDenoiser::shutdown()
{
    if (device_ == nullptr)
    {
        ready_ = false;
        return;
    }

    // Scratch SSBOs first (no dependency on layout / pipeline).
    if (history_length_buffer_.is_valid())
    {
        device_->destroy_buffer(history_length_buffer_);
        history_length_buffer_ = {};
    }
    if (variance_buffer_.is_valid())
    {
        device_->destroy_buffer(variance_buffer_);
        variance_buffer_ = {};
    }
    if (moment_buffer_.is_valid())
    {
        device_->destroy_buffer(moment_buffer_);
        moment_buffer_ = {};
    }

    // Descriptor sets, pipelines, layouts, DSLs, modules per pass.
    auto drop_set = [&](cd::rhi::DescriptorSetHandle& h)
    {
        if (h.is_valid())
        {
            device_->destroy_descriptor_set(h);
            h = {};
        }
    };
    drop_set(filter_set_b_);
    drop_set(filter_set_a_);
    drop_set(variance_set_);
    drop_set(moment_set_);

    auto drop_pipeline = [&](cd::rhi::ComputePipelineHandle& h)
    {
        if (h.is_valid())
        {
            device_->destroy_compute_pipeline(h);
            h = {};
        }
    };
    drop_pipeline(filter_pipeline_);
    drop_pipeline(variance_pipeline_);
    drop_pipeline(moment_pipeline_);

    auto drop_layout = [&](cd::rhi::PipelineLayoutHandle& h)
    {
        if (h.is_valid())
        {
            device_->destroy_pipeline_layout(h);
            h = {};
        }
    };
    drop_layout(filter_layout_);
    drop_layout(variance_layout_);
    drop_layout(moment_layout_);

    auto drop_dsl = [&](cd::rhi::DescriptorSetLayoutHandle& h)
    {
        if (h.is_valid())
        {
            device_->destroy_descriptor_set_layout(h);
            h = {};
        }
    };
    drop_dsl(filter_dsl_);
    drop_dsl(variance_dsl_);
    drop_dsl(moment_dsl_);

    auto drop_module = [&](cd::rhi::ShaderModuleHandle& h)
    {
        if (h.is_valid())
        {
            device_->destroy_shader_module(h);
            h = {};
        }
    };
    drop_module(filter_module_);
    drop_module(variance_module_);
    drop_module(moment_module_);

    ready_  = false;
    device_ = nullptr;
}

}  // namespace cd::restir_di
