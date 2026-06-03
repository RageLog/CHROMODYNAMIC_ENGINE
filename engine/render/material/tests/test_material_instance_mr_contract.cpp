// =============================================================================
// CHROMODYNAMIC — cd::material MaterialInstance metallic/roughness contract
//
// Phase 639 / T1.9 — surfaces the CPU-side metallic + roughness scalars on
// MaterialInstance so the G-buffer fill pass can write them into the
// dedicated metallic_roughness MRT channel (engine/render/material/README.md
// "G-buffer MRT channel contract") instead of inferring a surface_flag from
// the material kind enum — the bug pattern documented in
// docs/AUDIT/learned-lessons-curtain-reflection-2026-06-03.md.
//
// Three cases:
//   1. Default metallic/roughness matches PbrFactors (0.0F, 0.5F) — dielectric,
//      half-rough — so a freshly-created MaterialInstance is safe to feed into
//      the G-buffer writer without an explicit set call.
//   2. set_* + get_* round-trip preserves the in-range value bit-exactly.
//   3. Out-of-range set is silently clamped into [0,1]; this is the
//      production-safety guard so downstream Schlick F0 lerp and GGX
//      roughness^2 cannot see NaNs / negative weights.
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
// binding so MaterialInstance::create returns a valid instance. The tests
// below need an instance object; the descriptor set itself is unused.
[[nodiscard]] cd::material::MaterialInstance make_test_instance(cd::rhi::NullDevice& dev, cd::material::Material& owner)
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
    md.vertex_spirv = kStubSpirv;
    md.fragment_spirv = kStubSpirv;
    md.descriptor_bindings = bindings;
    md.name = "mr_contract_owner";
    auto m_r = cd::material::Material::create(dev, nullptr, md);
    EXPECT_TRUE(m_r.has_value());
    owner = std::move(*m_r);
    auto inst_r = cd::material::MaterialInstance::create(dev, owner);
    EXPECT_TRUE(inst_r.has_value());
    return std::move(*inst_r);
}

// ---- Case 1 ----------------------------------------------------------------

TEST(MaterialInstanceMrContract, DefaultMetallicRoughnessMatchesPbrFactors)
{
    cd::rhi::NullDevice dev;
    cd::material::Material owner;
    auto inst = make_test_instance(dev, owner);

    // Default = dielectric, half-rough — same defaults as PbrFactors so a
    // freshly-created instance does not need an explicit set call before
    // the G-buffer writer pass samples it.
    EXPECT_FLOAT_EQ(inst.metallic(), 0.0F);
    EXPECT_FLOAT_EQ(inst.roughness(), 0.5F);
}

// ---- Case 2 ----------------------------------------------------------------

TEST(MaterialInstanceMrContract, SetGetRoundTripPreservesInRangeValue)
{
    cd::rhi::NullDevice dev;
    cd::material::Material owner;
    auto inst = make_test_instance(dev, owner);

    inst.set_metallic(0.75F);
    inst.set_roughness(0.20F);
    EXPECT_FLOAT_EQ(inst.metallic(), 0.75F);
    EXPECT_FLOAT_EQ(inst.roughness(), 0.20F);

    // Boundary round-trip: 0 and 1 are inside the clamp range and must
    // pass through bit-exactly.
    inst.set_metallic(0.0F);
    inst.set_roughness(1.0F);
    EXPECT_FLOAT_EQ(inst.metallic(), 0.0F);
    EXPECT_FLOAT_EQ(inst.roughness(), 1.0F);
}

// ---- Case 3 ----------------------------------------------------------------

TEST(MaterialInstanceMrContract, OutOfRangeSetClampsToZeroOne)
{
    cd::rhi::NullDevice dev;
    cd::material::Material owner;
    auto inst = make_test_instance(dev, owner);

    inst.set_metallic(-0.50F);
    EXPECT_FLOAT_EQ(inst.metallic(), 0.0F);  // negative → 0

    inst.set_metallic(2.50F);
    EXPECT_FLOAT_EQ(inst.metallic(), 1.0F);  // > 1 → 1

    inst.set_roughness(-1.0F);
    EXPECT_FLOAT_EQ(inst.roughness(), 0.0F);

    inst.set_roughness(17.0F);
    EXPECT_FLOAT_EQ(inst.roughness(), 1.0F);
}

}  // namespace
