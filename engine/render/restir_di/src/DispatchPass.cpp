// =============================================================================
// CHROMODYNAMIC — engine/render/restir_di/src/DispatchPass.cpp
// Phase 550 / Sprint-1 — ReSTIR DI initial-candidate dispatch (library impl).
//
// See cd/restir_di/DispatchPass.hpp for the design rationale and intended
// scope of the Sprint-1 vs Sprint-2 hand-off. This translation unit owns:
//   * GLSL → SPIR-V compilation of the Sprint-1 sample shader
//   * descriptor set / pipeline layout creation
//   * compute pipeline creation
//   * the per-pixel reservoir SSBO allocation + descriptor binding
//   * the record() path (`bind_compute_pipeline` + `bind_descriptor_set` +
//     push_constants + `dispatch(ceil(w/16), ceil(h/16), 1)`)
//
// The Sprint-1 sample shader is a stripped variant of kRestirDiSampleCS:
//   * `#extension GL_EXT_ray_tracing` removed — Sprint-1 does not yet wire
//     the acceleration structure; keeping the extension forces the
//     consumer's Vulkan device to declare it, which fails on integrated
//     GPUs without RT support. Sprint-2 reinstates the extension when the
//     visibility-ray bind goes in.
//   * G-buffer + light-buffer bindings stripped — Sprint-1 zero-initialises
//     the reservoir SSBO so the dispatch can be validated end-to-end on
//     any Vulkan ICD without scene state. Same DiSample / DiReservoir
//     byte layout as the production shader so Sprint-2 can swap the
//     kernel in-place without re-laying-out the SSBO.
//
// hello_engine is *not* touched in Sprint-1 (per phase550 brief).
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

void DispatchPass::shutdown()
{
    if (device_ == nullptr)
    {
        ready_ = false;
        return;
    }
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
