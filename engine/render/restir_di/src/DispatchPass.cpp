// =============================================================================
// CHROMODYNAMIC -- engine/render/restir_di/src/DispatchPass.cpp
// Phase 550 / Sprint-1 -- ReSTIR DI initial-candidate dispatch (library impl).
// Phase 561 / Sprint-2 -- temporal + spatial reuse passes (library impl).
//
// See cd/restir_di/DispatchPass.hpp for the design rationale and intended
// scope of the Sprint-1 / Sprint-2 hand-off. This translation unit owns:
//   * GLSL -> SPIR-V compilation of the Sprint-1 sample shader and the
//     Sprint-2 temporal + spatial reuse shaders
//   * three descriptor-set layouts (one per dispatch)
//   * three pipeline layouts + three compute pipelines
//   * four reservoir SSBO allocations + descriptor bindings
//   * the record() path (Sprint-1) and the
//     execute_temporal_reuse() / execute_spatial_reuse() paths (Sprint-2)
//
// All sprint shaders are stripped variants of their production counterparts
// in Reservoir.hpp:
//   * `#extension GL_EXT_ray_tracing` removed -- no AS is wired yet; keeping
//     the extension would force the consumer's Vulkan device to declare it,
//     which fails on integrated GPUs without RT support. Sprint-3+ will
//     reinstate the extension when the visibility-ray bind goes in.
//   * G-buffer + motion-vector + light-buffer bindings stripped -- the
//     library-level tests run without scene state, so the kernels operate
//     purely on the reservoir SSBOs. Same DiSample / DiReservoir byte layout
//     as the production shaders so Sprint-3+ can swap the kernels in-place
//     without re-laying-out the SSBOs.
//
// hello_engine is *not* touched (per phase 550 / 561 brief).
// =============================================================================
#include <cd/restir_di/DispatchPass.hpp>

#include <cd/rhi/Enums.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/Pipeline.hpp>
#include <cd/shader/Compiler.hpp>

#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

namespace cd::restir_di
{

namespace
{

// Sprint-1 sample-CS source. Byte-compatible reservoir layout with
// `kRestirDiSampleCS` (Reservoir.hpp) but with the RT extension + scene
// bindings stripped — see file header for the rationale.
constexpr std::string_view kSprint1SampleCS = R"glsl(
#version 460
layout(local_size_x = 16, local_size_y = 16) in;

struct DiSample    { uint light_index; float target_pdf; vec3 radiance; };
struct DiReservoir { DiSample selected; float weight_sum; uint M; uint age; };

layout(set = 0, binding = 0, std430) buffer ReservoirOut {
    DiReservoir reservoirs[];
};
layout(push_constant) uniform PC {
    uvec2 resolution;
    uint  light_count;
    uint  frame_index;
    uint  candidates;
} pc;

void main() {
    uvec2 px = gl_GlobalInvocationID.xy;
    if (any(greaterThanEqual(px, pc.resolution))) return;

    uint idx = px.y * pc.resolution.x + px.x;

    // Sprint-1: zero-initialise the reservoir. Sprint-2 will swap in the
    // full WRS candidate-generation loop (with G-buffer + light SSBO
    // bindings + visibility ray) on top of the same byte layout.
    DiReservoir R;
    R.selected  = DiSample(0u, 0.0, vec3(0.0));
    R.weight_sum = 0.0;
    R.M          = 0u;
    R.age        = 0u;
    reservoirs[idx] = R;
}
)glsl";

constexpr std::uint32_t kGroupSizeXY = 16U;

/// Push-constant block matching the GLSL `PC` declaration above. Kept here
/// so a single edit changes both the shader and the host-side push.
struct PushConstants
{
    std::uint32_t resolution_x;
    std::uint32_t resolution_y;
    std::uint32_t light_count;
    std::uint32_t frame_index;
    std::uint32_t candidates;
};

// ---- Sprint-2: temporal reuse -----------------------------------------------

// Stripped variant of kRestirDiTemporalReuseCS (Reservoir.hpp). Byte-compatible
// reservoir layout but with the u_MotionVectors sampler binding removed --
// without motion vectors there is nothing to reproject, so the kernel falls
// back to same-pixel temporal blending (Bitterli 2020 eq. 6 with prev pixel ==
// current pixel). Sprint-3+ will reinstate the motion-vector binding + the
// reprojection branch once the framegraph G-buffer ABI is settled.
constexpr std::string_view kSprint2TemporalReuseCS = R"glsl(
#version 460
layout(local_size_x = 8, local_size_y = 8) in;

struct DiSample    { uint light_index; float target_pdf; vec3 radiance; };
struct DiReservoir { DiSample selected; float weight_sum; uint M; uint age; };

float reservoir_final_weight(DiReservoir r) {
    if (r.M == 0u || r.selected.target_pdf <= 0.0) return 0.0;
    return r.weight_sum / (float(r.M) * r.selected.target_pdf);
}

layout(set = 0, binding = 0, std430) readonly buffer ReservoirCurrent {
    DiReservoir cur_res[];
};
layout(set = 0, binding = 1, std430) readonly buffer ReservoirPrevious {
    DiReservoir prev_res[];
};
layout(set = 0, binding = 2, std430) buffer ReservoirOut {
    DiReservoir out_res[];
};
layout(push_constant) uniform PC {
    uvec2 resolution;
    uint  frame_index;
    uint  M_cap;
} pc;

uint pcg(uint v) {
    v = v * 747796405u + 2891336453u;
    v = ((v >> ((v >> 28u) + 4u)) ^ v) * 277803737u;
    return (v >> 22u) ^ v;
}
float rand(inout uint state) {
    state = pcg(state);
    return float(state) * (1.0 / 4294967296.0);
}

void main() {
    uvec2 px = gl_GlobalInvocationID.xy;
    if (any(greaterThanEqual(px, pc.resolution))) return;

    uint idx = px.y * pc.resolution.x + px.x;
    DiReservoir cur    = cur_res[idx];
    DiReservoir prev   = prev_res[idx];
    DiReservoir merged = cur;

    // Discard stale history.
    if (prev.age > 30u) {
        prev.weight_sum = 0.0;
        prev.M = 0u;
    }

    float p_hat = prev.selected.target_pdf;
    float w     = p_hat * reservoir_final_weight(prev) * float(prev.M);
    merged.weight_sum += w;
    merged.M          += prev.M;
    uint seed = pcg(idx ^ (pc.frame_index * 16807u));
    if (merged.weight_sum > 0.0 && rand(seed) < w / merged.weight_sum)
        merged.selected = prev.selected;

    // M cap (Bitterli history clamp).
    if (merged.M > pc.M_cap) {
        merged.weight_sum *= float(pc.M_cap) / float(merged.M);
        merged.M = pc.M_cap;
    }

    merged.age = cur.age + 1u;
    out_res[idx] = merged;
}
)glsl";

