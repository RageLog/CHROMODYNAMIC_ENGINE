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
