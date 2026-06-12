// =============================================================================
// CHROMODYNAMIC — engine/render/cluster_gpu/src/GpuPipeline.cpp
//
// Compiles cluster_assign.comp (GLSL embedded below as a raw string)
// at runtime via cd::shader::make_glslang_compiler, creates the
// descriptor-set + pipeline-layout + compute-pipeline objects, owns
// the four storage buffers, and dispatches the two-pass count/write
// flow followed by a CPU-side readback into ReferenceOutput.
// =============================================================================
#include <cd/cluster/gpu/GpuPipeline.hpp>

#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/shader/Compiler.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>
#include <utility>
#include <vector>

namespace cd::cluster::gpu
{

namespace
{

constexpr std::string_view kClusterAssignGlsl = R"GLSL(
#version 460
#extension GL_EXT_buffer_reference : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 4) in;

struct LightData { vec4 position_radius; };

layout(set = 0, binding = 0, std430) readonly buffer InLights {
    uint  light_count;
    uint  pad_0; uint pad_1; uint pad_2;
    LightData lights[];
} u_lights;

layout(set = 0, binding = 1, std430) buffer ClusterCounts {
    uint counts[];
} u_counts;

layout(set = 0, binding = 2, std430) readonly buffer ClusterOffsets {
    uint offsets[];
} u_offsets;

layout(set = 0, binding = 3, std430) buffer LightIndices {
    uint indices[];
} u_indices;

layout(push_constant) uniform PushConstants {
    uint  cells_x;
    uint  cells_y;
    uint  cells_z;
    float near_plane;
    float far_plane;
    float fov_y_rad;
    float aspect;
    uint  phase;
} u_pc;

uint depth_to_cluster_z(float depth) {
    float clamped = clamp(depth, u_pc.near_plane, u_pc.far_plane);
    float t = log(clamped / u_pc.near_plane) / log(u_pc.far_plane / u_pc.near_plane);
    return min(uint(floor(t * float(u_pc.cells_z))), u_pc.cells_z - 1u);
}
uint angle_to_cluster_x(float angle, float tan_half) {
    float t = clamp(tan(angle) / tan_half, -1.0, 1.0);
    float u = (t + 1.0) * 0.5 * float(u_pc.cells_x);
    return uint(clamp(floor(u), 0.0, float(u_pc.cells_x - 1u)));
}
uint angle_to_cluster_y(float angle, float tan_half) {
    float t = clamp(tan(angle) / tan_half, -1.0, 1.0);
    float u = (t + 1.0) * 0.5 * float(u_pc.cells_y);
    return uint(clamp(floor(u), 0.0, float(u_pc.cells_y - 1u)));
}
bool light_overlaps_cluster(LightData ld, uint cx, uint cy, uint cz) {
    float depth_centre = -ld.position_radius.z;
    float depth_min = max(u_pc.near_plane, depth_centre - ld.position_radius.w);
    float depth_max = min(u_pc.far_plane, depth_centre + ld.position_radius.w);
    if (depth_max < u_pc.near_plane || depth_min > u_pc.far_plane) return false;
    uint cz_min = depth_to_cluster_z(depth_min);
    uint cz_max = depth_to_cluster_z(depth_max);
    if (cz < cz_min || cz > cz_max) return false;
    float depth_for_angle = max(depth_min, 1.0e-3);
    float ang_half  = atan(ld.position_radius.w, depth_for_angle);
    float centre_x  = atan(ld.position_radius.x, depth_for_angle);
    float centre_y  = atan(ld.position_radius.y, depth_for_angle);
    float half_fov_y = u_pc.fov_y_rad * 0.5;
    float tan_half_y = tan(half_fov_y);
    float tan_half_x = u_pc.aspect * tan_half_y;
    uint cx_min = angle_to_cluster_x(centre_x - ang_half, tan_half_x);
    uint cx_max = angle_to_cluster_x(centre_x + ang_half, tan_half_x);
    uint cy_min = angle_to_cluster_y(centre_y - ang_half, tan_half_y);
    uint cy_max = angle_to_cluster_y(centre_y + ang_half, tan_half_y);
    return cx >= cx_min && cx <= cx_max && cy >= cy_min && cy <= cy_max;
}
void main() {
    uint cx = gl_GlobalInvocationID.x;
    uint cy = gl_GlobalInvocationID.y;
    uint cz = gl_GlobalInvocationID.z;
    if (cx >= u_pc.cells_x || cy >= u_pc.cells_y || cz >= u_pc.cells_z) return;
    uint cluster_id = (cz * u_pc.cells_y + cy) * u_pc.cells_x + cx;
    if (u_pc.phase == 0u) {
        uint count = 0u;
        for (uint i = 0u; i < u_lights.light_count; ++i)
            if (light_overlaps_cluster(u_lights.lights[i], cx, cy, cz)) count += 1u;
        u_counts.counts[cluster_id] = count;
    } else {
        uint slot = u_offsets.offsets[cluster_id];
        uint write_cursor = 0u;
        for (uint i = 0u; i < u_lights.light_count; ++i)
            if (light_overlaps_cluster(u_lights.lights[i], cx, cy, cz)) {
                u_indices.indices[slot + write_cursor] = i;
                write_cursor += 1u;
            }
    }
}
)GLSL";

/// Push-constant layout — must match the GLSL above byte-for-byte.
struct PushConstants
{
    std::uint32_t cells_x;
    std::uint32_t cells_y;
    std::uint32_t cells_z;
    float near_plane;
    float far_plane;
    float fov_y_rad;
    float aspect;
    std::uint32_t phase;
};
static_assert(sizeof(PushConstants) == 32, "push-constant block must be 32 bytes");

/// LightData on the wire — vec4 alignment.
struct GpuLight
{
    float x;
    float y;
    float z;
    float radius;
};

/// Header struct preceding the lights[] array in the InLights SSBO.
/// std430 packs uint+uint+uint+uint at 16 bytes, then vec4[] starts.
struct InLightsHeader
{
    std::uint32_t light_count;
    std::uint32_t pad_0;
    std::uint32_t pad_1;
    std::uint32_t pad_2;
};

}  // namespace

