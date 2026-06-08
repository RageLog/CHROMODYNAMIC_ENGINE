// =============================================================================
// CHROMODYNAMIC — cd/material/UiVariant.cpp
// M3 W1B — Route A foundation (Sprint-1) + Sprint-2 theme UBO + SDF sampler.
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
#include <string>
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

/// Compose a stable descriptor-layout key from the spec. Sprint-2
/// keys are wider: we fold in texture_count (8 bits), theme_ubo_slot
/// (8 bits), sdf_sampler_slot (8 bits) and the has_theme_ubo flag (1
/// bit). A bumped key tag in the high byte (`0x02`) signals the
/// Sprint-2 layout shape so a downstream cache cannot mix Sprint-1
/// and Sprint-2 sets.
[[nodiscard]] std::uint64_t make_layout_key(const UiVariantSpec& spec,
                                            bool has_theme_ubo) noexcept
{
    constexpr std::uint64_t kSprint2Tag = 0x02ULL << 56;
    const std::uint64_t tex  = static_cast<std::uint64_t>(spec.texture_count & 0xFFU) << 16;
    const std::uint64_t ubo  = static_cast<std::uint64_t>(spec.theme_palette_ubo_slot & 0xFFU) << 8;
    const auto sdf  = static_cast<std::uint64_t>(spec.sdf_font_sampler_slot & 0xFFU);
    const std::uint64_t flag = has_theme_ubo ? (1ULL << 32) : 0ULL;
    return kSprint2Tag | flag | tex | ubo | sdf;
}

