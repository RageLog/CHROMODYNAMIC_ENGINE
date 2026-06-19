// =============================================================================
// CHROMODYNAMIC — cd::material MaterialInstance CPU-state contract (edge cases)
//
// ≥80→100 marathon depth pass for cd::material. ADD-ONLY: locks in the
// CPU-side MaterialInstance contracts that were implemented but had no
// dedicated coverage. Nothing here touches BRDF math, descriptor layout,
// or any rendered-output path — every assertion is against pure CPU state.
//
// Cases:
//   1. set_alpha_cutoff clamps into [0,1] (mirrors the metallic/roughness
//      clamp contract; glTF 2.0 §3.9.3 cutoff domain).
//   2. set_alpha_params writes mode + cutoff atomically (glTF loader path).
//   3. set_alpha_mode collapses an out-of-range cast to kOpaque (defensive).
//   4. set_albedo / set_emissive clamp into [0, kRtMaxRadiance=1024] with a
//      hard zero floor (Schlick F0 lerp / reflection multiply safety).
//   5. release() restores the documented inert defaults so a released
//      instance round-trips through ray_hit_sample as the neutral-grey
//      fallback (NOT stale set values).
//   6. Move construction + move assignment preserve the CPU shading state
//      and reset the moved-from source to defaults.
//   7. update() on an inert (default-constructed) instance is rejected with
//      kInvalidArgument before any device call.
// =============================================================================
#include <cd/material/Material.hpp>
#include <cd/rhi/NullDevice.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <utility>

namespace
{

constexpr float kEps = 1e-5F;
constexpr std::array<std::uint32_t, 5> kStubSpirv { 0x07230203U, 0x00010000U, 0x00000000U, 1U, 0U };

// Build a valid (non-inert) MaterialInstance over the NullDevice — same
// one-binding pattern the sibling material tests use.
[[nodiscard]] cd::material::MaterialInstance
make_test_instance(cd::rhi::NullDevice& dev, cd::material::Material& owner)
{
    std::array<cd::rhi::DescriptorSetLayoutBinding, 1> bindings {
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 0,
            .type    = cd::rhi::DescriptorType::kUniformBuffer,
            .count   = 1,
            .stages  = cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
        }
    };
    cd::material::MaterialDesc md {};
    md.vertex_spirv        = kStubSpirv;
    md.fragment_spirv      = kStubSpirv;
    md.descriptor_bindings = bindings;
    md.name                = "cpu_state_owner";
    auto m_r = cd::material::Material::create(dev, nullptr, md);
    EXPECT_TRUE(m_r.has_value());
    owner = std::move(*m_r);
    auto inst_r = cd::material::MaterialInstance::create(dev, owner);
    EXPECT_TRUE(inst_r.has_value());
    return std::move(*inst_r);
}

// ---- Case 1: alpha cutoff clamp --------------------------------------------

TEST(MaterialInstanceCpuState, AlphaCutoffClampsIntoUnitRange)
{
    cd::rhi::NullDevice dev;
    cd::material::Material owner;
    auto inst = make_test_instance(dev, owner);

    // Default matches the glTF 2.0 spec default of 0.5.
    EXPECT_FLOAT_EQ(inst.alpha_cutoff(), 0.5F);

    // In-range value round-trips bit-exactly.
    inst.set_alpha_cutoff(0.25F);
    EXPECT_FLOAT_EQ(inst.alpha_cutoff(), 0.25F);

    // Below 0 saturates to 0; above 1 saturates to 1.
    inst.set_alpha_cutoff(-3.0F);
    EXPECT_FLOAT_EQ(inst.alpha_cutoff(), 0.0F);
    inst.set_alpha_cutoff(9.0F);
    EXPECT_FLOAT_EQ(inst.alpha_cutoff(), 1.0F);
}

// ---- Case 2: set_alpha_params atomic write ---------------------------------

TEST(MaterialInstanceCpuState, SetAlphaParamsWritesModeAndCutoffTogether)
{
    cd::rhi::NullDevice dev;
    cd::material::Material owner;
    auto inst = make_test_instance(dev, owner);

    cd::material::AlphaParams p {};
    p.mode   = cd::material::AlphaMode::kMask;
    p.cutoff = 0.3F;
    inst.set_alpha_params(p);

    EXPECT_TRUE(inst.is_mask());
    EXPECT_FLOAT_EQ(inst.alpha_cutoff(), 0.3F);

    // The cutoff inside AlphaParams is clamped on the way through too.
    cd::material::AlphaParams over {};
    over.mode   = cd::material::AlphaMode::kBlend;
    over.cutoff = 2.0F;
    inst.set_alpha_params(over);
    EXPECT_TRUE(inst.is_blend());
    EXPECT_FLOAT_EQ(inst.alpha_cutoff(), 1.0F);
}

// ---- Case 3: out-of-range alpha-mode cast defends to kOpaque ----------------

TEST(MaterialInstanceCpuState, SetAlphaModeOutOfRangeCastFallsBackToOpaque)
{
    cd::rhi::NullDevice dev;
    cd::material::Material owner;
    auto inst = make_test_instance(dev, owner);

    // Push it off the default first so the defensive reset is observable.
    inst.set_alpha_mode(cd::material::AlphaMode::kBlend);
    ASSERT_TRUE(inst.is_blend());

    // A caller casting an out-of-domain integer into the enum must collapse
    // to kOpaque rather than leaving the shader to branch on garbage.
    inst.set_alpha_mode(static_cast<cd::material::AlphaMode>(99));
    EXPECT_TRUE(inst.is_opaque());
    EXPECT_EQ(inst.alpha_mode(), cd::material::AlphaMode::kOpaque);
}