// Stripped variant of kRestirDiSpatialReuseCS (Reservoir.hpp). Byte-compatible
// reservoir layout but with the u_GBufNormal sampler binding removed -- the
// receiver/donor normal-dissimilarity test is therefore skipped. Sprint-3+
// reinstates the binding plus the dot-product gate once the framegraph
// G-buffer normal target is exposed at this seam.
constexpr std::string_view kSprint2SpatialReuseCS = R"glsl(
#version 460
layout(local_size_x = 8, local_size_y = 8) in;

struct DiSample    { uint light_index; float target_pdf; vec3 radiance; };
struct DiReservoir { DiSample selected; float weight_sum; uint M; uint age; };

float reservoir_final_weight(DiReservoir r) {
    if (r.M == 0u || r.selected.target_pdf <= 0.0) return 0.0;
    return r.weight_sum / (float(r.M) * r.selected.target_pdf);
}

layout(set = 0, binding = 0, std430) readonly buffer ReservoirIn {
    DiReservoir in_res[];
};
layout(set = 0, binding = 1, std430) buffer ReservoirOut {
    DiReservoir out_res[];
};
layout(push_constant) uniform PC {
    uvec2 resolution;
    uint  frame_index;
    uint  spatial_radius;
    uint  spatial_taps;
} pc;

uint pcg(uint v) {
    v = v * 747796405u + 2891336453u;
    v = ((v >> ((v >> 28u) + 4u)) ^ v) * 277803737u;
    return (v >> 22u) ^ v;
}
float rand(inout uint state) {
    state = pcg(state);
    return float(state) * (1.0 / 4294967296.0);
}

const float kPi = 3.14159265358979;

