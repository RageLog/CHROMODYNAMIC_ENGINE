// =============================================================================
// CHROMODYNAMIC — cd/material/UiVariant.cpp
// M3 W1B Sprint-1 — Route A foundation.
// See UiVariant.hpp for design + scope.
// =============================================================================
#include <cd/material/UiVariant.hpp>

#include <cd/core/ErrorCode.hpp>
#include <cd/rhi/BlendPresets.hpp>
#include <cd/rhi/DepthStencilPresets.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/shader/Compiler.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace cd::material
{

namespace
{

// Vertex layout for `UiVertexFormat::kUiDefault` — must mirror
// `cd::ui::renderer::Vertex` exactly. Layout is:
//   pos2 (offset 0, 8B), uv2 (offset 8, 8B), RGBA8 (offset 16, 4B),
//   variant+3pad (offset 20, 4B). Stride 24.
constexpr std::uint32_t kUiDefaultVertexStride = 24U;

[[nodiscard]] cd::rhi::BlendAttachmentState resolve_blend(UiBlendMode m) noexcept
{
    switch (m)
    {
        case UiBlendMode::kAlpha:         return cd::rhi::blend_alpha();
        case UiBlendMode::kPremultiplied: return cd::rhi::blend_premultiplied();
        case UiBlendMode::kOpaque:        return cd::rhi::blend_opaque();
    }
    return cd::rhi::blend_alpha();
}

[[nodiscard]] cd::rhi::DepthStencilState resolve_depth(UiDepthMode m) noexcept
{
    switch (m)
    {
        case UiDepthMode::kDisabled: return cd::rhi::depth_disabled();
        case UiDepthMode::kReadOnly: return cd::rhi::depth_readonly();
    }
    return cd::rhi::depth_disabled();
}

/// Compose a stable descriptor-layout key from the spec. Sprint-1
/// only has texture_count + theme_ubo_binding to consider; packing
/// them into a single uint64_t makes the key trivially comparable.
[[nodiscard]] std::uint64_t make_layout_key(const UiVariantSpec& spec) noexcept
{
    const std::uint64_t tex = static_cast<std::uint64_t>(spec.texture_count) & 0xFFFFFFFFULL;
    // Map -1 to 0xFFFFFFFFu so the key is purely numeric.
    const std::uint32_t theme = static_cast<std::uint32_t>(spec.theme_palette_ubo_binding);
    return (tex << 32) | static_cast<std::uint64_t>(theme);
}

}  // namespace

cd::core::Result<UiVariant>
create_ui_variant(cd::rhi::IDevice& device, const UiVariantSpec& spec)
{
    // ---- Validate spec -----------------------------------------------------

    if (spec.vertex_format != UiVertexFormat::kUiDefault)
    {
        return std::unexpected(material_errors::make(
            material_errors::Code::kInvalidArgument,
            "create_ui_variant: only UiVertexFormat::kUiDefault is supported in Sprint-1"));
    }

    if (spec.texture_count > 1U)
    {
        return std::unexpected(material_errors::make(
            material_errors::Code::kInvalidArgument,
            "create_ui_variant: texture_count > 1 not supported in Sprint-1"));
    }

    if (spec.texture_count != 0U)
    {
        return std::unexpected(material_errors::make(
            material_errors::Code::kInvalidArgument,
            "create_ui_variant: texture_count must be 0 in Sprint-1 "
            "(glyph sampler is Sprint-2)"));
    }

    if (spec.theme_palette_ubo_binding >= 0)
    {
        return std::unexpected(material_errors::make(
            material_errors::Code::kInvalidArgument,
            "create_ui_variant: theme_palette_ubo_binding is reserved for Sprint-2"));
    }

    // ---- Acquire compiler --------------------------------------------------

    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr)
    {
        return std::unexpected(material_errors::make(
            material_errors::Code::kCompilerRequired,
            "create_ui_variant: engine built without CD_ENABLE_GLSLANG; "
            "Sprint-2 will ship precompiled SPIR-V for this path"));
    }

    // ---- Resolve pipeline state -------------------------------------------

    // Vertex binding + attributes mirror cd::ui::renderer::Vertex.
    static constexpr std::array<cd::rhi::VertexBinding, 1> kVertexBindings { {
        cd::rhi::VertexBinding { .binding = 0U,
                                 .stride  = kUiDefaultVertexStride,
                                 .per_instance = false },
    } };
    static constexpr std::array<cd::rhi::VertexAttribute, 4> kVertexAttributes { {
        // loc 0: vec2 pos                (offset 0)
        cd::rhi::VertexAttribute { .location = 0U, .binding = 0U,
                                   .format = cd::rhi::Format::kRG32Float,
                                   .offset = 0U },
        // loc 1: vec2 uv                 (offset 8)
        cd::rhi::VertexAttribute { .location = 1U, .binding = 0U,
                                   .format = cd::rhi::Format::kRG32Float,
                                   .offset = 8U },
        // loc 2: vec4 color (RGBA8)      (offset 16)
        cd::rhi::VertexAttribute { .location = 2U, .binding = 0U,
                                   .format = cd::rhi::Format::kRGBA8Unorm,
                                   .offset = 16U },
        // loc 3: uint flags (variant+3pad as a packed uint32)
        cd::rhi::VertexAttribute { .location = 3U, .binding = 0U,
                                   .format = cd::rhi::Format::kR32Uint,
                                   .offset = 20U },
    } };

    const auto blend = resolve_blend(spec.blend_state);
    const auto depth = resolve_depth(spec.depth_state);
    const std::array<cd::rhi::BlendAttachmentState, 1> blend_attachments { blend };

    // UI does not back-face-cull (quads can be wound either way and
    // many widgets are anchored upside-down at the layout root).
    cd::rhi::RasterState raster {};
    raster.cull = cd::rhi::CullMode::kNone;

    // Push-constant: vec2 inv_viewport (vertex stage only).
    static constexpr std::array<cd::rhi::PushConstantRange, 1> kPushRanges { {
        cd::rhi::PushConstantRange { .stages = cd::rhi::ShaderStage::kVertex,
                                     .offset = 0U,
                                     .size   = sizeof(float) * 2U },
    } };

    // Default attachment list (single RGBA8 target) when caller did
    // not supply one. The local array must outlive the span passed
    // into MaterialDesc.
    std::array<cd::rhi::Format, 1> default_colors {
        cd::rhi::Format::kRGBA8Unorm,
    };
    std::span<const cd::rhi::Format> color_formats = spec.color_attachment_formats;
    if (color_formats.empty())
    {
        color_formats = std::span<const cd::rhi::Format>(default_colors.data(),
                                                         default_colors.size());
    }

    // ---- Build MaterialDesc + delegate ------------------------------------

    MaterialDesc md {};
    md.vertex_glsl              = kUiVariantSprint1VS;
    md.fragment_glsl            = kUiVariantSprint1FS;
    md.color_attachment_formats = color_formats;
    md.depth_attachment_format  = spec.depth_attachment_format;
    md.vertex_bindings          = kVertexBindings;
    md.vertex_attributes        = kVertexAttributes;
    md.push_constants           = kPushRanges;
    md.topology                 = cd::rhi::PrimitiveTopology::kTriangleList;
    md.raster                   = raster;
    md.depth_stencil            = depth;
    md.blend_attachments        = blend_attachments;
    md.name                     = spec.name;

    auto mat_r = Material::create(device, compiler.get(), md);
    if (!mat_r.has_value())
    {
        return std::unexpected(mat_r.error());
    }

    UiVariant out;
    out.material_              = std::move(*mat_r);
    out.descriptor_layout_key_ = make_layout_key(spec);
    out.texture_count_         = spec.texture_count;
    return out;
}

}  // namespace cd::material