// ---- Case 4: albedo / emissive clamp ---------------------------------------

TEST(MaterialInstanceCpuState, AlbedoAndEmissiveClampToHdrRange)
{
    cd::rhi::NullDevice dev;
    cd::material::Material owner;
    auto inst = make_test_instance(dev, owner);

    // Negative components saturate to 0 (keeps F0 lerp / reflect multiply
    // well-defined); the generous HDR cap is 1024 so a runaway glTF
    // baseColor / emissive_strength cannot push downstream sums to inf.
    inst.set_albedo(-1.0F, 2000.0F, 0.5F);
    EXPECT_FLOAT_EQ(inst.albedo_r(), 0.0F);
    EXPECT_FLOAT_EQ(inst.albedo_g(), 1024.0F);
    EXPECT_FLOAT_EQ(inst.albedo_b(), 0.5F);

    inst.set_emissive(5000.0F, -2.0F, 16.0F);
    EXPECT_FLOAT_EQ(inst.emissive_r(), 1024.0F);
    EXPECT_FLOAT_EQ(inst.emissive_g(), 0.0F);
    EXPECT_FLOAT_EQ(inst.emissive_b(), 16.0F);
}

// ---- Case 5: release() restores inert defaults -----------------------------

TEST(MaterialInstanceCpuState, ReleasedInstanceSamplesNeutralGreyFallback)
{
    cd::rhi::NullDevice dev;
    cd::material::Material owner;
    auto inst = make_test_instance(dev, owner);

    // Paint non-default Sponza-curtain state, then release.
    inst.set_albedo(0.1F, 0.65F, 0.2F);
    inst.set_emissive(0.0F, 0.05F, 0.0F);
    inst.set_metallic(0.9F);
    inst.set_roughness(0.1F);
    ASSERT_TRUE(inst.is_valid());

    // Move-assigning an empty instance over `inst` drives release() on the
    // old state. The released instance must NOT leak its stale set values:
    // ray_hit_sample has to return the documented neutral-grey fallback.
    inst = cd::material::MaterialInstance {};
    ASSERT_FALSE(inst.is_valid());

    const auto sample = cd::material::ray_hit_sample(inst);
    EXPECT_FALSE(sample.valid);
    EXPECT_NEAR(sample.albedo[0], cd::material::kRayHitFallbackGrey, kEps);
    EXPECT_NEAR(sample.albedo[1], cd::material::kRayHitFallbackGrey, kEps);
    EXPECT_NEAR(sample.albedo[2], cd::material::kRayHitFallbackGrey, kEps);
    EXPECT_NEAR(sample.emissive[0], 0.0F, kEps);
    EXPECT_NEAR(sample.emissive[1], 0.0F, kEps);
    EXPECT_NEAR(sample.emissive[2], 0.0F, kEps);
    EXPECT_NEAR(sample.metallic,  0.0F, kEps);
    EXPECT_NEAR(sample.roughness, 0.5F, kEps);
}

// ---- Case 6: move preserves CPU shading state ------------------------------

TEST(MaterialInstanceCpuState, MovePreservesShadingStateAndResetsSource)
{
    cd::rhi::NullDevice dev;
    cd::material::Material owner;
    auto src = make_test_instance(dev, owner);

    src.set_metallic(0.7F);
    src.set_roughness(0.2F);
    src.set_albedo(0.3F, 0.4F, 0.5F);
    src.set_emissive(0.6F, 0.7F, 0.8F);
    src.set_alpha_mode(cd::material::AlphaMode::kMask);
    src.set_alpha_cutoff(0.4F);

    // Move construction transfers the descriptor handle AND the CPU state.
    cd::material::MaterialInstance moved = std::move(src);
    EXPECT_TRUE(moved.is_valid());

    EXPECT_FLOAT_EQ(moved.metallic(), 0.7F);
    EXPECT_FLOAT_EQ(moved.roughness(), 0.2F);
    EXPECT_FLOAT_EQ(moved.albedo_r(), 0.3F);
    EXPECT_FLOAT_EQ(moved.albedo_b(), 0.5F);
    EXPECT_FLOAT_EQ(moved.emissive_g(), 0.7F);
    EXPECT_TRUE(moved.is_mask());
    EXPECT_FLOAT_EQ(moved.alpha_cutoff(), 0.4F);

    // The moved-from source contract IS the subject under test: it must be
    // back at the documented inert defaults. Accessing a moved-from object
    // is exactly what bugprone-use-after-move / hicpp-invalid-access-moved
    // flag, so the intentional verification block is fenced off.
    // NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved)
    EXPECT_FALSE(src.is_valid());
    const auto src_sample = cd::material::ray_hit_sample(src);
    EXPECT_FALSE(src_sample.valid);
    EXPECT_NEAR(src_sample.albedo[0], cd::material::kRayHitFallbackGrey, kEps);
    // NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)
}

// ---- Case 7: update() on inert instance is rejected ------------------------

TEST(MaterialInstanceCpuState, UpdateOnInertInstanceRejected)
{
    cd::material::MaterialInstance inert;  // default-constructed, no device.
    ASSERT_FALSE(inert.is_valid());

    std::array<cd::rhi::DescriptorWrite, 1> writes { {} };
    writes[0].binding = 0U;
    writes[0].type    = cd::rhi::DescriptorType::kUniformBuffer;

    auto r = inert.update(writes);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::material::material_errors::Code::kInvalidArgument));
}

}  // namespace