struct GpuPipeline::Impl
{
    cd::rhi::IDevice* device { nullptr };
    cd::render::cluster::ClusterConfig cfg {};
    std::uint32_t max_lights { 0 };

    cd::rhi::ShaderModuleHandle shader {};
    cd::rhi::DescriptorSetLayoutHandle set_layout {};
    cd::rhi::PipelineLayoutHandle pipeline_layout {};
    cd::rhi::ComputePipelineHandle pipeline {};
    cd::rhi::DescriptorSetHandle descriptor_set {};

    cd::rhi::BufferHandle in_lights_buf {};
    cd::rhi::BufferHandle counts_buf {};
    cd::rhi::BufferHandle offsets_buf {};
    cd::rhi::BufferHandle indices_buf {};
    std::uint64_t in_lights_buf_size { 0 };
    std::uint64_t counts_buf_size { 0 };
    std::uint64_t offsets_buf_size { 0 };
    std::uint64_t indices_buf_size { 0 };

    /// Per-cluster guaranteed upper bound for the light index buffer.
    /// Worst case = every light in every cluster = cluster_count *
    /// max_lights. Caller can tighten via a future Init overload.
    [[nodiscard]] std::uint32_t max_light_assignments() const noexcept
    {
        return static_cast<std::uint32_t>(cfg.cells_x) * cfg.cells_y * cfg.cells_z * max_lights;
    }

    ~Impl()
    {
        if (device == nullptr)
            return;
        if (indices_buf.value() != 0)  device->destroy_buffer(indices_buf);
        if (offsets_buf.value() != 0)  device->destroy_buffer(offsets_buf);
        if (counts_buf.value() != 0)   device->destroy_buffer(counts_buf);
        if (in_lights_buf.value() != 0) device->destroy_buffer(in_lights_buf);
        if (descriptor_set.value() != 0) device->destroy_descriptor_set(descriptor_set);
        if (pipeline.value() != 0)      device->destroy_compute_pipeline(pipeline);
        if (pipeline_layout.value() != 0) device->destroy_pipeline_layout(pipeline_layout);
        if (set_layout.value() != 0)    device->destroy_descriptor_set_layout(set_layout);
        if (shader.value() != 0)        device->destroy_shader_module(shader);
    }
};

GpuPipeline::GpuPipeline(std::unique_ptr<Impl> impl) noexcept : impl_ { std::move(impl) } {}
GpuPipeline::~GpuPipeline() = default;

const cd::render::cluster::ClusterConfig& GpuPipeline::config() const noexcept { return impl_->cfg; }
std::uint32_t GpuPipeline::max_lights() const noexcept { return impl_->max_lights; }