void main() {
    uvec2 px = gl_GlobalInvocationID.xy;
    if (any(greaterThanEqual(px, pc.resolution))) return;

    uint idx  = px.y * pc.resolution.x + px.x;
    uint seed = pcg(idx ^ (pc.frame_index * 22695477u));

    DiReservoir R = in_res[idx];

    float theta0 = rand(seed) * 2.0 * kPi;

    for (uint t = 0u; t < pc.spatial_taps; ++t) {
        float theta = theta0 + float(t) * (2.0 * kPi / float(pc.spatial_taps));
        float r     = float(pc.spatial_radius) * sqrt(rand(seed));
        ivec2 npx   = ivec2(vec2(px) + r * vec2(cos(theta), sin(theta)));
        if (any(lessThan(npx, ivec2(0))) || any(greaterThanEqual(npx, ivec2(pc.resolution))))
            continue;

        uint nidx = uint(npx.y) * pc.resolution.x + uint(npx.x);
        DiReservoir nei = in_res[nidx];
        if (nei.M == 0u) continue;

        // Re-evaluate p_hat in receiver domain. Without the G-buffer normal
        // binding the receiver/donor dissimilarity test is omitted; Sprint-3+
        // restores it (see file header).
        float p_hat = nei.selected.target_pdf;
        float w = p_hat * reservoir_final_weight(nei) * float(nei.M);
        R.weight_sum += w;
        R.M          += nei.M;
        if (R.weight_sum > 0.0 && rand(seed) < w / R.weight_sum)
            R.selected = nei.selected;
    }

    out_res[idx] = R;
}
)glsl";

constexpr std::uint32_t kReuseGroupSizeXY = 8U;

// Push-constant block matching kSprint2TemporalReuseCS's PC.
struct TemporalPushConstants
{
    std::uint32_t resolution_x;
    std::uint32_t resolution_y;
    std::uint32_t frame_index;
    std::uint32_t m_cap;
};

// Push-constant block matching kSprint2SpatialReuseCS's PC.
struct SpatialPushConstants
{
    std::uint32_t resolution_x;
    std::uint32_t resolution_y;
    std::uint32_t frame_index;
    std::uint32_t spatial_radius;
    std::uint32_t spatial_taps;
};

}  // namespace

// --- public static helpers ---------------------------------------------------

std::uint32_t DispatchPass::group_count_x(std::uint32_t viewport_w) noexcept
{
    // Sprint-1 brief: dispatch at viewport_w/16. Use ceiling division so a
    // non-multiple-of-16 viewport still covers every pixel; the shader
    // bounds-checks the trailing groups against `pc.resolution`.
    return (viewport_w + kGroupSizeXY - 1U) / kGroupSizeXY;
}

std::uint32_t DispatchPass::group_count_y(std::uint32_t viewport_h) noexcept
{
    return (viewport_h + kGroupSizeXY - 1U) / kGroupSizeXY;
}

std::uint64_t DispatchPass::reservoir_buffer_size(std::uint32_t w, std::uint32_t h) noexcept
{
    return static_cast<std::uint64_t>(w) * static_cast<std::uint64_t>(h) * sizeof(GpuReservoir);
}

std::uint32_t DispatchPass::reuse_group_count_x(std::uint32_t viewport_w) noexcept
{
    // Sprint-2: reuse shaders dispatch at viewport_w / 8. Ceiling division so
    // a non-multiple-of-8 viewport still covers every pixel; the GLSL kernel
    // bounds-checks the trailing groups against `pc.resolution`.
    return (viewport_w + kReuseGroupSizeXY - 1U) / kReuseGroupSizeXY;
}

std::uint32_t DispatchPass::reuse_group_count_y(std::uint32_t viewport_h) noexcept
{
    return (viewport_h + kReuseGroupSizeXY - 1U) / kReuseGroupSizeXY;
}

// --- lifecycle ---------------------------------------------------------------

DispatchPass::~DispatchPass()
{
    shutdown();
}

