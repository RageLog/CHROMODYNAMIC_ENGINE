// =============================================================================
// CHROMODYNAMIC — engine/render/debug_draw/src/DebugDraw.cpp
// phase1057 — see DebugDraw.hpp banner.
// =============================================================================
#include <cd/debug_draw/DebugDraw.hpp>

#include <algorithm>
#include <array>
#include <cstddef>

namespace cd::debug_draw
{
namespace
{

// Default embedded shaders: world-space pos+color passthrough with a
// single mat4 view-proj push constant, writing ONE colour attachment.
// MRT consumers override via RendererDesc::fragment_glsl.
constexpr const char* kDefaultLineVS = R"glsl(
#version 460
layout(push_constant) uniform PC {
  mat4 view_proj;
} pc;

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec4 in_color;

layout(location = 0) out vec4 v_color;

void main() {
  v_color = in_color;
  gl_Position = pc.view_proj * vec4(in_pos, 1.0);
}
)glsl";

constexpr const char* kDefaultLineFS = R"glsl(
#version 460
layout(location = 0) in vec4 v_color;
layout(location = 0) out vec4 out_color;

void main() {
  out_color = v_color;
}
)glsl";

}  // namespace

namespace
{

[[nodiscard]] cd::core::Result<cd::material::Material>
build_line_material(cd::rhi::IDevice& device,
                    cd::shader::ICompiler* compiler,
                    const RendererDesc& desc)
{
    static_assert(sizeof(cd::debug_line::LineVertex) == 28,
                  "LineVertex layout drifted; update kAttrs/stride");

    static constexpr std::array<cd::rhi::VertexBinding, 1> kBindings {
        cd::rhi::VertexBinding {
            0, sizeof(cd::debug_line::LineVertex), false }
    };
    static constexpr std::array<cd::rhi::VertexAttribute, 2> kAttrs {
        cd::rhi::VertexAttribute { 0, 0, cd::rhi::Format::kRGB32Float,
                                   offsetof(cd::debug_line::LineVertex, position) },
        cd::rhi::VertexAttribute { 1, 0, cd::rhi::Format::kRGBA32Float,
                                   offsetof(cd::debug_line::LineVertex, color) }
    };
    static constexpr std::array<cd::rhi::PushConstantRange, 1> kPush {
        cd::rhi::PushConstantRange { .stages = cd::rhi::ShaderStage::kVertex,
                                     .offset = 0,
                                     .size   = static_cast<std::uint32_t>(sizeof(cd::math::Mat4f)) }
    };

    cd::material::MaterialDesc md {};
    if (!desc.vertex_glsl_path.empty())
        md.vertex_glsl_path = desc.vertex_glsl_path;
    if (!desc.fragment_glsl_path.empty())
        md.fragment_glsl_path = desc.fragment_glsl_path;
    md.vertex_glsl =
        desc.vertex_glsl.empty() ? kDefaultLineVS : desc.vertex_glsl.data();
    md.fragment_glsl =
        desc.fragment_glsl.empty() ? kDefaultLineFS : desc.fragment_glsl.data();
    md.color_attachment_formats = desc.color_attachment_formats;
    md.depth_attachment_format  = desc.depth_attachment_format;
    md.vertex_bindings   = kBindings;
    md.vertex_attributes = kAttrs;
    md.push_constants    = kPush;
    md.topology = cd::rhi::PrimitiveTopology::kLineList;
    md.raster.cull = cd::rhi::CullMode::kNone;
    // Depth-tested, never depth-writing: debug lines occlude
    // correctly behind geometry yet cannot punch holes into the
    // depth buffer that later passes would inherit.
    md.depth_stencil.depth_test    = true;
    md.depth_stencil.depth_write   = false;
    md.depth_stencil.depth_compare = cd::rhi::CompareOp::kLess;
    md.name = desc.name;

    return cd::material::Material::create(device, compiler, md);
}

}  // namespace

cd::core::Result<Renderer>
Renderer::create(cd::rhi::IDevice& device,
                 cd::shader::ICompiler* compiler,
                 const RendererDesc& desc)
{
    auto mat = build_line_material(device, compiler, desc);
    if (!mat.has_value())
    {
        return std::unexpected(mat.error());
    }
    Renderer out {};
    out.material_ = std::move(*mat);
    return out;
}

bool Renderer::recreate_pipeline(cd::rhi::IDevice& device,
                                 cd::shader::ICompiler* compiler,
                                 const RendererDesc& desc)
{
    auto mat = build_line_material(device, compiler, desc);
    if (!mat.has_value())
    {
        return false;  // keep the old pipeline rendering
    }
    material_ = std::move(*mat);
    return true;
}

void Renderer::flush(cd::rhi::IDevice& device,
                     cd::rhi::IDrawRecorder& cmd,
                     const cd::debug_line::LineBatch& batch,
                     const cd::math::Mat4f& view_proj,
                     std::uint32_t frame_idx)
{
    // Reclaim parked buffers whose in-flight frames have fenced —
    // BEFORE any new growth so a grow-every-frame worst case still
    // bounds the parked set.
    while (!parked_.empty() &&
           parked_.front().destroy_at_frame <= frame_idx)
    {
        device.destroy_buffer(parked_.front().h);
        parked_.pop_front();
    }

    if (batch.empty() || !material_.is_valid())
    {
        return;
    }

    const auto verts = batch.vertices();
    const std::uint64_t bytes =
        verts.size() * sizeof(cd::debug_line::LineVertex);
    if (!vb_.is_valid() || vb_capacity_ < bytes)
    {
        // NEVER destroy in place: frame N-1 may still read the old
        // buffer (fif=2 fences only N-2). Park with the engine-wide
        // margin instead (phase 1034 review finding).
        if (vb_.is_valid())
        {
            parked_.push_back({ vb_, frame_idx + kDestroyMargin });
        }
        const std::uint64_t new_cap =
            std::max<std::uint64_t>(bytes * 2U, 16ULL * 1024ULL);
        cd::rhi::BufferDesc bd {};
        bd.size       = new_cap;
        bd.usage      = cd::rhi::BufferUsage::kVertex;
        bd.memory     = cd::rhi::MemoryUsage::kCpuToGpu;
        bd.debug_name = "cd::debug_draw/line_vb";
        if (auto br = device.create_buffer(bd); br.has_value())
        {
            vb_          = *br;
            vb_capacity_ = new_cap;
        }
        else
        {
            vb_          = {};
            vb_capacity_ = 0;
            return;  // allocation failed; skip this frame's lines
        }
    }

    (void)device.upload_buffer(
        vb_, 0,
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(verts.data()), bytes));
    material_.apply(cmd);
    cmd.push_constants(material_.pipeline_layout(),
                       cd::rhi::ShaderStage::kVertex,
                       0, sizeof(cd::math::Mat4f), &view_proj);
    cmd.bind_vertex_buffer(0, vb_, 0);
    cmd.draw(static_cast<std::uint32_t>(verts.size()), 1, 0, 0);
}

void Renderer::destroy(cd::rhi::IDevice& device) noexcept
{
    for (const auto& p : parked_)
    {
        device.destroy_buffer(p.h);
    }
    parked_.clear();
    if (vb_.is_valid())
    {
        device.destroy_buffer(vb_);
        vb_ = {};
    }
    vb_capacity_ = 0;
    material_ = {};  // Material RAII releases its handles
}

}  // namespace cd::debug_draw
