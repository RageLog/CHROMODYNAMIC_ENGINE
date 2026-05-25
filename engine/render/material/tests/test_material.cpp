// =============================================================================
// CHROMODYNAMIC — cd::material tests (NullDevice + Vulkan when present)
// =============================================================================
#include <cd/material/Material.hpp>
#include <cd/rhi/Format.hpp>
#include <cd/rhi/NullDevice.hpp>
#include <cd/rhi_vulkan/VulkanDevice.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace
{

// Minimal valid SPIR-V header — the NullDevice accepts any non-empty
// 32-bit-aligned blob; the Vulkan path needs a real entry point so the
// SPIR-V-from-source tests are gated on the Vulkan suite.
constexpr std::array<std::uint32_t, 5> kStubSpirv { 0x07230203U, 0x00010000U, 0x00000000U, 1U, 0U };

// ---- NullDevice tests (no GPU required) -----------------------------------

TEST(Material, CreateFromPreCompiledSpirvOnNullDevice)
{
    cd::rhi::NullDevice dev;
    // span<> view; backing array must outlive the desc → keep it as a local.
    constexpr std::array<cd::rhi::Format, 1> kColorFormats { cd::rhi::Format::kRGBA8Unorm };
    cd::material::MaterialDesc desc {};
    desc.vertex_spirv = kStubSpirv;
    desc.fragment_spirv = kStubSpirv;
    desc.color_attachment_formats = kColorFormats;
    desc.name = "stub";

    auto m = cd::material::Material::create(dev, nullptr, desc);
    ASSERT_TRUE(m.has_value()) << m.error().message;
    EXPECT_TRUE(m->is_valid());
    EXPECT_FALSE(m->has_descriptors());
    EXPECT_TRUE(m->pipeline().is_valid());
    EXPECT_TRUE(m->pipeline_layout().is_valid());
}

TEST(Material, MissingVertexStageRejected)
{
    cd::rhi::NullDevice dev;
    cd::material::MaterialDesc desc {};
    desc.fragment_spirv = kStubSpirv;
    desc.name = "no_vs";

    auto m = cd::material::Material::create(dev, nullptr, desc);
    ASSERT_FALSE(m.has_value());
    EXPECT_EQ(m.error().code, static_cast<std::uint32_t>(cd::material::material_errors::Code::kInvalidArgument));
}

TEST(Material, GlslWithoutCompilerRejected)
{
    cd::rhi::NullDevice dev;
    cd::material::MaterialDesc desc {};
    desc.vertex_glsl = "void main(){}";
    desc.fragment_glsl = "void main(){}";

    auto m = cd::material::Material::create(dev, /*compiler=*/nullptr, desc);
    ASSERT_FALSE(m.has_value());
    EXPECT_EQ(m.error().code, static_cast<std::uint32_t>(cd::material::material_errors::Code::kCompilerRequired));
}

TEST(Material, DescriptorBindingsCreateLayout)
{
    cd::rhi::NullDevice dev;
    std::array<cd::rhi::DescriptorSetLayoutBinding, 1> bindings {
        cd::rhi::DescriptorSetLayoutBinding {
                                             .binding = 0,
                                             .type = cd::rhi::DescriptorType::kUniformBuffer,
                                             .count = 1,
                                             .stages = cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                                             }
    };
    cd::material::MaterialDesc desc {};
    desc.vertex_spirv = kStubSpirv;
    desc.fragment_spirv = kStubSpirv;
    desc.descriptor_bindings = bindings;
    desc.name = "ubo_mat";

    auto m = cd::material::Material::create(dev, nullptr, desc);
    ASSERT_TRUE(m.has_value());
    EXPECT_TRUE(m->has_descriptors());
    EXPECT_TRUE(m->descriptor_set_layout().is_valid());
}

TEST(Material, MoveTransfersOwnership)
{
    cd::rhi::NullDevice dev;
    cd::material::MaterialDesc desc {};
    desc.vertex_spirv = kStubSpirv;
    desc.fragment_spirv = kStubSpirv;
    auto m_r = cd::material::Material::create(dev, nullptr, desc);
    ASSERT_TRUE(m_r.has_value());

    cd::material::Material moved = std::move(*m_r);
    EXPECT_TRUE(moved.is_valid());
    EXPECT_FALSE(m_r->is_valid());  // moved-from is reset to inert state
}

TEST(MaterialInstance, NoDescriptorsRejected)
{
    cd::rhi::NullDevice dev;
    cd::material::MaterialDesc desc {};
    desc.vertex_spirv = kStubSpirv;
    desc.fragment_spirv = kStubSpirv;
    auto m = cd::material::Material::create(dev, nullptr, desc);
    ASSERT_TRUE(m.has_value());

    auto inst = cd::material::MaterialInstance::create(dev, *m);
    ASSERT_FALSE(inst.has_value());
    EXPECT_EQ(inst.error().code, static_cast<std::uint32_t>(cd::material::material_errors::Code::kInvalidArgument));
}

TEST(MaterialInstance, CreateFromMaterialWithDescriptors)
{
    cd::rhi::NullDevice dev;
    std::array<cd::rhi::DescriptorSetLayoutBinding, 1> bindings {
        cd::rhi::DescriptorSetLayoutBinding {
                                             .binding = 0,
                                             .type = cd::rhi::DescriptorType::kUniformBuffer,
                                             .count = 1,
                                             .stages = cd::rhi::ShaderStage::kVertex,
                                             }
    };
    cd::material::MaterialDesc desc {};
    desc.vertex_spirv = kStubSpirv;
    desc.fragment_spirv = kStubSpirv;
    desc.descriptor_bindings = bindings;
    auto m = cd::material::Material::create(dev, nullptr, desc);
    ASSERT_TRUE(m.has_value());

    auto inst = cd::material::MaterialInstance::create(dev, *m);
    ASSERT_TRUE(inst.has_value()) << inst.error().message;
    EXPECT_TRUE(inst->is_valid());
    EXPECT_TRUE(inst->descriptor_set().is_valid());
}

// ---- Vulkan-backed end-to-end test (skipped if no ICD) --------------------

TEST(Material, GlslEndToEndOnRealDevice)
{
    auto vk_r = cd::rhi_vulkan::create_vulkan_device({});
    if (!vk_r.has_value())
        GTEST_SKIP() << "no Vulkan ICD";
    auto& dev = **vk_r;

    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    constexpr const char* kVS = R"glsl(
#version 450
const vec2 P[3] = vec2[](vec2(0.0,-0.5),vec2(0.5,0.5),vec2(-0.5,0.5));
void main() { gl_Position = vec4(P[gl_VertexIndex], 0.0, 1.0); }
)glsl";
    constexpr const char* kFS = R"glsl(
#version 450
layout(location = 0) out vec4 c;
void main() { c = vec4(1.0, 0.5, 0.2, 1.0); }
)glsl";

    constexpr std::array<cd::rhi::Format, 1> kFormats { cd::rhi::Format::kRGBA8Unorm };
    cd::material::MaterialDesc desc {};
    desc.vertex_glsl = kVS;
    desc.fragment_glsl = kFS;
    desc.color_attachment_formats = kFormats;
    desc.name = "triangle";

    auto m = cd::material::Material::create(dev, compiler.get(), desc);
    ASSERT_TRUE(m.has_value()) << m.error().message;
    EXPECT_TRUE(m->is_valid());
    EXPECT_TRUE(m->pipeline().is_valid());
}

}  // namespace