cd::core::Result<void>
DispatchPass::prepare(cd::rhi::IDevice& device, const DispatchConfig& cfg)
{
    using cd::rhi::rhi_errors::Code;
    using cd::rhi::rhi_errors::make;

    if (cfg.viewport_width == 0U || cfg.viewport_height == 0U)
    {
        return std::unexpected(
            make(Code::kInvalidArgument,
                 "restir_di::DispatchPass::prepare: zero viewport extent"));
    }

    // Idempotent re-prepare: drop any prior resources so callers can call
    // prepare() again with a new viewport size without leaking.
    shutdown();

    device_ = &device;
    cfg_    = cfg;

    // ---- 1. compile GLSL -> SPIR-V ----------------------------------------
    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr)
    {
        return std::unexpected(
            make(Code::kBackendInitFailed,
                 "restir_di::DispatchPass::prepare: glslang compiler unavailable"));
    }

    cd::shader::CompileDesc cd_desc {};
    cd_desc.source       = kSprint1SampleCS;
    cd_desc.stage        = cd::shader::ShaderStage::kCompute;
    cd_desc.source_name  = "restir_di_sample_sprint1.comp";
    auto compiled = compiler->compile(cd_desc);
    if (!compiled.has_value())
    {
        return std::unexpected(
            make(Code::kBackendInitFailed,
                 "restir_di::DispatchPass::prepare: shader compile failed"));
    }

    // ---- 2. shader module -------------------------------------------------
    cd::rhi::ShaderModuleDesc sm_desc {};
    sm_desc.stage      = cd::rhi::ShaderStage::kCompute;
    sm_desc.code       = compiled->spirv.data();
    sm_desc.code_size  = compiled->spirv.size() * sizeof(std::uint32_t);
    sm_desc.debug_name = "restir_di_sample_sprint1";
    auto sm = device.create_shader_module(sm_desc);
    if (!sm.has_value())
    {
        const auto err = sm.error();
        shutdown();
        return std::unexpected(err);
    }
    shader_module_ = *sm;

    // ---- 3. descriptor set layout: 1 SSBO ---------------------------------
    const std::array<cd::rhi::DescriptorSetLayoutBinding, 1> bindings { {
        { .binding = 0U,
          .type    = cd::rhi::DescriptorType::kStorageBuffer,
          .count   = 1U,
          .stages  = cd::rhi::ShaderStage::kCompute },
    } };
    cd::rhi::DescriptorSetLayoutDesc dsl_desc {};
    dsl_desc.bindings = bindings;
    auto dsl = device.create_descriptor_set_layout(dsl_desc);
    if (!dsl.has_value())
    {
        const auto err = dsl.error();
        shutdown();
        return std::unexpected(err);
    }
    dsl_ = *dsl;

    // ---- 4. pipeline layout: DSL + push-constants -------------------------
    const std::array<cd::rhi::DescriptorSetLayoutHandle, 1> set_layouts { dsl_ };
    const std::array<cd::rhi::PushConstantRange, 1> push_ranges { {
        { .stages = cd::rhi::ShaderStage::kCompute,
          .offset = 0U,
          .size   = static_cast<std::uint32_t>(sizeof(PushConstants)) },
    } };
    cd::rhi::PipelineLayoutDesc pl_desc {};
    pl_desc.set_layouts    = set_layouts;
    pl_desc.push_constants = push_ranges;
    auto pl = device.create_pipeline_layout(pl_desc);
    if (!pl.has_value())
    {
        const auto err = pl.error();
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
        const auto err = cp.error();
        shutdown();
        return std::unexpected(err);
    }
    pipeline_ = *cp;

    // ---- 6. reservoir SSBO -----------------------------------------------
    cd::rhi::BufferDesc rb_desc {};
    rb_desc.size       = reservoir_buffer_size(cfg.viewport_width, cfg.viewport_height);
    rb_desc.usage      = cd::rhi::BufferUsage::kStorage;
    rb_desc.memory     = cd::rhi::MemoryUsage::kGpuOnly;
    rb_desc.debug_name = "restir_di_reservoirs";
    auto rb = device.create_buffer(rb_desc);
    if (!rb.has_value())
    {
        const auto err = rb.error();
        shutdown();
        return std::unexpected(err);
    }
    reservoir_buffer_ = *rb;

    // ---- 7. descriptor set + bind reservoir SSBO --------------------------
    auto ds = device.allocate_descriptor_set(dsl_);
    if (!ds.has_value())
    {
        const auto err = ds.error();
        shutdown();
        return std::unexpected(err);
    }
    descriptor_set_ = *ds;

    const std::array<cd::rhi::DescriptorWrite, 1> writes { {
        { .binding       = 0U,
          .array_element = 0U,
          .type          = cd::rhi::DescriptorType::kStorageBuffer,
          .buffer        = reservoir_buffer_,
          .buffer_offset = 0U,
          .buffer_range  = 0U },  // whole buffer
    } };
    auto upd = device.update_descriptor_set(descriptor_set_, writes);
    if (!upd.has_value())
    {
        const auto err = upd.error();
        shutdown();
        return std::unexpected(err);
    }

    // =======================================================================
    // Sprint-2: temporal reuse pass
    // =======================================================================

    // ---- 8. previous + temporal reservoir SSBOs --------------------------
    cd::rhi::BufferDesc prev_desc {};
    prev_desc.size       = reservoir_buffer_size(cfg.viewport_width, cfg.viewport_height);
    prev_desc.usage      = cd::rhi::BufferUsage::kStorage;
    prev_desc.memory     = cd::rhi::MemoryUsage::kGpuOnly;
    prev_desc.debug_name = "restir_di_reservoirs_previous";
    auto prev_buf = device.create_buffer(prev_desc);
    if (!prev_buf.has_value())
    {
        const auto err = prev_buf.error();
        shutdown();
        return std::unexpected(err);
    }
    previous_reservoir_buffer_ = *prev_buf;

    cd::rhi::BufferDesc temporal_desc {};
    temporal_desc.size       = reservoir_buffer_size(cfg.viewport_width, cfg.viewport_height);
    temporal_desc.usage      = cd::rhi::BufferUsage::kStorage;
    temporal_desc.memory     = cd::rhi::MemoryUsage::kGpuOnly;
    temporal_desc.debug_name = "restir_di_reservoirs_temporal";
    auto temporal_buf = device.create_buffer(temporal_desc);
    if (!temporal_buf.has_value())
    {
        const auto err = temporal_buf.error();
        shutdown();
        return std::unexpected(err);
    }
    temporal_reservoir_buffer_ = *temporal_buf;

    // ---- 9. compile temporal reuse shader --------------------------------
    cd::shader::CompileDesc temporal_cd_desc {};
    temporal_cd_desc.source       = kSprint2TemporalReuseCS;
    temporal_cd_desc.stage        = cd::shader::ShaderStage::kCompute;
    temporal_cd_desc.source_name  = "restir_di_temporal_reuse_sprint2.comp";
    auto temporal_compiled = compiler->compile(temporal_cd_desc);
    if (!temporal_compiled.has_value())
    {
        shutdown();
        return std::unexpected(
            make(Code::kBackendInitFailed,
                 "restir_di::DispatchPass::prepare: temporal reuse shader compile failed"));
    }

    cd::rhi::ShaderModuleDesc temporal_sm_desc {};
    temporal_sm_desc.stage      = cd::rhi::ShaderStage::kCompute;
    temporal_sm_desc.code       = temporal_compiled->spirv.data();
    temporal_sm_desc.code_size  = temporal_compiled->spirv.size() * sizeof(std::uint32_t);
    temporal_sm_desc.debug_name = "restir_di_temporal_reuse_sprint2";
    auto temporal_sm = device.create_shader_module(temporal_sm_desc);
    if (!temporal_sm.has_value())
    {
        const auto err = temporal_sm.error();
        shutdown();
        return std::unexpected(err);
    }
    temporal_shader_module_ = *temporal_sm;

    // ---- 10. temporal descriptor-set layout: 3 SSBOs ---------------------
    const std::array<cd::rhi::DescriptorSetLayoutBinding, 3> temporal_bindings { {
        { .binding = 0U,
          .type    = cd::rhi::DescriptorType::kStorageBuffer,
          .count   = 1U,
          .stages  = cd::rhi::ShaderStage::kCompute },
        { .binding = 1U,
          .type    = cd::rhi::DescriptorType::kStorageBuffer,
          .count   = 1U,
          .stages  = cd::rhi::ShaderStage::kCompute },
        { .binding = 2U,
          .type    = cd::rhi::DescriptorType::kStorageBuffer,
          .count   = 1U,
          .stages  = cd::rhi::ShaderStage::kCompute },
    } };
    cd::rhi::DescriptorSetLayoutDesc temporal_dsl_desc {};
    temporal_dsl_desc.bindings = temporal_bindings;
    auto temporal_dsl = device.create_descriptor_set_layout(temporal_dsl_desc);
    if (!temporal_dsl.has_value())
    {
        const auto err = temporal_dsl.error();
        shutdown();
        return std::unexpected(err);
    }
    temporal_dsl_ = *temporal_dsl;

    // ---- 11. temporal pipeline layout ------------------------------------
    const std::array<cd::rhi::DescriptorSetLayoutHandle, 1> temporal_set_layouts { temporal_dsl_ };
    const std::array<cd::rhi::PushConstantRange, 1> temporal_push_ranges { {
        { .stages = cd::rhi::ShaderStage::kCompute,
          .offset = 0U,
          .size   = static_cast<std::uint32_t>(sizeof(TemporalPushConstants)) },
    } };
    cd::rhi::PipelineLayoutDesc temporal_pl_desc {};
    temporal_pl_desc.set_layouts    = temporal_set_layouts;
    temporal_pl_desc.push_constants = temporal_push_ranges;
    auto temporal_pl = device.create_pipeline_layout(temporal_pl_desc);
    if (!temporal_pl.has_value())
    {
        const auto err = temporal_pl.error();
        shutdown();
        return std::unexpected(err);
    }
    temporal_pipeline_layout_ = *temporal_pl;

    // ---- 12. temporal compute pipeline -----------------------------------
    cd::rhi::ComputePipelineDesc temporal_cp_desc {};
    temporal_cp_desc.layout = temporal_pipeline_layout_;
    temporal_cp_desc.shader = temporal_shader_module_;
    auto temporal_cp = device.create_compute_pipeline(temporal_cp_desc);
    if (!temporal_cp.has_value())
    {
        const auto err = temporal_cp.error();
        shutdown();
        return std::unexpected(err);
    }
    temporal_pipeline_ = *temporal_cp;

    // ---- 13. temporal descriptor set + bindings --------------------------
    auto temporal_ds = device.allocate_descriptor_set(temporal_dsl_);
    if (!temporal_ds.has_value())
    {
        const auto err = temporal_ds.error();
        shutdown();
        return std::unexpected(err);
    }
    temporal_descriptor_set_ = *temporal_ds;

    const std::array<cd::rhi::DescriptorWrite, 3> temporal_writes { {
        { .binding       = 0U,
          .array_element = 0U,
          .type          = cd::rhi::DescriptorType::kStorageBuffer,
          .buffer        = reservoir_buffer_,
          .buffer_offset = 0U,
          .buffer_range  = 0U },
        { .binding       = 1U,
          .array_element = 0U,
          .type          = cd::rhi::DescriptorType::kStorageBuffer,
          .buffer        = previous_reservoir_buffer_,
          .buffer_offset = 0U,
          .buffer_range  = 0U },
        { .binding       = 2U,
          .array_element = 0U,
          .type          = cd::rhi::DescriptorType::kStorageBuffer,
          .buffer        = temporal_reservoir_buffer_,
          .buffer_offset = 0U,
          .buffer_range  = 0U },
    } };
    auto temporal_upd = device.update_descriptor_set(temporal_descriptor_set_, temporal_writes);
    if (!temporal_upd.has_value())
    {
        const auto err = temporal_upd.error();
        shutdown();
        return std::unexpected(err);
    }

    // =======================================================================
    // Sprint-2: spatial reuse pass
    // =======================================================================

    // ---- 14. spatial reservoir SSBO --------------------------------------
    cd::rhi::BufferDesc spatial_desc {};
    spatial_desc.size       = reservoir_buffer_size(cfg.viewport_width, cfg.viewport_height);
    spatial_desc.usage      = cd::rhi::BufferUsage::kStorage;
    spatial_desc.memory     = cd::rhi::MemoryUsage::kGpuOnly;
    spatial_desc.debug_name = "restir_di_reservoirs_spatial";
    auto spatial_buf = device.create_buffer(spatial_desc);
    if (!spatial_buf.has_value())
    {
        const auto err = spatial_buf.error();
        shutdown();
        return std::unexpected(err);
    }
    spatial_reservoir_buffer_ = *spatial_buf;

    // ---- 15. compile spatial reuse shader --------------------------------
    cd::shader::CompileDesc spatial_cd_desc {};
    spatial_cd_desc.source       = kSprint2SpatialReuseCS;
    spatial_cd_desc.stage        = cd::shader::ShaderStage::kCompute;
    spatial_cd_desc.source_name  = "restir_di_spatial_reuse_sprint2.comp";
    auto spatial_compiled = compiler->compile(spatial_cd_desc);
    if (!spatial_compiled.has_value())
    {
        shutdown();
        return std::unexpected(
            make(Code::kBackendInitFailed,
                 "restir_di::DispatchPass::prepare: spatial reuse shader compile failed"));
    }

    cd::rhi::ShaderModuleDesc spatial_sm_desc {};
    spatial_sm_desc.stage      = cd::rhi::ShaderStage::kCompute;
    spatial_sm_desc.code       = spatial_compiled->spirv.data();
    spatial_sm_desc.code_size  = spatial_compiled->spirv.size() * sizeof(std::uint32_t);
    spatial_sm_desc.debug_name = "restir_di_spatial_reuse_sprint2";
    auto spatial_sm = device.create_shader_module(spatial_sm_desc);
    if (!spatial_sm.has_value())
    {
        const auto err = spatial_sm.error();
        shutdown();
        return std::unexpected(err);
    }
    spatial_shader_module_ = *spatial_sm;

    // ---- 16. spatial descriptor-set layout: 2 SSBOs ----------------------
    const std::array<cd::rhi::DescriptorSetLayoutBinding, 2> spatial_bindings { {
        { .binding = 0U,
          .type    = cd::rhi::DescriptorType::kStorageBuffer,
          .count   = 1U,
          .stages  = cd::rhi::ShaderStage::kCompute },
        { .binding = 1U,
          .type    = cd::rhi::DescriptorType::kStorageBuffer,
          .count   = 1U,
          .stages  = cd::rhi::ShaderStage::kCompute },
    } };
    cd::rhi::DescriptorSetLayoutDesc spatial_dsl_desc {};
    spatial_dsl_desc.bindings = spatial_bindings;
    auto spatial_dsl = device.create_descriptor_set_layout(spatial_dsl_desc);
    if (!spatial_dsl.has_value())
    {
        const auto err = spatial_dsl.error();
        shutdown();
        return std::unexpected(err);
    }
    spatial_dsl_ = *spatial_dsl;

    // ---- 17. spatial pipeline layout -------------------------------------
    const std::array<cd::rhi::DescriptorSetLayoutHandle, 1> spatial_set_layouts { spatial_dsl_ };
    const std::array<cd::rhi::PushConstantRange, 1> spatial_push_ranges { {
        { .stages = cd::rhi::ShaderStage::kCompute,
          .offset = 0U,
          .size   = static_cast<std::uint32_t>(sizeof(SpatialPushConstants)) },
    } };
    cd::rhi::PipelineLayoutDesc spatial_pl_desc {};
    spatial_pl_desc.set_layouts    = spatial_set_layouts;
    spatial_pl_desc.push_constants = spatial_push_ranges;
    auto spatial_pl = device.create_pipeline_layout(spatial_pl_desc);
    if (!spatial_pl.has_value())
    {
        const auto err = spatial_pl.error();
        shutdown();
        return std::unexpected(err);
    }
    spatial_pipeline_layout_ = *spatial_pl;

    // ---- 18. spatial compute pipeline ------------------------------------
    cd::rhi::ComputePipelineDesc spatial_cp_desc {};
    spatial_cp_desc.layout = spatial_pipeline_layout_;
    spatial_cp_desc.shader = spatial_shader_module_;
    auto spatial_cp = device.create_compute_pipeline(spatial_cp_desc);
    if (!spatial_cp.has_value())
    {
        const auto err = spatial_cp.error();
        shutdown();
        return std::unexpected(err);
    }
    spatial_pipeline_ = *spatial_cp;

    // ---- 19. spatial descriptor set + bindings ---------------------------
    auto spatial_ds = device.allocate_descriptor_set(spatial_dsl_);
    if (!spatial_ds.has_value())
    {
        const auto err = spatial_ds.error();
        shutdown();
        return std::unexpected(err);
    }
    spatial_descriptor_set_ = *spatial_ds;

    const std::array<cd::rhi::DescriptorWrite, 2> spatial_writes { {
        { .binding       = 0U,
          .array_element = 0U,
          .type          = cd::rhi::DescriptorType::kStorageBuffer,
          .buffer        = temporal_reservoir_buffer_,
          .buffer_offset = 0U,
          .buffer_range  = 0U },
        { .binding       = 1U,
          .array_element = 0U,
          .type          = cd::rhi::DescriptorType::kStorageBuffer,
          .buffer        = spatial_reservoir_buffer_,
          .buffer_offset = 0U,
          .buffer_range  = 0U },
    } };
    auto spatial_upd = device.update_descriptor_set(spatial_descriptor_set_, spatial_writes);
    if (!spatial_upd.has_value())
    {
        const auto err = spatial_upd.error();
        shutdown();
        return std::unexpected(err);
    }

    ready_ = true;
    return {};
}

