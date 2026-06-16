// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_metal_pipeline.cpp
//
// Backend-to-100 Wave 4b / C-METAL-TIER2 (docs/METAL_MAC_TESTING.md §3 Tier-2):
//   cd_test_metal_pipeline — create_graphics_pipeline from a real VertexLayout
//   (vertex bindings + attributes) + a BlendState (M2 PSO builder). The builder
//   must translate the engine descriptor into an MTLRenderPipelineDescriptor +
//   MTLVertexDescriptor and produce a valid MTLRenderPipelineState.
//
// PLATFORM GATE (see test_metal_device.cpp for the full rationale): real
// MTLDevice on Apple; skip-stub everywhere else. The GLSL is cross-compiled to
// MSL in-device via create_shader_module(kGlsl) (cd::rhi_metal_shader toolchain).
//
// Pattern: Arrange / Act / Assert. Deterministic; no sleep_for.
// =============================================================================
#if defined(__APPLE__) && defined(CD_RHI_METAL_ENABLED)

    #include <cd/rhi/Descriptors.hpp>
    #include <cd/rhi/Enums.hpp>
    #include <cd/rhi/Format.hpp>
    #include <cd/rhi/Handles.hpp>
    #include <cd/rhi/IDevice.hpp>
    #include <cd/rhi/Pipeline.hpp>
    #include <cd/rhi/metal/MetalDevice.hpp>

    #include <gtest/gtest.h>

    #include <array>
    #include <cstdint>
    #include <memory>
    #include <span>
    #include <string>

namespace
{

// A vertex shader consuming a position + colour vertex stream (location 0/1),
// so the PSO builder must wire a real MTLVertexDescriptor with two attributes
// and one buffer layout.
constexpr const char* kVS = R"glsl(
#version 450
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec4 in_color;
layout(location = 0) out vec4 v_color;
void main()
{
    v_color = in_color;
    gl_Position = vec4(in_pos, 1.0);
}
)glsl";

constexpr const char* kFS = R"glsl(
#version 450
layout(location = 0) in  vec4 v_color;
layout(location = 0) out vec4 o;
void main() { o = v_color; }
)glsl";

[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> make_metal_device_or_null()
{
    cd::rhi::metal::MetalCreateInfo ci {};
    ci.enable_validation = false;
    auto r = cd::rhi::metal::create_metal_device(ci);
    return r.has_value() ? std::move(*r) : nullptr;
}

[[nodiscard]] cd::rhi::ShaderModuleHandle
make_module(cd::rhi::IDevice& dev, cd::rhi::ShaderStage stage, const char* src)
{
    cd::rhi::ShaderModuleDesc d {};
    d.stage       = stage;
    d.code        = src;
    d.code_size   = std::char_traits<char>::length(src);
    d.entry_point = "main";
    d.language    = cd::rhi::ShaderSourceLanguage::kGlsl;
    auto r = dev.create_shader_module(d);
    return r.has_value() ? *r : cd::rhi::ShaderModuleHandle {};
}

// ---- M2: graphics PSO from a real VertexLayout + BlendState -----------------
TEST(MetalPipeline, CreateGraphicsPipelineFromVertexLayoutAndBlend)
{
    auto dev = make_metal_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no Metal device on this Mac";
    auto& d = *dev;

    const auto vs = make_module(d, cd::rhi::ShaderStage::kVertex, kVS);
    const auto fs = make_module(d, cd::rhi::ShaderStage::kFragment, kFS);
    ASSERT_TRUE(vs.is_valid()) << "GLSL->MSL vertex module must compile";
    ASSERT_TRUE(fs.is_valid()) << "GLSL->MSL fragment module must compile";

    cd::rhi::PipelineLayoutDesc pld {};
    auto layout_r = d.create_pipeline_layout(pld);
    ASSERT_TRUE(layout_r.has_value());
    const auto layout = *layout_r;

    // A real VertexLayout: pos (RGB32F @ 0) + colour (RGBA32F @ 12), stride 28.
    const std::array<cd::rhi::VertexBinding, 1> bindings {
        cd::rhi::VertexBinding { .binding = 0, .stride = 28, .per_instance = false }
    };
    const std::array<cd::rhi::VertexAttribute, 2> attrs {
        cd::rhi::VertexAttribute { .location = 0, .binding = 0,
                                   .format = cd::rhi::Format::kRGB32Float, .offset = 0 },
        cd::rhi::VertexAttribute { .location = 1, .binding = 0,
                                   .format = cd::rhi::Format::kRGBA32Float, .offset = 12 }
    };

    // A real BlendState: standard src-alpha over-blend on attachment 0.
    const std::array<cd::rhi::BlendAttachmentState, 1> blend {
        cd::rhi::BlendAttachmentState {
            .blend_enable = true,
            .src_color = cd::rhi::BlendFactor::kSrcAlpha,
            .dst_color = cd::rhi::BlendFactor::kOneMinusSrcAlpha,
            .color_op  = cd::rhi::BlendOp::kAdd,
            .src_alpha = cd::rhi::BlendFactor::kOne,
            .dst_alpha = cd::rhi::BlendFactor::kZero,
            .alpha_op  = cd::rhi::BlendOp::kAdd,
            .color_write_mask = 0xF }
    };

    const std::array<cd::rhi::Format, 1> color_fmts { cd::rhi::Format::kRGBA8Unorm };

    cd::rhi::GraphicsPipelineDesc gpd {};
    gpd.layout                   = layout;
    gpd.vertex_shader            = vs;
    gpd.fragment_shader          = fs;
    gpd.vertex_bindings          = bindings;
    gpd.vertex_attributes        = attrs;
    gpd.topology                 = cd::rhi::PrimitiveTopology::kTriangleList;
    gpd.raster.cull              = cd::rhi::CullMode::kBack;
    gpd.depth_stencil.depth_test = false;
    gpd.blend_attachments        = blend;
    gpd.color_attachment_formats = color_fmts;

    auto pso_r = d.create_graphics_pipeline(gpd);
    ASSERT_TRUE(pso_r.has_value())
        << "MTLRenderPipelineState build from the VertexLayout+BlendState failed: "
        << (pso_r.has_value() ? std::string {}
                              : std::string(pso_r.error().message.begin(),
                                            pso_r.error().message.end()));
    EXPECT_TRUE(pso_r->is_valid());

    d.destroy_graphics_pipeline(*pso_r);
    d.destroy_pipeline_layout(layout);
    d.destroy_shader_module(vs);
    d.destroy_shader_module(fs);
}

}  // namespace

#else  // not (Apple && CD_RHI_METAL_ENABLED)

    #include <gtest/gtest.h>

TEST(MetalPipeline, SkippedOffApple)
{
    GTEST_SKIP() << "Metal backend disabled on this platform (Apple-only)";
}

#endif  // __APPLE__ && CD_RHI_METAL_ENABLED
