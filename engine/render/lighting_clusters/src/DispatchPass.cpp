// =============================================================================
// CHROMODYNAMIC — engine/render/lighting_clusters/src/DispatchPass.cpp
// phase672 — Sprint-2: GPU compute light-culling pass (impl).
//
// See cd/render/lighting_clusters/DispatchPass.hpp for the design rationale.
// This translation unit owns:
//   * GLSL -> SPIR-V compilation of kClusterCullCS via cd::shader.
//   * the descriptor-set layout, pipeline layout, compute pipeline.
//   * the three SSBOs (lights, cluster_table, light_indices) -- all
//     allocated host-visible so a Sprint-2 smoke test can read the
//     populated cluster_table back without staging.
//   * the dispatch path: host-side push-constant pack, lights upload,
//     vkCmdBindPipeline / vkCmdBindDescriptorSets / vkCmdPushConstants /
//     vkCmdDispatch sequence.
//
// CPU parity: kClusterCullCS performs the identical NDC-to-view-space AABB
// reconstruction and AABB-vs-sphere closest-point test as the CPU Clusterer
// (see LightingClusters.cpp). The two paths produce the same cluster ->
// light index assignment when given the same view-proj matrix + light list.
//
// Naming discipline check: distinct from cd::ddgi::DispatchPass and
// cd::restir_di::DispatchPass — those live in their own subsystem namespaces.
// This DispatchPass lives in cd::render::lighting_clusters per the
// library-level namespace declared in LightingClusters.hpp.
// =============================================================================
#include <cd/render/lighting_clusters/DispatchPass.hpp>

#include <cd/rhi/Enums.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/Pipeline.hpp>
#include <cd/shader/Compiler.hpp>

#include <array>
#include <cstddef>
#include <cstring>
#include <string_view>
#include <vector>

namespace cd::render::lighting_clusters
{

namespace
{

// ---------------------------------------------------------------------------
// Push-constant block: grid + view-proj + light count + max-per-cluster.
// std430 / std140 16B-aligned. Total 96 B.
// ---------------------------------------------------------------------------
struct alignas(16) CullPushConstants
{
    float         view_proj[16];            //  0 .. 64 B  -- column-major.
    std::uint32_t x_tiles;                  // 64
    std::uint32_t y_tiles;                  // 68
    std::uint32_t z_slices;                 // 72
    std::uint32_t light_count;              // 76
    float         near_plane;               // 80
    float         far_plane;                // 84
    std::uint32_t max_lights_per_cluster;   // 88
    std::uint32_t _pad0;                    // 92  (round to 96 -- 16-aligned).
};
static_assert(sizeof(CullPushConstants) == 96U,
              "CullPushConstants must match kClusterCullCS PC block (std430)");

// ---------------------------------------------------------------------------
// kClusterCullCS — GPU light-culling compute shader.
//
// One thread per cluster: reconstruct the cluster's view-space AABB from
// (tx, ty, tz) NDC tile boundaries + log-z depth boundaries (identical math
// to LightingClusters.cpp), then walk the light array and AABB-vs-sphere-test
// each light. Writes the (offset, count) pair into cluster_table[idx] and
// fills the per-cluster slab in light_indices starting at
// idx * max_lights_per_cluster.
// ---------------------------------------------------------------------------
constexpr std::string_view kClusterCullCS = R"glsl(
#version 460

layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

struct GpuPointLight {
    vec4 position_radius;   // xyz = world position, w = radius
    vec4 color_intensity;   // xyz = linear RGB,    w = intensity
};

struct ClusterEntry {
    uint offset;
    uint count;
};

layout(set = 0, binding = 0, std430) readonly buffer Lights {
    GpuPointLight lights[];
};
layout(set = 0, binding = 1, std430) buffer ClusterTable {
    ClusterEntry cluster_table[];
};
layout(set = 0, binding = 2, std430) buffer LightIndices {
    uint light_indices[];
};

layout(push_constant) uniform PC {
    mat4  view_proj;          // column-major
    uint  x_tiles;
    uint  y_tiles;
    uint  z_slices;
    uint  light_count;
    float near_plane;
    float far_plane;
    uint  max_lights_per_cluster;
    uint  _pad0;
} pc;

// Log-z partition: view-space depth of the *near* face of slice `s`.
float slice_near_depth(uint s) {
    float t = float(s) / float(pc.z_slices);
    return pc.near_plane * pow(pc.far_plane / pc.near_plane, t);
}

// AABB-vs-sphere closest-point test (identical to LightingClusters.cpp).
// sphere is in view space: (view_x, view_y, depth_positive).
bool cluster_sphere_overlap(uint tx, uint ty, uint tz,
                            vec3 sphere_view, float radius)
{
    // Depth boundaries (positive view-space).
    float depth_near_face = slice_near_depth(tz);
    float depth_far_face  = slice_near_depth(tz + 1u);
    float depth_far       = min(depth_far_face, pc.far_plane);

    // NDC tile boundaries scaled by far-face depth (conservative perspective).
    float ndc_x_min = -1.0 + 2.0 * float(tx)        / float(pc.x_tiles);
    float ndc_x_max = -1.0 + 2.0 * float(tx + 1u)   / float(pc.x_tiles);
    float ndc_y_min = -1.0 + 2.0 * float(ty)        / float(pc.y_tiles);
    float ndc_y_max = -1.0 + 2.0 * float(ty + 1u)   / float(pc.y_tiles);

    vec3 aabb_min = vec3(ndc_x_min * depth_far,
                         ndc_y_min * depth_far,
                         depth_near_face);
    vec3 aabb_max = vec3(ndc_x_max * depth_far,
                         ndc_y_max * depth_far,
                         depth_far);

    vec3 closest = clamp(sphere_view, aabb_min, aabb_max);
    vec3 d       = sphere_view - closest;
    return dot(d, d) <= radius * radius;
}

void main() {
    uint tx = gl_GlobalInvocationID.x;
    uint ty = gl_GlobalInvocationID.y;
    uint tz = gl_GlobalInvocationID.z;
    if (tx >= pc.x_tiles || ty >= pc.y_tiles || tz >= pc.z_slices) return;

    uint idx    = (tz * pc.y_tiles + ty) * pc.x_tiles + tx;
    uint slab   = idx * pc.max_lights_per_cluster;

    // Walk the light array; project + AABB-vs-sphere-test each one.
    uint count = 0u;
    for (uint li = 0u; li < pc.light_count && count < pc.max_lights_per_cluster; ++li) {
        vec4  pr   = lights[li].position_radius;
        float r    = pr.w;

        // Clip-space transform: clip = view_proj * vec4(world, 1).
        vec4 clip = pc.view_proj * vec4(pr.xyz, 1.0);

        // depth_view = clip.w  (positive view-space depth for our convention).
        float depth_view = clip.w;
        if (depth_view + r < pc.near_plane) continue;
        if (depth_view - r > pc.far_plane)  continue;

        // NDC centre; view-space position used for the AABB test.
        float inv_w = (depth_view > 1.0e-5) ? 1.0 / depth_view : 0.0;
        vec2  ndc_c = clip.xy * inv_w;
        vec3  sphere_view = vec3(ndc_c.x * depth_view,
                                 ndc_c.y * depth_view,
                                 depth_view);

        if (cluster_sphere_overlap(tx, ty, tz, sphere_view, r)) {
            light_indices[slab + count] = li;
            ++count;
        }
    }

    cluster_table[idx].offset = slab;
    cluster_table[idx].count  = count;
}
)glsl";

}  // namespace