void DispatchPass::record(cd::rhi::ICommandBuffer& cb) const
{
    if (!ready_)
        return;

    const PushConstants pc {
        .resolution_x = cfg_.viewport_width,
        .resolution_y = cfg_.viewport_height,
        .light_count  = cfg_.light_count,
        .frame_index  = 0U,
        .candidates   = cfg_.candidates,
    };

    cb.push_debug_group("restir_di::sample");
    cb.bind_compute_pipeline(pipeline_);
    cb.bind_descriptor_set(0U, descriptor_set_);
    cb.push_constants(pipeline_layout_,
                      cd::rhi::ShaderStage::kCompute,
                      0U,
                      static_cast<std::uint32_t>(sizeof(PushConstants)),
                      &pc);
    cb.dispatch(group_count_x(cfg_.viewport_width),
                group_count_y(cfg_.viewport_height),
                1U);
    cb.pop_debug_group();
}

void DispatchPass::execute_temporal_reuse(cd::rhi::ICommandBuffer& cb,
                                          std::uint32_t frame_index) const
{
    if (!ready_)
        return;

    // Bitterli 2020 recommends an M cap of 20 x M_initial so the temporal
    // history can't dominate fresh samples after a disocclusion. We mirror
    // that here (cfg_.candidates is the initial M).
    const std::uint32_t m_cap = cfg_.candidates * 20U;

    const TemporalPushConstants pc {
        .resolution_x = cfg_.viewport_width,
        .resolution_y = cfg_.viewport_height,
        .frame_index  = frame_index,
        .m_cap        = m_cap,
    };

    cb.push_debug_group("restir_di::temporal_reuse");
    cb.bind_compute_pipeline(temporal_pipeline_);
    cb.bind_descriptor_set(0U, temporal_descriptor_set_);
    cb.push_constants(temporal_pipeline_layout_,
                      cd::rhi::ShaderStage::kCompute,
                      0U,
                      static_cast<std::uint32_t>(sizeof(TemporalPushConstants)),
                      &pc);
    cb.dispatch(reuse_group_count_x(cfg_.viewport_width),
                reuse_group_count_y(cfg_.viewport_height),
                1U);
    cb.pop_debug_group();
}

