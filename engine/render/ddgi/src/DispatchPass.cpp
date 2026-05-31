// =============================================================================
// CHROMODYNAMIC — engine/render/ddgi/src/DispatchPass.cpp
// phase549 — implementation of the DDGI trace-pass GPU dispatcher.
// =============================================================================
#include <cd/ddgi/DispatchPass.hpp>

#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/Pipeline.hpp>
#include <cd/shader/Compiler.hpp>

#include <array>
#include <cstring>
#include <string>

namespace cd::ddgi
{

namespace
{

// ---------------------------------------------------------------------------
// Shader source variants:
//   * full     — kDdgiTraceCS (with VK_KHR_ray_query)
//   * no_tlas  — a structurally identical compute shader that skips the ray
//                query and writes the sky-colour miss radiance into every
//                texel. Lets the trace pipeline compile + dispatch on any
//                Vulkan device, including hosts without ray_query support,
//                so the unit test can verify end-to-end GPU dispatch.
// ---------------------------------------------------------------------------

constexpr std::string_view kDdgiTraceSmokeCS = R"glsl(
#version 460

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0, rgba16f) uniform image2D ray_radiance;
layout(set = 0, binding = 1, rg16f)   uniform image2D ray_dir_dist;

layout(push_constant) uniform PC {
    vec3  grid_origin;   float max_distance;
    vec3  grid_spacing;  float hysteresis;
    uvec4 probes_dim;
    uint  probe_face_size;
    uint  frame_index;
    uint  _pad0;
    uint  _pad1;
    vec3  sky_color;     float _pad2;
} pc;

vec3 fibonacci_dir(uint i, uint n, uint seed) {
    float phi   = 2.399963229728653 * float((i + seed) % n);
    float cos_t = 1.0 - 2.0 * float(i) / float(n);
    float sin_t = sqrt(max(0.0, 1.0 - cos_t*cos_t));
    return vec3(cos(phi)*sin_t, sin(phi)*sin_t, cos_t);
}

vec2 oct_encode(vec3 n) {
    float l = abs(n.x) + abs(n.y) + abs(n.z);
    vec2  p = n.xy / l;
    if (n.z < 0.0) p = (1.0 - abs(p.yx)) * sign(p);
    return p * 0.5 + 0.5;
}

void main() {
    uint ray_idx   = gl_GlobalInvocationID.x + gl_GlobalInvocationID.y * 8u;
    uint probe_idx = gl_GlobalInvocationID.z;
    uint total     = pc.probes_dim.x * pc.probes_dim.y * pc.probes_dim.z;
    if (ray_idx   >= pc.probes_dim.w) return;
    if (probe_idx >= total)           return;

    vec3 dir = fibonacci_dir(ray_idx, pc.probes_dim.w, pc.frame_index * 1013u);

    // Miss-only smoke variant: every ray reports the sky colour.
    vec4 radiance = vec4(pc.sky_color, -1.0);
    float dist    = -1.0;

    ivec2 px_out = ivec2(int(ray_idx), int(probe_idx));
    imageStore(ray_radiance, px_out, radiance);
    imageStore(ray_dir_dist, px_out, vec4(oct_encode(dir), dist, 0.0));
}
)glsl";

