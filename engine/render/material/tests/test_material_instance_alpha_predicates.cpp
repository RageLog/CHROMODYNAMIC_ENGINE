// =============================================================================
// CHROMODYNAMIC — cd::material MaterialInstance alpha-mode predicate accessors
//
// Phase 646 / T1.16 — is_blend / is_mask / is_opaque convenience predicates
// on MaterialInstance.  Prerequisite for T1.15 two-pass alpha render order
// (next marathon), which routes translucent geometry via:
//
//   if (mat.is_blend()) route_to_blend_bucket();
//
// instead of comparing the enum directly at every call site.
//
// Three cases:
//   1. Fresh instance: is_opaque() == true, is_mask() == false,
//      is_blend() == false — default kOpaque wins.
//   2. set_alpha_mode(kMask): is_mask() == true, others false.
//   3. set_alpha_mode(kBlend): is_blend() == true, others false.
// =============================================================================
#include <cd/material/Material.hpp>
#include <cd/rhi/Format.hpp>
#include <cd/rhi/NullDevice.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace
{

constexpr std::array<std::uint32_t, 5> kStubSpirv { 0x07230203U, 0x00010000U, 0x00000000U, 1U, 0U };

// Helper — build a MaterialInstance over the NullDevice with one descriptor
// binding so MaterialInstance::create returns a valid instance.
[[nodiscard]] cd::material::MaterialInstance make_test_instance(cd::rhi::NullDevice& dev,
                                                                 cd::material::Material& owner)
{
    std::array<cd::rhi::DescriptorSetLayoutBinding, 1> bindings {
        cd::rhi::DescriptorSetLayoutBinding {
                                             .binding = 0,
                                             .type = cd::rhi::DescriptorType::kUniformBuffer,
                                             .count = 1,
                                             .stages = cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                                             }
    };
    cd::material::MaterialDesc md {};
    md.vertex_spirv   = kStubSpirv;
    md.fragment_spirv = kStubSpirv;
    md.descriptor_bindings = bindings;
    md.name = "alpha_predicates_owner";
    auto m_r = cd::material::Material::create(dev, nullptr, md);
    EXPECT_TRUE(m_r.has_value());
    owner = std::move(*m_r);
    auto inst_r = cd::material::MaterialInstance::create(dev, owner);
    EXPECT_TRUE(inst_r.has_value());
    return std::move(*inst_r);
}

// ---- Case 1: default is_opaque ---------------------------------------------

TEST(MaterialInstanceAlphaPredicates, DefaultIsOpaque)
{
    cd::rhi::NullDevice dev;
    cd::material::Material owner;
    auto inst = make_test_instance(dev, owner);

    // A freshly-created MaterialInstance must be opaque — standard depth-tested
    // + depth-written path, no alpha test, no blend bucket.
    EXPECT_TRUE(inst.is_opaque());
    EXPECT_FALSE(inst.is_mask());
    EXPECT_FALSE(inst.is_blend());
}

// ---- Case 2: set_alpha_mode(kMask) -----------------------------------------

TEST(MaterialInstanceAlphaPredicates, SetMaskPredicatesCorrect)
{
    cd::rhi::NullDevice dev;
    cd::material::Material owner;
    auto inst = make_test_instance(dev, owner);

    inst.set_alpha_mode(cd::material::AlphaMode::kMask);

    EXPECT_FALSE(inst.is_opaque());
    EXPECT_TRUE(inst.is_mask());
    EXPECT_FALSE(inst.is_blend());
}

// ---- Case 3: set_alpha_mode(kBlend) ----------------------------------------

TEST(MaterialInstanceAlphaPredicates, SetBlendPredicatesCorrect)
{
    cd::rhi::NullDevice dev;
    cd::material::Material owner;
    auto inst = make_test_instance(dev, owner);

    inst.set_alpha_mode(cd::material::AlphaMode::kBlend);

    EXPECT_FALSE(inst.is_opaque());
    EXPECT_FALSE(inst.is_mask());
    EXPECT_TRUE(inst.is_blend());
}

}  // namespace