void DispatchPass::execute_spatial_reuse(cd::rhi::ICommandBuffer& cb,
                                         std::uint32_t frame_index) const
{
    if (!ready_)
        return;

    // Bitterli 2020 spatial-reuse defaults: 5 taps, ~30-pixel disc radius.
    constexpr std::uint32_t kSpatialRadius = 30U;
    constexpr std::uint32_t kSpatialTaps   = 5U;

    const SpatialPushConstants pc {
        .resolution_x   = cfg_.viewport_width,
        .resolution_y   = cfg_.viewport_height,
        .frame_index    = frame_index,
        .spatial_radius = kSpatialRadius,
        .spatial_taps   = kSpatialTaps,
    };

    cb.push_debug_group("restir_di::spatial_reuse");
    cb.bind_compute_pipeline(spatial_pipeline_);
    cb.bind_descriptor_set(0U, spatial_descriptor_set_);
    cb.push_constants(spatial_pipeline_layout_,
                      cd::rhi::ShaderStage::kCompute,
                      0U,
                      static_cast<std::uint32_t>(sizeof(SpatialPushConstants)),
                      &pc);
    cb.dispatch(reuse_group_count_x(cfg_.viewport_width),
                reuse_group_count_y(cfg_.viewport_height),
                1U);
    cb.pop_debug_group();
}

