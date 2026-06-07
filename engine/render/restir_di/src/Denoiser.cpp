// =============================================================================
// CHROMODYNAMIC -- engine/render/restir_di/src/Denoiser.cpp
// Phase 571 / Sprint-3 -- ReSTIR DI A-trous wavelet denoiser hookpoint.
//
// See cd/restir_di/Denoiser.hpp for design rationale + intended scope of the
// Sprint-3 hand-off. This translation unit owns:
//   * GLSL -> SPIR-V compilation of the Sprint-3 A-trous shader
//   * one descriptor-set layout (2 SSBOs: input + output)
//   * one pipeline layout (+ push-constant block) + one compute pipeline
//   * two descriptor sets (A reads `reservoir_buffer`, writes `output_buffer`;
//     B reads `output_buffer`, writes `reservoir_buffer`) -- swapped per
//     iteration so the chain ping-pongs without re-writing bindings.
//
// Sprint-3 keeps the kernel SSBO-only (no texture bindings) so library-level
// tests run on any Vulkan ICD without scene state. Sprint-4 SVGF promotion is
// deferred -- normal_view + depth_view parameters on execute() are accepted
// but ignored this sprint (matches Sprint-2's motion-vector stripping).
//
// hello_engine is *not* touched (per FROZEN constraint).
// =============================================================================
#include <cd/restir_di/Denoiser.hpp>

#include <cd/rhi/Enums.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/Pipeline.hpp>
#include <cd/shader/Compiler.hpp>

#include <array>
#include <cstdint>
#include <string_view>

namespace cd::restir_di
{

namespace
{

// Sprint-3 A-trous wavelet kernel. One pass = one ping-pong dispatch; the
// host issues `iteration_count` of them, doubling `step_width` each time
// per the Dammertz 2010 schedule. Edge stops are derived from the reservoir
// final_weight() (a proxy for luminance) so we don't need a separate
// G-buffer binding for the Sprint-3 hookpoint.
//
// Reservoir byte layout matches the Sprint-1 / Sprint-2 `DiReservoir`
// definition (32 bytes, 8 x 4B fields) so the SSBO can be the same handle
// the dispatch pass produced.
constexpr std::string_view kRestirAtrousCS = R"glsl(
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
layout(push_constant) uniform PC {
    uvec2 resolution;
    float step_width;
    float sigma_l;       // luminance edge-stop sigma
} pc;

// 5x5 A-trous wavelet stencil (Dammertz 2010, normalised). The host doubles
// `step_width` per iteration so the same stencil covers 1 / 2 / 4 pixel
// reach across the 3-level chain.
const float kKernel[5] = float[5](
    1.0 / 16.0,
    4.0 / 16.0,
    6.0 / 16.0,
    4.0 / 16.0,
    1.0 / 16.0
);

void main() {
    uvec2 px = gl_GlobalInvocationID.xy;
    if (any(greaterThanEqual(px, pc.resolution))) return;

    uint  idx   = px.y * pc.resolution.x + px.x;
    DiReservoir Rc = in_res[idx];

    // Centre luminance for edge stop.
    float lc = luma(Rc.selected.radiance) * reservoir_final_weight(Rc);

    vec3  acc_radiance   = vec3(0.0);
    float acc_weight_sum = 0.0;
    float acc_target_pdf = 0.0;
    uint  acc_M          = 0u;
    float total_w        = 0.0;
    uint  best_light     = Rc.selected.light_index;
    float best_w         = -1.0;

    int   step = int(max(pc.step_width, 1.0));
    float sigl = max(pc.sigma_l, 1e-4);

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
            float wl = exp(-dl / sigl);

            float kw = kKernel[dx + 2] * kKernel[dy + 2];
            float w  = kw * wl;

            acc_radiance   += Rn.selected.radiance * w;
            acc_weight_sum += Rn.weight_sum        * w;
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
    // The A-trous pass does not change the survival count.
    Ro.M   = Rc.M;
    Ro.age = Rc.age;
    out_res[idx] = Ro;
}
)glsl";

constexpr std::uint32_t kGroupSizeXY = 8U;

/// Push-constant block matching the GLSL `PC` declaration above.
struct DenoiserPushConstants
{
    std::uint32_t resolution_x;
    std::uint32_t resolution_y;
    float         step_width;
    float         sigma_l;
};

constexpr float kDefaultSigmaL = 4.0F;

}  // namespace

// --- public static helpers ---------------------------------------------------

std::uint32_t Denoiser::group_count_x(std::uint32_t viewport_w) noexcept
{
    return (viewport_w + kGroupSizeXY - 1U) / kGroupSizeXY;
}

std::uint32_t Denoiser::group_count_y(std::uint32_t viewport_h) noexcept
{
    return (viewport_h + kGroupSizeXY - 1U) / kGroupSizeXY;
}

// --- lifecycle ---------------------------------------------------------------