[[nodiscard]] cd::core::Result<cd::rhi::ShaderModuleHandle>
compile_trace_module(cd::rhi::IDevice& device, bool needs_tlas)
{
    auto compiler = cd::shader::make_glslang_compiler();
    if (!compiler)
    {
        return std::unexpected(cd::shader::shader_errors::make(
            cd::shader::shader_errors::Code::kInitFailed,
            "DispatchPass: glslang backend not enabled"));
    }

    cd::shader::CompileDesc cd_desc {};
    cd_desc.source       = needs_tlas ? kDdgiTraceCS : kDdgiTraceSmokeCS;
    cd_desc.stage        = cd::shader::ShaderStage::kCompute;
    cd_desc.lang         = cd::shader::ShaderLanguage::kGlsl;
    cd_desc.target       = cd::shader::TargetEnv::kVulkan_1_3;
    cd_desc.source_name  = needs_tlas ? "ddgi_trace.comp" : "ddgi_trace_smoke.comp";

    auto compiled = compiler->compile(cd_desc);
    if (!compiled.has_value())
    {
        return std::unexpected(compiled.error());
    }

    cd::rhi::ShaderModuleDesc smd {};
    smd.stage       = cd::rhi::ShaderStage::kCompute;
    smd.code        = compiled->spirv.data();
    smd.code_size   = compiled->spirv.size() * sizeof(std::uint32_t);
    smd.entry_point = "main";
    smd.debug_name  = "ddgi_trace_cs";
    return device.create_shader_module(smd);
}

}  // namespace

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------
cd::core::Result<void>
DispatchPass::init(cd::rhi::IDevice& device, const DispatchPassDesc& desc)
{
    // 1. cache description.
    grid_         = desc.grid;
    settings_     = desc.settings;
    sky_color_[0] = desc.sky_color[0];
    sky_color_[1] = desc.sky_color[1];
    sky_color_[2] = desc.sky_color[2];
    needs_tlas_   = desc.needs_tlas;

    const std::uint32_t probe_count = grid_.probe_count();
    ray_image_width_  = settings_.rays_per_probe == 0U ? 1U : settings_.rays_per_probe;
    ray_image_height_ = probe_count == 0U              ? 1U : probe_count;

    // 2. compile shader -> SPIR-V module.
    auto sm = compile_trace_module(device, needs_tlas_);
    if (!sm.has_value())
    {
        shutdown(device);
        return std::unexpected(sm.error());
    }
    shader_module_ = *sm;

    // 3. descriptor-set layout.
    //    full path:    binding 0 = TLAS, 1 = ray_radiance, 2 = ray_dir_dist.
    //    no-TLAS path: binding 0 = ray_radiance, 1 = ray_dir_dist.
    std::array<cd::rhi::DescriptorSetLayoutBinding, 3> bindings_full {
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 0,
            .type    = cd::rhi::DescriptorType::kAccelerationStructure,
            .count   = 1,
            .stages  = cd::rhi::ShaderStage::kCompute,
        },
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 1,
            .type    = cd::rhi::DescriptorType::kStorageImage,
            .count   = 1,
            .stages  = cd::rhi::ShaderStage::kCompute,
        },
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 2,
            .type    = cd::rhi::DescriptorType::kStorageImage,
            .count   = 1,
            .stages  = cd::rhi::ShaderStage::kCompute,
        },
    };
    std::array<cd::rhi::DescriptorSetLayoutBinding, 2> bindings_no_tlas {
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 0,
            .type    = cd::rhi::DescriptorType::kStorageImage,
            .count   = 1,
            .stages  = cd::rhi::ShaderStage::kCompute,
        },
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 1,
            .type    = cd::rhi::DescriptorType::kStorageImage,
            .count   = 1,
            .stages  = cd::rhi::ShaderStage::kCompute,
        },
    };

    cd::rhi::DescriptorSetLayoutDesc sld {};
    if (needs_tlas_)
        sld.bindings = bindings_full;
    else
        sld.bindings = bindings_no_tlas;

    auto sl = device.create_descriptor_set_layout(sld);
    if (!sl.has_value())
    {
        shutdown(device);
        return std::unexpected(sl.error());
    }
    set_layout_ = *sl;

    // 4. pipeline layout (descriptor set + push constants for kDdgiTraceCS).
    std::array<cd::rhi::DescriptorSetLayoutHandle, 1> set_layouts { set_layout_ };
    std::array<cd::rhi::PushConstantRange, 1> push {
        cd::rhi::PushConstantRange {
            .stages = cd::rhi::ShaderStage::kCompute,
            .offset = 0,
            .size   = sizeof(TracePushConstants),
        },
    };
    cd::rhi::PipelineLayoutDesc pld {};
    pld.set_layouts    = set_layouts;
    pld.push_constants = push;
    auto pl = device.create_pipeline_layout(pld);
    if (!pl.has_value())
    {
        shutdown(device);
        return std::unexpected(pl.error());
    }
    pipeline_layout_ = *pl;

    // 5. compute pipeline.
    cd::rhi::ComputePipelineDesc cpd {};
    cpd.layout = pipeline_layout_;
    cpd.shader = shader_module_;
    auto cp = device.create_compute_pipeline(cpd);
    if (!cp.has_value())
    {
        shutdown(device);
        return std::unexpected(cp.error());
    }
    pipeline_ = *cp;

    // 6. descriptor set + parameter UBO.
    auto ds = device.allocate_descriptor_set(set_layout_);
    if (!ds.has_value())
    {
        shutdown(device);
        return std::unexpected(ds.error());
    }
    descriptor_set_ = *ds;

    {
        cd::rhi::BufferDesc bd {};
        bd.size       = sizeof(TracePushConstants);
        bd.usage      = cd::rhi::BufferUsage::kUniform;
        bd.memory     = cd::rhi::MemoryUsage::kCpuToGpu;
        bd.debug_name = "ddgi_trace_ubo";
        auto buf = device.create_buffer(bd);
        if (!buf.has_value())
        {
            shutdown(device);
            return std::unexpected(buf.error());
        }
        trace_ubo_ = *buf;
    }

    // 7. ray_radiance image (RGBA16F).
    {
        cd::rhi::TextureDesc td {};
        td.type         = cd::rhi::TextureType::k2D;
        td.format       = cd::rhi::Format::kRGBA16Float;
        td.extent       = { ray_image_width_, ray_image_height_, 1 };
        td.mip_levels   = 1;
        td.array_layers = 1;
        td.samples      = cd::rhi::SampleCount::k1;
        td.usage        = cd::rhi::TextureUsage::kStorage |
                          cd::rhi::TextureUsage::kTransferSrc |
                          cd::rhi::TextureUsage::kSampled;
        td.memory       = cd::rhi::MemoryUsage::kGpuOnly;
        td.debug_name   = "ddgi_ray_radiance";
        auto tex = device.create_texture(td);
        if (!tex.has_value())
        {
            shutdown(device);
            return std::unexpected(tex.error());
        }
        ray_radiance_ = *tex;

        cd::rhi::TextureViewDesc vd {};
        vd.texture     = ray_radiance_;
        vd.type        = cd::rhi::TextureType::k2D;
        vd.format      = cd::rhi::Format::kRGBA16Float;
        vd.base_mip    = 0;
        vd.mip_count   = 1;
        vd.base_layer  = 0;
        vd.layer_count = 1;
        auto view = device.create_texture_view(vd);
        if (!view.has_value())
        {
            shutdown(device);
            return std::unexpected(view.error());
        }
        ray_radiance_view_ = *view;
    }

    // 8. ray_dir_dist image (RG16F).
    {
        cd::rhi::TextureDesc td {};
        td.type         = cd::rhi::TextureType::k2D;
        td.format       = cd::rhi::Format::kRG16Float;
        td.extent       = { ray_image_width_, ray_image_height_, 1 };
        td.mip_levels   = 1;
        td.array_layers = 1;
        td.samples      = cd::rhi::SampleCount::k1;
        td.usage        = cd::rhi::TextureUsage::kStorage |
                          cd::rhi::TextureUsage::kTransferSrc |
                          cd::rhi::TextureUsage::kSampled;
        td.memory       = cd::rhi::MemoryUsage::kGpuOnly;
        td.debug_name   = "ddgi_ray_dir_dist";
        auto tex = device.create_texture(td);
        if (!tex.has_value())
        {
            shutdown(device);
            return std::unexpected(tex.error());
        }
        ray_dir_dist_ = *tex;

        cd::rhi::TextureViewDesc vd {};
        vd.texture     = ray_dir_dist_;
        vd.type        = cd::rhi::TextureType::k2D;
        vd.format      = cd::rhi::Format::kRG16Float;
        vd.base_mip    = 0;
        vd.mip_count   = 1;
        vd.base_layer  = 0;
        vd.layer_count = 1;
        auto view = device.create_texture_view(vd);
        if (!view.has_value())
        {
            shutdown(device);
            return std::unexpected(view.error());
        }
        ray_dir_dist_view_ = *view;
    }

    // 9. wire the two image views into the descriptor set. The TLAS is left
    //    unbound until bind_tlas() is called explicitly.
    {
        const std::uint32_t radiance_binding = needs_tlas_ ? 1U : 0U;
        const std::uint32_t dir_dist_binding = needs_tlas_ ? 2U : 1U;
        std::array<cd::rhi::DescriptorWrite, 2> writes {
            cd::rhi::DescriptorWrite {
                .binding       = radiance_binding,
                .array_element = 0,
                .type          = cd::rhi::DescriptorType::kStorageImage,
                .view          = ray_radiance_view_,
            },
            cd::rhi::DescriptorWrite {
                .binding       = dir_dist_binding,
                .array_element = 0,
                .type          = cd::rhi::DescriptorType::kStorageImage,
                .view          = ray_dir_dist_view_,
            },
        };
        auto upd = device.update_descriptor_set(descriptor_set_, writes);
        if (!upd.has_value())
        {
            shutdown(device);
            return std::unexpected(upd.error());
        }
    }

    return {};
}