// ---------------------------------------------------------------------------
// configure / shutdown
// ---------------------------------------------------------------------------
void DispatchPass::configure(const ClusterGrid& grid) noexcept
{
    grid_ = grid;
}

DispatchPass::~DispatchPass()
{
    shutdown();
}

void DispatchPass::shutdown() noexcept
{
    if (device_ == nullptr)
    {
        ready_ = false;
        return;
    }

    if (descriptor_set_.is_valid())  { device_->destroy_descriptor_set(descriptor_set_);  descriptor_set_ = {}; }
    if (light_indices_buffer_.is_valid()) { device_->destroy_buffer(light_indices_buffer_); light_indices_buffer_ = {}; }
    if (cluster_table_buffer_.is_valid()) { device_->destroy_buffer(cluster_table_buffer_); cluster_table_buffer_ = {}; }
    if (lights_buffer_.is_valid())        { device_->destroy_buffer(lights_buffer_);        lights_buffer_ = {}; }
    if (pipeline_.is_valid())        { device_->destroy_compute_pipeline(pipeline_);  pipeline_ = {}; }
    if (pipeline_layout_.is_valid()) { device_->destroy_pipeline_layout(pipeline_layout_); pipeline_layout_ = {}; }
    if (dsl_.is_valid())             { device_->destroy_descriptor_set_layout(dsl_); dsl_ = {}; }
    if (shader_module_.is_valid())   { device_->destroy_shader_module(shader_module_); shader_module_ = {}; }

    ready_     = false;
    device_    = nullptr;
    max_lights_ = 0;
}