cd::core::Result<std::unique_ptr<GpuPipeline>>
GpuPipeline::create(cd::rhi::IDevice& device,
                    const cd::render::cluster::ClusterConfig& cfg,
                    std::uint32_t max_lights)
{
    if (max_lights == 0)
        return std::unexpected(cluster_gpu_errors::make(
            cluster_gpu_errors::Code::kInvalidArgument, "max_lights must be > 0"));

    auto impl = std::make_unique<Impl>();
    impl->device = &device;
    impl->cfg = cfg;
    impl->max_lights = max_lights;

    // 1. Compile shader.
    auto compiler = cd::shader::make_glslang_compiler();
    if (!compiler)
        return std::unexpected(cluster_gpu_errors::make(
            cluster_gpu_errors::Code::kShaderCompileFailed,
            "glslang backend not enabled"));
    cd::shader::CompileDesc desc;
    desc.source = kClusterAssignGlsl;
    desc.stage = cd::shader::ShaderStage::kCompute;
    desc.lang = cd::shader::ShaderLanguage::kGlsl;
    desc.target = cd::shader::TargetEnv::kVulkan13;
    desc.source_name = "cluster_assign.comp";
    auto comp = compiler->compile(desc);
    if (!comp.has_value())
        return std::unexpected(cluster_gpu_errors::make(
            cluster_gpu_errors::Code::kShaderCompileFailed));

    cd::rhi::ShaderModuleDesc shader_desc;
    shader_desc.stage = cd::rhi::ShaderStage::kCompute;
    shader_desc.code = comp->spirv.data();
    shader_desc.code_size = comp->spirv.size() * sizeof(std::uint32_t);
    shader_desc.entry_point = "main";
    auto shader = device.create_shader_module(shader_desc);
    if (!shader.has_value())
        return std::unexpected(cluster_gpu_errors::make(
            cluster_gpu_errors::Code::kShaderCompileFailed,
            "create_shader_module failed"));
    impl->shader = *shader;

    // 2. Descriptor-set layout — 4 storage buffer bindings, compute stage.
    std::array<cd::rhi::DescriptorSetLayoutBinding, 4> bindings;
    for (std::uint32_t i = 0; i < 4; ++i)
    {
        bindings[i].binding = i;
        bindings[i].type = cd::rhi::DescriptorType::kStorageBuffer;
        bindings[i].count = 1;
        bindings[i].stages = cd::rhi::ShaderStage::kCompute;
    }
    cd::rhi::DescriptorSetLayoutDesc layout_desc;
    layout_desc.bindings = bindings;
    auto set_layout = device.create_descriptor_set_layout(layout_desc);
    if (!set_layout.has_value())
        return std::unexpected(cluster_gpu_errors::make(
            cluster_gpu_errors::Code::kPipelineCreationFailed,
            "descriptor set layout"));
    impl->set_layout = *set_layout;

    // 3. Pipeline layout — set + push constants.
    std::array<cd::rhi::DescriptorSetLayoutHandle, 1> sl { impl->set_layout };
    cd::rhi::PushConstantRange pc;
    pc.stages = cd::rhi::ShaderStage::kCompute;
    pc.offset = 0;
    pc.size = sizeof(PushConstants);
    std::array<cd::rhi::PushConstantRange, 1> pcs { pc };

    cd::rhi::PipelineLayoutDesc pl_desc;
    pl_desc.set_layouts = sl;
    pl_desc.push_constants = pcs;
    auto pipeline_layout = device.create_pipeline_layout(pl_desc);
    if (!pipeline_layout.has_value())
        return std::unexpected(cluster_gpu_errors::make(
            cluster_gpu_errors::Code::kPipelineCreationFailed,
            "pipeline layout"));
    impl->pipeline_layout = *pipeline_layout;

    // 4. Compute pipeline.
    cd::rhi::ComputePipelineDesc cp_desc;
    cp_desc.layout = impl->pipeline_layout;
    cp_desc.shader = impl->shader;
    auto pipeline = device.create_compute_pipeline(cp_desc);
    if (!pipeline.has_value())
        return std::unexpected(cluster_gpu_errors::make(
            cluster_gpu_errors::Code::kPipelineCreationFailed,
            "compute pipeline"));
    impl->pipeline = *pipeline;

    // 5. Descriptor set.
    auto ds = device.allocate_descriptor_set(impl->set_layout);
    if (!ds.has_value())
        return std::unexpected(cluster_gpu_errors::make(
            cluster_gpu_errors::Code::kPipelineCreationFailed,
            "descriptor set allocation"));
    impl->descriptor_set = *ds;

    // 6. Buffers. All CPU-visible so we can upload + read back via the
    // existing upload_buffer / buffer-copy primitives. Production code
    // would use kGpuOnly + a staging buffer for hot paths.
    const std::uint32_t cluster_count =
        static_cast<std::uint32_t>(cfg.cells_x) * cfg.cells_y * cfg.cells_z;
    const std::uint32_t indices_count = impl->max_light_assignments();

    auto make_storage_buffer = [&](std::uint64_t size,
                                    cd::rhi::BufferHandle& out,
                                    std::uint64_t& size_out,
                                    const char* name) -> cd::core::Result<void> {
        cd::rhi::BufferDesc bd;
        bd.size = size;
        bd.usage = cd::rhi::BufferUsage::kStorage
                 | cd::rhi::BufferUsage::kTransferSrc
                 | cd::rhi::BufferUsage::kTransferDst;
        bd.memory = cd::rhi::MemoryUsage::kCpuRandomAccess;
        bd.debug_name = name;
        auto h = device.create_buffer(bd);
        if (!h.has_value())
            return std::unexpected(cluster_gpu_errors::make(
                cluster_gpu_errors::Code::kBufferCreationFailed, name));
        out = *h;
        size_out = size;
        return {};
    };

    const std::uint64_t in_lights_size =
        sizeof(InLightsHeader) + static_cast<std::uint64_t>(max_lights) * sizeof(GpuLight);
    if (auto r = make_storage_buffer(in_lights_size, impl->in_lights_buf,
                                      impl->in_lights_buf_size, "InLights");
        !r.has_value())
        return std::unexpected(r.error());

    const std::uint64_t counts_size = static_cast<std::uint64_t>(cluster_count) * sizeof(std::uint32_t);
    if (auto r = make_storage_buffer(counts_size, impl->counts_buf,
                                      impl->counts_buf_size, "ClusterCounts");
        !r.has_value())
        return std::unexpected(r.error());

    const std::uint64_t offsets_size =
        static_cast<std::uint64_t>(cluster_count + 1) * sizeof(std::uint32_t);
    if (auto r = make_storage_buffer(offsets_size, impl->offsets_buf,
                                      impl->offsets_buf_size, "ClusterOffsets");
        !r.has_value())
        return std::unexpected(r.error());

    const std::uint64_t indices_size = static_cast<std::uint64_t>(indices_count) * sizeof(std::uint32_t);
    if (auto r = make_storage_buffer(indices_size, impl->indices_buf,
                                      impl->indices_buf_size, "LightIndices");
        !r.has_value())
        return std::unexpected(r.error());

    // 7. Wire descriptor set bindings.
    std::array<cd::rhi::DescriptorWrite, 4> writes;
    for (std::uint32_t i = 0; i < 4; ++i)
    {
        writes[i].binding = i;
        writes[i].type = cd::rhi::DescriptorType::kStorageBuffer;
        writes[i].buffer_offset = 0;
        writes[i].buffer_range = 0;  // whole buffer
    }
    writes[0].buffer = impl->in_lights_buf;
    writes[1].buffer = impl->counts_buf;
    writes[2].buffer = impl->offsets_buf;
    writes[3].buffer = impl->indices_buf;
    auto uds = device.update_descriptor_set(impl->descriptor_set, writes);
    if (!uds.has_value())
        return std::unexpected(cluster_gpu_errors::make(
            cluster_gpu_errors::Code::kPipelineCreationFailed,
            "descriptor set write"));

    return std::unique_ptr<GpuPipeline>(new GpuPipeline(std::move(impl)));
}