Denoiser::~Denoiser()
{
    shutdown();
}

cd::core::Result<void>
Denoiser::configure(cd::rhi::IDevice& device, const DenoiserConfig& cfg)
{
    using cd::rhi::rhi_errors::Code;
    using cd::rhi::rhi_errors::make;

    if (cfg.viewport_width == 0U || cfg.viewport_height == 0U)
    {
        return std::unexpected(
            make(Code::kInvalidArgument,
                 "restir_di::Denoiser::configure: zero viewport extent"));
    }
    if (cfg.iteration_count == 0U)
    {
        return std::unexpected(
            make(Code::kInvalidArgument,
                 "restir_di::Denoiser::configure: iteration_count must be >= 1"));
    }

    // Idempotent re-configure: drop any prior resources so callers can call
    // configure() again with a new viewport size without leaking.
    shutdown();

    device_ = &device;
    cfg_    = cfg;

    // ---- 1. compile GLSL -> SPIR-V ----------------------------------------
    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr)
    {
        return std::unexpected(
            make(Code::kBackendInitFailed,
                 "restir_di::Denoiser::configure: glslang compiler unavailable"));
    }

    cd::shader::CompileDesc cd_desc {};
    cd_desc.source      = kRestirAtrousCS;
    cd_desc.stage       = cd::shader::ShaderStage::kCompute;
    cd_desc.source_name = "restir_di_atrous_sprint3.comp";
    auto compiled = compiler->compile(cd_desc);
    if (!compiled.has_value())
    {
        return std::unexpected(
            make(Code::kBackendInitFailed,
                 "restir_di::Denoiser::configure: shader compile failed"));
    }

    // ---- 2. shader module -------------------------------------------------
    cd::rhi::ShaderModuleDesc sm_desc {};
    sm_desc.stage      = cd::rhi::ShaderStage::kCompute;
    sm_desc.code       = compiled->spirv.data();
    sm_desc.code_size  = compiled->spirv.size() * sizeof(std::uint32_t);
    sm_desc.debug_name = "restir_di_atrous_sprint3";
    auto sm = device.create_shader_module(sm_desc);
    if (!sm.has_value())
    {
        const auto& err = sm.error();
        shutdown();
        return std::unexpected(err);
    }
    shader_module_ = *sm;

    // ---- 3. descriptor set layout: 2 SSBOs --------------------------------
    const std::array<cd::rhi::DescriptorSetLayoutBinding, 2> bindings { {
        { .binding = 0U,
          .type    = cd::rhi::DescriptorType::kStorageBuffer,
          .count   = 1U,
          .stages  = cd::rhi::ShaderStage::kCompute },
        { .binding = 1U,
          .type    = cd::rhi::DescriptorType::kStorageBuffer,
          .count   = 1U,
          .stages  = cd::rhi::ShaderStage::kCompute },
    } };
    cd::rhi::DescriptorSetLayoutDesc dsl_desc {};
    dsl_desc.bindings = bindings;
    auto dsl = device.create_descriptor_set_layout(dsl_desc);
    if (!dsl.has_value())
    {
        const auto& err = dsl.error();
        shutdown();
        return std::unexpected(err);
    }
    dsl_ = *dsl;

    // ---- 4. pipeline layout: DSL + push-constants -------------------------
    const std::array<cd::rhi::DescriptorSetLayoutHandle, 1> set_layouts { dsl_ };
    const std::array<cd::rhi::PushConstantRange, 1> push_ranges { {
        { .stages = cd::rhi::ShaderStage::kCompute,
          .offset = 0U,
          .size   = static_cast<std::uint32_t>(sizeof(DenoiserPushConstants)) },
    } };
    cd::rhi::PipelineLayoutDesc pl_desc {};
    pl_desc.set_layouts    = set_layouts;
    pl_desc.push_constants = push_ranges;
    auto pl = device.create_pipeline_layout(pl_desc);
    if (!pl.has_value())
    {
        const auto& err = pl.error();
        shutdown();
        return std::unexpected(err);
    }
    pipeline_layout_ = *pl;

    // ---- 5. compute pipeline ---------------------------------------------
    cd::rhi::ComputePipelineDesc cp_desc {};
    cp_desc.layout = pipeline_layout_;
    cp_desc.shader = shader_module_;
    auto cp = device.create_compute_pipeline(cp_desc);
    if (!cp.has_value())
    {
        const auto& err = cp.error();
        shutdown();
        return std::unexpected(err);
    }
    pipeline_ = *cp;

    // ---- 6. descriptor sets (A + B for ping-pong) ------------------------
    auto ds_a = device.allocate_descriptor_set(dsl_);
    if (!ds_a.has_value())
    {
        const auto& err = ds_a.error();
        shutdown();
        return std::unexpected(err);
    }
    descriptor_set_a_ = *ds_a;

    auto ds_b = device.allocate_descriptor_set(dsl_);
    if (!ds_b.has_value())
    {
        const auto& err = ds_b.error();
        shutdown();
        return std::unexpected(err);
    }
    descriptor_set_b_ = *ds_b;

    ready_ = true;
    return {};
}