#include <cd/material/PbrParams.hpp>

TEST(PbrParams, DefaultIsWhiteHalfRoughDielectric)
{
    cd::material::PbrParams p;
    EXPECT_FLOAT_EQ(p.factors.albedo[0], 1.0F);
    EXPECT_FLOAT_EQ(p.factors.metallic, 0.0F);
    EXPECT_FLOAT_EQ(p.factors.roughness, 0.5F);
    EXPECT_FLOAT_EQ(p.factors.occlusion, 1.0F);
}

TEST(PbrParams, IsOpaqueAtAlpha1)
{
    cd::material::PbrFactors f;
    EXPECT_TRUE(cd::material::is_opaque(f));
    f.albedo[3] = 0.5F;
    EXPECT_FALSE(cd::material::is_opaque(f));
}

TEST(PbrParams, IsEmissiveOnlyWhenAnyChannelNonZero)
{
    cd::material::PbrFactors f;
    EXPECT_FALSE(cd::material::is_emissive(f));
    f.emissive[1] = 0.5F;
    EXPECT_TRUE(cd::material::is_emissive(f));
}

TEST(PbrParams, FactorsSizeIsStd140Compatible)
{
    EXPECT_EQ(sizeof(cd::material::PbrFactors), 48u);
}

// Phase 155 — pre-integrated split-sum BRDF LUT.
#include <cd/material/BrdfLut.hpp>

TEST(BrdfLut, HammersleyFirstSamplesAreLowDiscrepancy)
{
    using namespace cd::material;
    const auto s0 = hammersley(0, 16);
    const auto s1 = hammersley(1, 16);
    const auto s8 = hammersley(8, 16);
    EXPECT_FLOAT_EQ(s0[0], 0.0F);
    EXPECT_FLOAT_EQ(s1[0], 1.0F / 16.0F);
    EXPECT_FLOAT_EQ(s8[0], 0.5F);
    EXPECT_NEAR(s1[1], 0.5F,    1e-5F);
    EXPECT_NEAR(s8[1], 0.0625F, 1e-5F);
}