void DispatchPass::shutdown()
{
    if (device_ == nullptr)
    {
        ready_ = false;
        return;
    }

    // ---- Sprint-2: spatial reuse resources --------------------------------
    if (spatial_descriptor_set_.is_valid())
    {
        device_->destroy_descriptor_set(spatial_descriptor_set_);
        spatial_descriptor_set_ = {};
    }
    if (spatial_pipeline_.is_valid())
    {
        device_->destroy_compute_pipeline(spatial_pipeline_);
        spatial_pipeline_ = {};
    }
    if (spatial_pipeline_layout_.is_valid())
    {
        device_->destroy_pipeline_layout(spatial_pipeline_layout_);
        spatial_pipeline_layout_ = {};
    }
    if (spatial_dsl_.is_valid())
    {
        device_->destroy_descriptor_set_layout(spatial_dsl_);
        spatial_dsl_ = {};
    }
    if (spatial_shader_module_.is_valid())
    {
        device_->destroy_shader_module(spatial_shader_module_);
        spatial_shader_module_ = {};
    }
    if (spatial_reservoir_buffer_.is_valid())
    {
        device_->destroy_buffer(spatial_reservoir_buffer_);
        spatial_reservoir_buffer_ = {};
    }

    // ---- Sprint-2: temporal reuse resources -------------------------------
    if (temporal_descriptor_set_.is_valid())
    {
        device_->destroy_descriptor_set(temporal_descriptor_set_);
        temporal_descriptor_set_ = {};
    }
    if (temporal_pipeline_.is_valid())
    {
        device_->destroy_compute_pipeline(temporal_pipeline_);
        temporal_pipeline_ = {};
    }
    if (temporal_pipeline_layout_.is_valid())
    {
        device_->destroy_pipeline_layout(temporal_pipeline_layout_);
        temporal_pipeline_layout_ = {};
    }
    if (temporal_dsl_.is_valid())
    {
        device_->destroy_descriptor_set_layout(temporal_dsl_);
        temporal_dsl_ = {};
    }
    if (temporal_shader_module_.is_valid())
    {
        device_->destroy_shader_module(temporal_shader_module_);
        temporal_shader_module_ = {};
    }
    if (temporal_reservoir_buffer_.is_valid())
    {
        device_->destroy_buffer(temporal_reservoir_buffer_);
        temporal_reservoir_buffer_ = {};
    }
    if (previous_reservoir_buffer_.is_valid())
    {
        device_->destroy_buffer(previous_reservoir_buffer_);
        previous_reservoir_buffer_ = {};
    }

    // ---- Sprint-1: sample pass resources ----------------------------------
    if (descriptor_set_.is_valid())
    {
        device_->destroy_descriptor_set(descriptor_set_);
        descriptor_set_ = {};
    }
    if (reservoir_buffer_.is_valid())
    {
        device_->destroy_buffer(reservoir_buffer_);
        reservoir_buffer_ = {};
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