void Denoiser::execute(cd::rhi::ICommandBuffer&  cb,
                       cd::rhi::BufferHandle     reservoir_buffer,
                       cd::rhi::TextureViewHandle /*normal_view*/,
                       cd::rhi::TextureViewHandle /*depth_view*/,
                       cd::rhi::BufferHandle     output_buffer) const
{
    if (!ready_)
        return;
    if (!reservoir_buffer.is_valid() || !output_buffer.is_valid())
        return;
    if (device_ == nullptr)
        return;

    // Write bindings every execute() so the caller can hand in new buffers
    // (e.g. a Sprint-4 framegraph reroute) without re-configuring. Sprint-3
    // ignores `normal_view` + `depth_view` -- see file header for rationale.
    const std::array<cd::rhi::DescriptorWrite, 2> writes_a { {
        { .binding       = 0U,
          .array_element = 0U,
          .type          = cd::rhi::DescriptorType::kStorageBuffer,
          .buffer        = reservoir_buffer,
          .buffer_offset = 0U,
          .buffer_range  = 0U },
        { .binding       = 1U,
          .array_element = 0U,
          .type          = cd::rhi::DescriptorType::kStorageBuffer,
          .buffer        = output_buffer,
          .buffer_offset = 0U,
          .buffer_range  = 0U },
    } };
    (void)device_->update_descriptor_set(descriptor_set_a_, writes_a);

    const std::array<cd::rhi::DescriptorWrite, 2> writes_b { {
        { .binding       = 0U,
          .array_element = 0U,
          .type          = cd::rhi::DescriptorType::kStorageBuffer,
          .buffer        = output_buffer,
          .buffer_offset = 0U,
          .buffer_range  = 0U },
        { .binding       = 1U,
          .array_element = 0U,
          .type          = cd::rhi::DescriptorType::kStorageBuffer,
          .buffer        = reservoir_buffer,
          .buffer_offset = 0U,
          .buffer_range  = 0U },
    } };
    (void)device_->update_descriptor_set(descriptor_set_b_, writes_b);

    cb.push_debug_group("restir_di::denoiser_atrous");
    cb.bind_compute_pipeline(pipeline_);

    float step = cfg_.step_width;
    for (std::uint32_t i = 0U; i < cfg_.iteration_count; ++i)
    {
        // Iteration `i` reads the "left" buffer, writes the "right" buffer.
        // Set A: in=reservoir, out=output. Set B: in=output, out=reservoir.
        // Even-numbered iterations use A; odd use B. Net effect after N
        // iterations: if N is even, the final result lives in reservoir;
        // if N is odd, it lives in output (Sprint-3 brief default is 3 so
        // the result lands in `output_buffer`).
        const auto& ds = ((i % 2U) == 0U) ? descriptor_set_a_ : descriptor_set_b_;

        const DenoiserPushConstants pc {
            .resolution_x = cfg_.viewport_width,
            .resolution_y = cfg_.viewport_height,
            .step_width   = step,
            .sigma_l      = kDefaultSigmaL,
        };

        cb.bind_descriptor_set(0U, ds);
        cb.push_constants(pipeline_layout_,
                          cd::rhi::ShaderStage::kCompute,
                          0U,
                          static_cast<std::uint32_t>(sizeof(DenoiserPushConstants)),
                          &pc);
        cb.dispatch(group_count_x(cfg_.viewport_width),
                    group_count_y(cfg_.viewport_height),
                    1U);

        // Dammertz 2010 step schedule: double per iteration.
        step *= 2.0F;
    }

    cb.pop_debug_group();
}

void Denoiser::shutdown()
{
    if (device_ == nullptr)
    {
        ready_ = false;
        return;
    }

    if (descriptor_set_b_.is_valid())
    {
        device_->destroy_descriptor_set(descriptor_set_b_);
        descriptor_set_b_ = {};
    }
    if (descriptor_set_a_.is_valid())
    {
        device_->destroy_descriptor_set(descriptor_set_a_);
        descriptor_set_a_ = {};
    }
    if (pipeline_.is_valid())
    {
        device_->destroy_compute_pipeline(pipeline_);
        pipeline_ = {};
    }
    if (pipeline_layout_.is_valid())
    {
        device_->destroy_pipeline_layout(pipeline_layout_);
        pipeline_layout_ = {};
    }
    if (dsl_.is_valid())
    {
        device_->destroy_descriptor_set_layout(dsl_);
        dsl_ = {};
    }
    if (shader_module_.is_valid())
    {
        device_->destroy_shader_module(shader_module_);
        shader_module_ = {};
    }
    ready_  = false;
    device_ = nullptr;
}

}  // namespace cd::restir_di