// ---------------------------------------------------------------------------
// shutdown
// ---------------------------------------------------------------------------
void DispatchPass::shutdown(cd::rhi::IDevice& device) noexcept
{
    if (ray_dir_dist_view_.is_valid())   { device.destroy_texture_view(ray_dir_dist_view_);   ray_dir_dist_view_   = {}; }
    if (ray_radiance_view_.is_valid())   { device.destroy_texture_view(ray_radiance_view_);   ray_radiance_view_   = {}; }
    if (ray_dir_dist_.is_valid())        { device.destroy_texture(ray_dir_dist_);             ray_dir_dist_        = {}; }
    if (ray_radiance_.is_valid())        { device.destroy_texture(ray_radiance_);             ray_radiance_        = {}; }
    if (trace_ubo_.is_valid())           { device.destroy_buffer(trace_ubo_);                 trace_ubo_           = {}; }
    if (descriptor_set_.is_valid())      { device.destroy_descriptor_set(descriptor_set_);    descriptor_set_      = {}; }
    if (pipeline_.is_valid())            { device.destroy_compute_pipeline(pipeline_);        pipeline_            = {}; }
    if (pipeline_layout_.is_valid())     { device.destroy_pipeline_layout(pipeline_layout_);  pipeline_layout_     = {}; }
    if (set_layout_.is_valid())          { device.destroy_descriptor_set_layout(set_layout_); set_layout_          = {}; }
    if (shader_module_.is_valid())       { device.destroy_shader_module(shader_module_);      shader_module_       = {}; }
}