TEST(BrdfLut, IntegratedTexelInBounds)
{
    using namespace cd::material;
    auto t0 = integrate_brdf(0.05F, 0.05F, 256);
    EXPECT_GE(t0.scale, 0.0F); EXPECT_LE(t0.scale, 1.0F);
    EXPECT_GE(t0.bias,  0.0F); EXPECT_LE(t0.bias,  1.0F);
    auto t1 = integrate_brdf(0.95F, 0.95F, 256);
    EXPECT_GE(t1.scale, 0.0F); EXPECT_LE(t1.scale, 1.0F);
}

TEST(BrdfLut, BakedLutHasExpectedShape)
{
    using namespace cd::material;
    auto lut = bake_brdf_lut(16, 16, 64);
    ASSERT_EQ(lut.size(), 256u);
    for (const auto& t : lut)
    {
        EXPECT_GE(t.scale, 0.0F); EXPECT_LE(t.scale, 1.0F);
        EXPECT_GE(t.bias,  0.0F); EXPECT_LE(t.bias,  1.0F);
    }
}

TEST(BrdfLut, SmoothLowAngleHasLowScaleHighBias)
{
    using namespace cd::material;
    auto t = integrate_brdf(0.03F, 0.05F, 512);
    EXPECT_LT(t.scale, t.bias);
}

// Phase 171 — LitPbrMaterial std140 packer
#include <cd/material/LitPbrMaterial.hpp>

TEST(LitPbr, StdLayoutSizes)
{
    EXPECT_EQ(sizeof(cd::material::LitPbrPush), 128U);
    EXPECT_EQ(sizeof(cd::material::LitPbrLightStd140), 128U);
}

TEST(LitPbr, PackLightStd140PreservesFields)
{
    auto src = cd::light::point({ 1.0F, 2.0F, 3.0F }, { 0.9F, 0.8F, 0.7F }, 1200.0F, 8.0F);
    auto packed = cd::material::pack_light_std140(src);
    EXPECT_FLOAT_EQ(packed.position_range[0], 1.0F);
    EXPECT_FLOAT_EQ(packed.position_range[3], 8.0F);  // range
    EXPECT_FLOAT_EQ(packed.color_kelvin[0], 0.9F);
    EXPECT_FLOAT_EQ(packed.direction_intensity[3], 1200.0F);
    EXPECT_EQ(packed.slots[2], static_cast<std::uint32_t>(cd::light::LightType::kPoint));
}

TEST(LitPbr, PackSpotIncludesConePreCompute)
{
    auto src = cd::light::spot({ 0,0,0 }, { 0,-1,0 }, { 1,1,1 }, 800.0F,
                               5.0F, 0.4F, 0.7F);
    auto packed = cd::material::pack_light_std140(src);
    EXPECT_GT(packed.cone_params[0], packed.cone_params[1]);  // cos(inner) > cos(outer)
    EXPECT_GT(packed.cone_params[2], 0.0F);                    // inv_cone_range positive
}

TEST(LitPbr, ShaderSourcesAreCompilableShaped)
{
    const std::string vs { cd::material::kLitPbrVS };
    const std::string fs { cd::material::kLitPbrFS };
    EXPECT_NE(vs.find("gl_Position"), std::string::npos);
    EXPECT_NE(fs.find("u_lights.lights"), std::string::npos);
    EXPECT_NE(fs.find("distance_atten"), std::string::npos);
    EXPECT_NE(fs.find("cone_atten"), std::string::npos);
}

// Phase 172 — IBL cubemap descriptor bindings + split-sum reconstruction.
TEST(LitPbr, ShaderDeclaresIblBindings)
{
    const std::string fs { cd::material::kLitPbrFS };
    EXPECT_NE(fs.find("samplerCube u_irradiance"), std::string::npos);
    EXPECT_NE(fs.find("samplerCube u_prefiltered"), std::string::npos);
    EXPECT_NE(fs.find("sampler2D   u_brdf_lut"), std::string::npos);
    // Split-sum reconstruction: F = F0 * scale + bias.
    EXPECT_NE(fs.find("F0 * brdf.x + vec3(brdf.y)"), std::string::npos);
    // Roughness-driven mip lookup on the prefiltered cubemap.
    EXPECT_NE(fs.find("textureLod(u_prefiltered"), std::string::npos);
}