// ---------------------------------------------------------------------------
// prepare
// ---------------------------------------------------------------------------
cd::core::Result<void>
DispatchPass::prepare(cd::rhi::IDevice& device, std::uint32_t max_lights)
{
    using cd::rhi::rhi_errors::Code;
    using cd::rhi::rhi_errors::make;

    const std::uint32_t total = total_cluster_count();
    if (total == 0U)
    {
        return std::unexpected(
            make(Code::kInvalidArgument,
                 "lighting_clusters::DispatchPass::prepare: empty cluster grid"));
    }
    if (max_lights == 0U)
    {
        return std::unexpected(
            make(Code::kInvalidArgument,
                 "lighting_clusters::DispatchPass::prepare: max_lights == 0"));
    }

    // Idempotent re-prepare: free prior resources before reallocating.
    shutdown();

    device_     = &device;
    max_lights_ = max_lights;

    // ---- 1. compile GLSL -> SPIR-V ---------------------------------------
    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr)
    {
        shutdown();
        return std::unexpected(
            make(Code::kBackendInitFailed,
                 "lighting_clusters::DispatchPass::prepare: glslang compiler unavailable"));
    }

    cd::shader::CompileDesc cd_desc {};
    cd_desc.source       = kClusterCullCS;
    cd_desc.stage        = cd::shader::ShaderStage::kCompute;
    cd_desc.lang         = cd::shader::ShaderLanguage::kGlsl;
    cd_desc.target       = cd::shader::TargetEnv::kVulkan13;
    cd_desc.source_name  = "cluster_cull.comp";
    auto compiled = compiler->compile(cd_desc);
    if (!compiled.has_value())
    {
        shutdown();
        return std::unexpected(
            make(Code::kBackendInitFailed,
                 "lighting_clusters::DispatchPass::prepare: kClusterCullCS compile failed"));
    }

    // ---- 2. shader module ------------------------------------------------
    cd::rhi::ShaderModuleDesc sm_desc {};
    sm_desc.stage      = cd::rhi::ShaderStage::kCompute;
    sm_desc.code       = compiled->spirv.data();
    sm_desc.code_size  = compiled->spirv.size() * sizeof(std::uint32_t);
    sm_desc.debug_name = "cluster_cull_cs";
    auto sm = device.create_shader_module(sm_desc);
    if (!sm.has_value())
    {
        const auto& err = sm.error();
        shutdown();
        return std::unexpected(err);
    }
    shader_module_ = *sm;

    // ---- 3. descriptor-set layout: 3 SSBOs (lights, table, indices) ------
    const std::array<cd::rhi::DescriptorSetLayoutBinding, 3> bindings { {
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

    // ---- 4. pipeline layout: DSL + push-constants -----------------------
    const std::array<cd::rhi::DescriptorSetLayoutHandle, 1> set_layouts { dsl_ };
    const std::array<cd::rhi::PushConstantRange, 1> push_ranges { {
        { .stages = cd::rhi::ShaderStage::kCompute,
          .offset = 0U,
          .size   = static_cast<std::uint32_t>(sizeof(CullPushConstants)) },
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

    // ---- 5. compute pipeline --------------------------------------------
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

    // ---- 6. SSBOs --------------------------------------------------------
    // All three host-visible so the Sprint-2 smoke test can read the
    // cluster_table populated by the GPU back through download_buffer().
    {
        cd::rhi::BufferDesc d {};
        d.size       = static_cast<std::uint64_t>(max_lights) * sizeof(GpuPointLight);
        d.usage      = cd::rhi::BufferUsage::kStorage
                     | cd::rhi::BufferUsage::kTransferDst;
        d.memory     = cd::rhi::MemoryUsage::kCpuRandomAccess;
        d.debug_name = "lighting_clusters_lights";
        auto r = device.create_buffer(d);
        if (!r.has_value())
        {
            const auto& err = r.error();
            shutdown();
            return std::unexpected(err);
        }
        lights_buffer_ = *r;
    }
    {
        cd::rhi::BufferDesc d {};
        d.size       = cluster_table_size(total);
        d.usage      = cd::rhi::BufferUsage::kStorage
                     | cd::rhi::BufferUsage::kTransferSrc;
        d.memory     = cd::rhi::MemoryUsage::kCpuRandomAccess;
        d.debug_name = "lighting_clusters_cluster_table";
        auto r = device.create_buffer(d);
        if (!r.has_value())
        {
            const auto& err = r.error();
            shutdown();
            return std::unexpected(err);
        }
        cluster_table_buffer_ = *r;
    }
    {
        cd::rhi::BufferDesc d {};
        d.size       = light_indices_size(total);
        d.usage      = cd::rhi::BufferUsage::kStorage
                     | cd::rhi::BufferUsage::kTransferSrc;
        d.memory     = cd::rhi::MemoryUsage::kCpuRandomAccess;
        d.debug_name = "lighting_clusters_light_indices";
        auto r = device.create_buffer(d);
        if (!r.has_value())
        {
            const auto& err = r.error();
            shutdown();
            return std::unexpected(err);
        }
        light_indices_buffer_ = *r;
    }

    // ---- 7. descriptor set + writes -------------------------------------
    auto ds = device.allocate_descriptor_set(dsl_);
    if (!ds.has_value())
    {
        const auto& err = ds.error();
        shutdown();
        return std::unexpected(err);
    }
    descriptor_set_ = *ds;

    const std::array<cd::rhi::DescriptorWrite, 3> writes { {
        { .binding       = 0U,
          .array_element = 0U,
          .type          = cd::rhi::DescriptorType::kStorageBuffer,
          .buffer        = lights_buffer_,
          .buffer_offset = 0U,
          .buffer_range  = 0U },
        { .binding       = 1U,
          .array_element = 0U,
          .type          = cd::rhi::DescriptorType::kStorageBuffer,
          .buffer        = cluster_table_buffer_,
          .buffer_offset = 0U,
          .buffer_range  = 0U },
        { .binding       = 2U,
          .array_element = 0U,
          .type          = cd::rhi::DescriptorType::kStorageBuffer,
          .buffer        = light_indices_buffer_,
          .buffer_offset = 0U,
          .buffer_range  = 0U },
    } };
    auto upd = device.update_descriptor_set(descriptor_set_, writes);
    if (!upd.has_value())
    {
        const auto& err = upd.error();
        shutdown();
        return std::unexpected(err);
    }

    ready_ = true;
    return {};
}

// ---------------------------------------------------------------------------
// execute_culling
// ---------------------------------------------------------------------------
cd::core::Result<void>
DispatchPass::execute_culling(cd::rhi::ICommandBuffer& cmd,
                              std::span<const PointLight> lights)
{
    using cd::rhi::rhi_errors::Code;
    using cd::rhi::rhi_errors::make;

    if (!ready_ || device_ == nullptr)
    {
        return std::unexpected(
            make(Code::kInvalidArgument,
                 "lighting_clusters::DispatchPass::execute_culling: not prepared"));
    }
    if (lights.size() > max_lights_)
    {
        return std::unexpected(
            make(Code::kInvalidArgument,
                 "lighting_clusters::DispatchPass::execute_culling: light_count exceeds max_lights"));
    }

    // Pack PointLight (host layout) into GpuPointLight (std430 layout).
    // The CPU PointLight struct already lays out (vec3 pos, float r, vec3 col,
    // float intensity) -- we copy to be explicit about the GPU contract since
    // the C++ side gives no std430 guarantees on alignment / padding.
    std::vector<GpuPointLight> packed;
    packed.reserve(lights.size());
    for (const auto& l : lights)
    {
        GpuPointLight gp {};
        gp.position[0] = l.position[0];
        gp.position[1] = l.position[1];
        gp.position[2] = l.position[2];
        gp.radius      = l.radius;
        gp.color[0]    = l.color[0];
        gp.color[1]    = l.color[1];
        gp.color[2]    = l.color[2];
        gp.intensity   = l.intensity;
        packed.push_back(gp);
    }

    if (!packed.empty())
    {
        const std::span<const std::byte> bytes {
            reinterpret_cast<const std::byte*>(packed.data()),
            packed.size() * sizeof(GpuPointLight)
        };
        auto up = device_->upload_buffer(lights_buffer_, 0U, bytes);
        if (!up.has_value())
        {
            return std::unexpected(up.error());
        }
    }

    // Pack push-constants. view_proj defaults to identity if grid_ has no
    // associated camera state — the caller is expected to feed a real
    // matrix once the framegraph wires this pass in (Sprint-3+). For the
    // Sprint-2 smoke test the identity matrix lets `w = clip[3]` reflect
    // the source w explicitly.
    CullPushConstants pc {};
    // identity column-major
    pc.view_proj[0]  = 1.0F;
    pc.view_proj[5]  = 1.0F;
    pc.view_proj[10] = 1.0F;
    pc.view_proj[15] = 1.0F;

    pc.x_tiles                = grid_.x_tiles;
    pc.y_tiles                = grid_.y_tiles;
    pc.z_slices               = grid_.z_slices;
    pc.light_count            = static_cast<std::uint32_t>(lights.size());
    pc.near_plane             = grid_.near_plane;
    pc.far_plane              = grid_.far_plane;
    pc.max_lights_per_cluster = kMaxLightsPerCluster;

    cmd.push_debug_group("lighting_clusters::execute_culling");
    cmd.bind_compute_pipeline(pipeline_);
    cmd.bind_descriptor_set(0U, descriptor_set_);
    cmd.push_constants(pipeline_layout_,
                       cd::rhi::ShaderStage::kCompute,
                       0U,
                       static_cast<std::uint32_t>(sizeof(CullPushConstants)),
                       &pc);
    cmd.dispatch(group_count(grid_.x_tiles),
                 group_count(grid_.y_tiles),
                 group_count(grid_.z_slices));
    cmd.pop_debug_group();

    return {};
}

}  // namespace cd::render::lighting_clusters