cd::core::Result<cd::render::cluster::ReferenceOutput>
GpuPipeline::run(std::span<const cd::render::cluster::LightSphere> lights)
{
    auto& d = *impl_->device;
    if (lights.size() > impl_->max_lights)
        return std::unexpected(cluster_gpu_errors::make(
            cluster_gpu_errors::Code::kInvalidArgument,
            "lights.size() > max_lights"));

    // --- Upload InLights -----------------------------------------------------
    std::vector<std::byte> in_bytes(sizeof(InLightsHeader)
                                    + lights.size() * sizeof(GpuLight));
    {
        InLightsHeader h {};
        h.light_count = static_cast<std::uint32_t>(lights.size());
        std::memcpy(in_bytes.data(), &h, sizeof(h));
        auto* dst = reinterpret_cast<GpuLight*>(in_bytes.data() + sizeof(h));
        for (std::size_t i = 0; i < lights.size(); ++i)
        {
            dst[i].x = lights[i].view_pos.x;
            dst[i].y = lights[i].view_pos.y;
            dst[i].z = lights[i].view_pos.z;
            dst[i].radius = lights[i].radius;
        }
    }
    if (auto r = d.upload_buffer(impl_->in_lights_buf, 0, in_bytes); !r.has_value())
        return std::unexpected(cluster_gpu_errors::make(
            cluster_gpu_errors::Code::kBufferUploadFailed, "lights upload"));

    // Clear cluster_counts to zero before pass 0.
    const std::uint32_t cluster_count =
        static_cast<std::uint32_t>(impl_->cfg.cells_x) * impl_->cfg.cells_y
        * impl_->cfg.cells_z;
    std::vector<std::byte> zero_counts(static_cast<std::size_t>(cluster_count)
                                        * sizeof(std::uint32_t), std::byte { 0 });
    if (auto r = d.upload_buffer(impl_->counts_buf, 0, zero_counts); !r.has_value())
        return std::unexpected(cluster_gpu_errors::make(
            cluster_gpu_errors::Code::kBufferUploadFailed, "counts clear"));

    // Helper: dispatch one pass with phase = N.
    auto dispatch_phase = [&](std::uint32_t phase) -> cd::core::Result<void> {
        auto cb = d.create_command_buffer();
        if (!cb)
            return std::unexpected(cluster_gpu_errors::make(
                cluster_gpu_errors::Code::kPipelineCreationFailed, "cmd buffer"));
        cb->begin();
        cb->bind_compute_pipeline(impl_->pipeline);
        cb->bind_descriptor_set(0, impl_->descriptor_set);

        PushConstants pc;
        pc.cells_x = impl_->cfg.cells_x;
        pc.cells_y = impl_->cfg.cells_y;
        pc.cells_z = impl_->cfg.cells_z;
        pc.near_plane = impl_->cfg.near_plane;
        pc.far_plane = impl_->cfg.far_plane;
        pc.fov_y_rad = impl_->cfg.fov_y_rad;
        pc.aspect = impl_->cfg.aspect;
        pc.phase = phase;
        cb->push_constants(impl_->pipeline_layout, cd::rhi::ShaderStage::kCompute,
                            0, sizeof(pc), &pc);

        const std::uint32_t gx = (impl_->cfg.cells_x + 7U) / 8U;
        const std::uint32_t gy = (impl_->cfg.cells_y + 7U) / 8U;
        const std::uint32_t gz = (impl_->cfg.cells_z + 3U) / 4U;
        cb->dispatch(gx, gy, gz);
        cb->end();
        d.submit(*cb);
        d.wait_idle();
        return {};
    };

    if (auto r = dispatch_phase(0); !r.has_value())
        return std::unexpected(r.error());

    // --- CPU prefix-sum: read counts, build offsets, write back. -------------
    cd::render::cluster::ReferenceOutput out;
    out.cluster_counts.assign(cluster_count, 0U);
    out.cluster_offsets.assign(cluster_count + 1, 0U);

    {
        const std::uint64_t bytes =
            static_cast<std::uint64_t>(cluster_count) * sizeof(std::uint32_t);
        std::vector<std::byte> staging(static_cast<std::size_t>(bytes));
        if (auto r = d.download_buffer(impl_->counts_buf, 0, staging); !r.has_value())
            return std::unexpected(cluster_gpu_errors::make(
                cluster_gpu_errors::Code::kBufferReadbackFailed, "counts readback"));
        std::memcpy(out.cluster_counts.data(), staging.data(), staging.size());
    }
    std::uint32_t running = 0;
    for (std::size_t i = 0; i < cluster_count; ++i)
    {
        out.cluster_offsets[i] = running;
        running += out.cluster_counts[i];
    }
    out.cluster_offsets[cluster_count] = running;

    // Upload offsets back to GPU for PASS 1.
    {
        std::vector<std::byte> offsets_bytes(out.cluster_offsets.size() * sizeof(std::uint32_t));
        std::memcpy(offsets_bytes.data(), out.cluster_offsets.data(), offsets_bytes.size());
        if (auto r = d.upload_buffer(impl_->offsets_buf, 0, offsets_bytes); !r.has_value())
            return std::unexpected(cluster_gpu_errors::make(
                cluster_gpu_errors::Code::kBufferUploadFailed, "offsets upload"));
    }

    if (auto r = dispatch_phase(1); !r.has_value())
        return std::unexpected(r.error());

    // --- Readback light_indices ----------------------------------------------
    out.light_indices.assign(running, 0U);
    if (running > 0)
    {
        const std::uint64_t bytes =
            static_cast<std::uint64_t>(running) * sizeof(std::uint32_t);
        std::vector<std::byte> staging(static_cast<std::size_t>(bytes));
        if (auto r = d.download_buffer(impl_->indices_buf, 0, staging); !r.has_value())
            return std::unexpected(cluster_gpu_errors::make(
                cluster_gpu_errors::Code::kBufferReadbackFailed, "indices readback"));
        std::memcpy(out.light_indices.data(), staging.data(), staging.size());
    }
    return out;
}

}  // namespace cd::cluster::gpu