// ---------------------------------------------------------------------------
// bind_tlas
// ---------------------------------------------------------------------------
cd::core::Result<void>
DispatchPass::bind_tlas(cd::rhi::IDevice& device, cd::rhi::AccelStructureHandle tlas)
{
    if (!needs_tlas_)
        return {};                                     // smoke variant — no TLAS binding
    if (!descriptor_set_.is_valid())
    {
        return std::unexpected(cd::rhi::rhi_errors::make(
            cd::rhi::rhi_errors::Code::kInvalidArgument,
            "DispatchPass::bind_tlas called before init()"));
    }

    std::array<cd::rhi::DescriptorWrite, 1> writes {
        cd::rhi::DescriptorWrite {
            .binding       = 0,
            .array_element = 0,
            .type          = cd::rhi::DescriptorType::kAccelerationStructure,
            .accel         = tlas,
        },
    };
    return device.update_descriptor_set(descriptor_set_, writes);
}

// ---------------------------------------------------------------------------
// dispatch
// ---------------------------------------------------------------------------
void DispatchPass::dispatch(cd::rhi::ICommandBuffer& cmd, std::uint32_t frame_index)
{
    if (!pipeline_.is_valid())
        return;

    // Pack the push-constant block.
    TracePushConstants pc {};
    pc.grid_origin[0]    = grid_.origin.x;
    pc.grid_origin[1]    = grid_.origin.y;
    pc.grid_origin[2]    = grid_.origin.z;
    pc.max_distance      = settings_.max_distance;
    pc.grid_spacing[0]   = grid_.spacing.x;
    pc.grid_spacing[1]   = grid_.spacing.y;
    pc.grid_spacing[2]   = grid_.spacing.z;
    pc.hysteresis        = settings_.hysteresis;
    pc.probes_dim[0]     = grid_.probes_x;
    pc.probes_dim[1]     = grid_.probes_y;
    pc.probes_dim[2]     = grid_.probes_z;
    pc.probes_dim[3]     = settings_.rays_per_probe;
    pc.probe_face_size   = 0U;                     // unused by trace pass
    pc.frame_index       = frame_index;
    pc.sky_color[0]      = sky_color_[0];
    pc.sky_color[1]      = sky_color_[1];
    pc.sky_color[2]      = sky_color_[2];

    cmd.bind_compute_pipeline(pipeline_);
    cmd.bind_descriptor_set(0U, descriptor_set_);
    cmd.push_constants(
        pipeline_layout_,
        cd::rhi::ShaderStage::kCompute,
        0U,
        static_cast<std::uint32_t>(sizeof(TracePushConstants)),
        &pc);

    // Workgroup size = 8x8x1 = 64 threads (one ray per thread).
    // Each workgroup covers all rays of a single probe; dispatch one group
    // per probe along Z.
    const std::uint32_t probe_count = grid_.probe_count();
    cmd.dispatch(1U, 1U, probe_count == 0U ? 1U : probe_count);
}

}  // namespace cd::ddgi