/// Emit the Sprint-2 fragment shader source with the caller-supplied
/// descriptor binding numbers substituted in. The reference shader
/// constant in the header uses binding=0 for the theme UBO and
/// binding=1 for the SDF sampler; this generator re-emits the same
/// source with the actual slots so the SPIR-V matches the descriptor
/// set layout we hand to Material::create().
[[nodiscard]] std::string make_sprint2_fs(std::uint32_t theme_slot,
                                          std::uint32_t sdf_slot,
                                          bool include_sampler)
{
    std::string s;
    s.reserve(1024U);
    s += "#version 450\n";
    s += "layout(set = 0, binding = ";
    s += std::to_string(theme_slot);
    s += ") uniform ThemePalette {\n"
         "    vec4 primary;\n"
         "    vec4 secondary;\n"
         "    vec4 surface;\n"
         "    vec4 on_surface;\n"
         "} u_theme;\n";
    if (include_sampler)
    {
        s += "layout(set = 0, binding = ";
        s += std::to_string(sdf_slot);
        s += ") uniform sampler2D u_sdf_atlas;\n";
    }
    s += "layout(location = 0) in vec2 v_uv;\n"
         "layout(location = 1) in vec4 v_color;\n"
         "layout(location = 2) flat in uint v_variant;\n"
         "layout(location = 0) out vec4 out_color;\n"
         "void main() {\n"
         "    vec4 base = v_color;\n";
    if (include_sampler)
    {
        s += "    const float kSdfZero = 128.0 / 255.0;\n"
             "    if (v_uv.x >= 0.0 && v_uv.x <= 1.0 &&\n"
             "        v_uv.y >= 0.0 && v_uv.y <= 1.0) {\n"
             "        float d = texture(u_sdf_atlas, v_uv).r;\n"
             "        float a = smoothstep(kSdfZero - 0.0625, kSdfZero + 0.0625, d);\n"
             "        base.a *= a;\n"
             "    }\n";
    }
    s += "    vec4 tint = (v_variant == 1u) ? u_theme.surface : u_theme.primary;\n"
         "    out_color = base * tint;\n"
         "}\n";
    return s;
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
            "create_ui_variant: only UiVertexFormat::kUiDefault is supported"));
    }

    if (spec.texture_count > 1U)
    {
        return std::unexpected(material_errors::make(
            material_errors::Code::kInvalidArgument,
            "create_ui_variant: texture_count > 1 not supported "
            "(Sprint-2 caps at 1 glyph SDF atlas)"));
    }

    // The legacy `theme_palette_ubo_binding` field is reserved as an ABI
    // marker. Sprint-2 callers must use `theme_palette_ubo_slot` instead;
    // any non-negative legacy value remains a hard error.
    if (spec.theme_palette_ubo_binding >= 0)
    {
        return std::unexpected(material_errors::make(
            material_errors::Code::kInvalidArgument,
            "create_ui_variant: theme_palette_ubo_binding is the legacy "
            "Sprint-1 ABI marker — set theme_palette_ubo_slot instead and "
            "leave theme_palette_ubo_binding at -1"));
    }

    // Sprint-2: decide whether to take the Sprint-2 fragment shader path.
    // Two opt-in signals trigger Sprint-2: (1) `use_theme_palette_ubo`
    // is true, or (2) `texture_count == 1` (the SDF sampler shader
    // requires the theme tint multiplier so we always pair the two).
    // A default-constructed spec triggers neither -> Sprint-1 path,
    // matching the Sprint-1 test suite byte-for-byte.
    const bool wants_sdf_sampler =
        spec.texture_count == 1U;
    const bool wants_theme_ubo =
        wants_sdf_sampler || spec.use_theme_palette_ubo;

    if (wants_sdf_sampler &&
        spec.sdf_font_sampler_slot == spec.theme_palette_ubo_slot)
    {
        return std::unexpected(material_errors::make(
            material_errors::Code::kInvalidArgument,
            "create_ui_variant: sdf_font_sampler_slot must differ from "
            "theme_palette_ubo_slot when texture_count == 1"));
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

    // Sprint-2: when the spec asks for a theme UBO or an SDF sampler we
    // emit the Sprint-2 fragment shader with the actual descriptor
    // binding numbers substituted in; otherwise we keep the Sprint-1
    // vertex-color-only path so default-constructed specs still produce
    // the variant the Sprint-1 tests exercise.
    std::string fs_storage;  // must outlive the MaterialDesc
    std::string_view vs_source = kUiVariantSprint1VS;
    std::string_view fs_source = kUiVariantSprint1FS;
    if (wants_theme_ubo)
    {
        fs_storage = make_sprint2_fs(spec.theme_palette_ubo_slot,
                                     spec.sdf_font_sampler_slot,
                                     wants_sdf_sampler);
        vs_source = kUiVariantSprint2VS;
        fs_source = fs_storage;
    }

    // Sprint-2 descriptor bindings — UBO (binding = theme_palette_ubo_slot)
    // and optional combined image+sampler (binding = sdf_font_sampler_slot).
    // The storage must outlive MaterialDesc.
    std::array<cd::rhi::DescriptorSetLayoutBinding, 2> ds_bindings { {} };
    std::size_t ds_count = 0U;
    if (wants_theme_ubo)
    {
        ds_bindings[ds_count++] = cd::rhi::DescriptorSetLayoutBinding {
            .binding = spec.theme_palette_ubo_slot,
            .type    = cd::rhi::DescriptorType::kUniformBuffer,
            .count   = 1U,
            .stages  = cd::rhi::ShaderStage::kFragment,
        };
    }
    if (wants_sdf_sampler)
    {
        ds_bindings[ds_count++] = cd::rhi::DescriptorSetLayoutBinding {
            .binding = spec.sdf_font_sampler_slot,
            .type    = cd::rhi::DescriptorType::kCombinedImageSampler,
            .count   = 1U,
            .stages  = cd::rhi::ShaderStage::kFragment,
        };
    }
    const std::span<const cd::rhi::DescriptorSetLayoutBinding> ds_span {
        ds_bindings.data(), ds_count
    };

    MaterialDesc md {};
    md.vertex_glsl              = vs_source;
    md.fragment_glsl            = fs_source;
    md.color_attachment_formats = color_formats;
    md.depth_attachment_format  = spec.depth_attachment_format;
    md.vertex_bindings          = kVertexBindings;
    md.vertex_attributes        = kVertexAttributes;
    md.descriptor_bindings      = ds_span;
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
    out.descriptor_layout_key_ = make_layout_key(spec, wants_theme_ubo);
    out.texture_count_         = spec.texture_count;
    out.has_theme_ubo_         = wants_theme_ubo;
    return out;
}

}  // namespace cd::material
