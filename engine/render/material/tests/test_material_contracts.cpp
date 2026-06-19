// =============================================================================
// CHROMODYNAMIC — cd::material header-contract + multi-set edge tests
//
// ≥80→100 marathon depth pass for cd::material. ADD-ONLY. Covers the
// dependency-free header predicates and the phase864 multi-set assembly
// path that had no dedicated coverage. No BRDF math, descriptor layout, or
// rendered-output path is touched.
//
// Cases:
//   A. AlphaMode predicates — needs_alpha_test / needs_blend constexpr
//      truth table (used by the pipeline-selection code).
//   B. PbrParams::is_opaque boundary at exactly 0.999 (the documented
//      cutoff threshold) + is_emissive per-channel sensitivity.
//   C. RtMaterialRecord / RtSunBlock default values match the std430 / std140
//      contract the closest-hit GLSL reads (defaults = dielectric white,
//      half-rough; sun straight up at unit intensity).
//   D. extra_set_layouts (phase864) — a caller-owned extra descriptor set
//      layout appended after the material's own set produces a valid
//      multi-set Material on the headless backend.
//   E. Material::create rejects a missing fragment stage with
//      kInvalidArgument (the symmetric counterpart of the existing
//      missing-vertex-stage test).
// =============================================================================
#include <cd/material/AlphaMode.hpp>
#include <cd/material/Material.hpp>
#include <cd/material/PbrParams.hpp>
#include <cd/material/RtClosestHit.hpp>
#include <cd/rhi/NullDevice.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace
{

constexpr std::array<std::uint32_t, 5> kStubSpirv { 0x07230203U, 0x00010000U, 0x00000000U, 1U, 0U };

// ---- Case A: AlphaMode predicate truth table -------------------------------

TEST(MaterialContracts, AlphaModePredicateTruthTable)
{
    using cd::material::AlphaMode;
    using cd::material::needs_alpha_test;
    using cd::material::needs_blend;

    // needs_alpha_test is true ONLY for kMask.
    static_assert(!needs_alpha_test(AlphaMode::kOpaque));
    static_assert(needs_alpha_test(AlphaMode::kMask));
    static_assert(!needs_alpha_test(AlphaMode::kBlend));

    // needs_blend is true ONLY for kBlend.
    static_assert(!needs_blend(AlphaMode::kOpaque));
    static_assert(!needs_blend(AlphaMode::kMask));
    static_assert(needs_blend(AlphaMode::kBlend));

    // Runtime mirror so the case shows in the ctest report.
    EXPECT_TRUE(needs_alpha_test(AlphaMode::kMask));
    EXPECT_FALSE(needs_alpha_test(AlphaMode::kBlend));
    EXPECT_TRUE(needs_blend(AlphaMode::kBlend));
    EXPECT_FALSE(needs_blend(AlphaMode::kMask));

    // AlphaParams default = opaque, glTF spec cutoff 0.5.
    constexpr cd::material::AlphaParams kDefault {};
    static_assert(kDefault.mode == AlphaMode::kOpaque);
    EXPECT_FLOAT_EQ(kDefault.cutoff, 0.5F);
}

// ---- Case B: PbrParams predicate boundaries --------------------------------

TEST(MaterialContracts, PbrFactorsIsOpaqueBoundaryAtPoint999)
{
    cd::material::PbrFactors f {};  // default alpha = 1 -> opaque.
    EXPECT_TRUE(cd::material::is_opaque(f));

    // Exactly on the documented 0.999 threshold counts as opaque.
    f.albedo[3] = 0.999F;
    EXPECT_TRUE(cd::material::is_opaque(f));

    // Just below the threshold is translucent.
    f.albedo[3] = 0.998F;
    EXPECT_FALSE(cd::material::is_opaque(f));
}

TEST(MaterialContracts, PbrFactorsIsEmissivePerChannel)
{
    cd::material::PbrFactors f {};
    EXPECT_FALSE(cd::material::is_emissive(f));

    // Any single channel > 0 flips the predicate.
    f.emissive[0] = 0.01F;
    EXPECT_TRUE(cd::material::is_emissive(f));
    f.emissive[0] = 0.0F;
    f.emissive[2] = 0.01F;
    EXPECT_TRUE(cd::material::is_emissive(f));
}

// ---- Case C: RT record / sun-block default contract ------------------------

TEST(MaterialContracts, RtMaterialRecordDefaultsMatchStd430Contract)
{
    // Default = white diffuse, dielectric (metallic packed in albedo.a),
    // half-rough (roughness packed in emissive.a), no emissive — mirrors
    // PbrFactors so an un-set record reads as a neutral surface.
    cd::material::RtMaterialRecord rec {};
    EXPECT_FLOAT_EQ(rec.albedo[0], 1.0F);
    EXPECT_FLOAT_EQ(rec.albedo[1], 1.0F);
    EXPECT_FLOAT_EQ(rec.albedo[2], 1.0F);
    EXPECT_FLOAT_EQ(rec.albedo[3], 0.0F);   // metallic
    EXPECT_FLOAT_EQ(rec.emissive[0], 0.0F);
    EXPECT_FLOAT_EQ(rec.emissive[1], 0.0F);
    EXPECT_FLOAT_EQ(rec.emissive[2], 0.0F);
    EXPECT_FLOAT_EQ(rec.emissive[3], 0.5F); // roughness
    static_assert(sizeof(cd::material::RtMaterialRecord) == 32U);
}

TEST(MaterialContracts, RtSunBlockDefaultsToUnitUpSun)
{
    cd::material::RtSunBlock sun {};
    // Default direction points straight up (toward sun) at unit intensity.
    EXPECT_FLOAT_EQ(sun.dir_intensity[0], 0.0F);
    EXPECT_FLOAT_EQ(sun.dir_intensity[1], 1.0F);
    EXPECT_FLOAT_EQ(sun.dir_intensity[2], 0.0F);
    EXPECT_FLOAT_EQ(sun.dir_intensity[3], 1.0F);  // intensity
    EXPECT_FLOAT_EQ(sun.color[0], 1.0F);
    EXPECT_FLOAT_EQ(sun.color[1], 1.0F);
    EXPECT_FLOAT_EQ(sun.color[2], 1.0F);
    static_assert(sizeof(cd::material::RtSunBlock) == 32U);
}

TEST(MaterialContracts, PackEmptySpanYieldsEmptyRecordVector)
{
    // Defensive: a zero-prim scene packs to an empty (but valid) record
    // list — the caller uploads a zero-byte SSBO and the GLSL record_count
    // guard handles every hit via the neutral-grey fallback.
    const auto records = cd::material::pack_rt_material_records(
        std::span<const cd::material::MaterialInstance*> {});
    EXPECT_TRUE(records.empty());
}

// ---- Case D: phase864 multi-set assembly -----------------------------------

TEST(MaterialContracts, ExtraSetLayoutsProduceValidMultiSetMaterial)
{
    cd::rhi::NullDevice dev;

    // A caller-owned extra descriptor set layout (e.g. the W8-BE bindless
    // dedicated set). Material does NOT take ownership; the caller's handle
    // outlives the create() call here.
    cd::rhi::DescriptorSetLayoutDesc extra_dsld {};
    auto extra_r = dev.create_descriptor_set_layout(extra_dsld);
    ASSERT_TRUE(extra_r.has_value());
    const std::array<cd::rhi::DescriptorSetLayoutHandle, 1> extra_layouts { *extra_r };

    // The material also has its own per-instance descriptor set at set 0.
    std::array<cd::rhi::DescriptorSetLayoutBinding, 1> bindings {
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 0,
            .type    = cd::rhi::DescriptorType::kUniformBuffer,
            .count   = 1,
            .stages  = cd::rhi::ShaderStage::kFragment,
        }
    };

    cd::material::MaterialDesc md {};
    md.vertex_spirv        = kStubSpirv;
    md.fragment_spirv      = kStubSpirv;
    md.descriptor_bindings = bindings;
    md.extra_set_layouts   = extra_layouts;
    md.name                = "multi_set_owner";

    auto m = cd::material::Material::create(dev, nullptr, md);
    ASSERT_TRUE(m.has_value()) << m.error().message;
    EXPECT_TRUE(m->is_valid());
    EXPECT_TRUE(m->has_descriptors());                  // own set 0 present
    EXPECT_TRUE(m->descriptor_set_layout().is_valid());
    EXPECT_TRUE(m->pipeline_layout().is_valid());       // built over set 0 + extra

    // The material does not own the extra layout — destroying it after
    // create() must be the caller's job, and the Material destructor must
    // NOT touch it. (Tracked: NullDevice destroy is a no-op, so this is a
    // contract assertion, not a leak check.)
    dev.destroy_descriptor_set_layout(*extra_r);
}

// ---- Case D2: extra set layouts WITHOUT a material-owned set ----------------

TEST(MaterialContracts, ExtraSetLayoutsWithoutOwnSetStillValid)
{
    cd::rhi::NullDevice dev;

    cd::rhi::DescriptorSetLayoutDesc extra_dsld {};
    auto extra_r = dev.create_descriptor_set_layout(extra_dsld);
    ASSERT_TRUE(extra_r.has_value());
    const std::array<cd::rhi::DescriptorSetLayoutHandle, 1> extra_layouts { *extra_r };

    // No descriptor_bindings -> the material owns no set 0; the extra layout
    // becomes the only set in the pipeline layout. This exercises the
    // has_descriptors_ == false branch of the multi-set assembly.
    cd::material::MaterialDesc md {};
    md.vertex_spirv      = kStubSpirv;
    md.fragment_spirv    = kStubSpirv;
    md.extra_set_layouts = extra_layouts;
    md.name              = "multi_set_no_own";

    auto m = cd::material::Material::create(dev, nullptr, md);
    ASSERT_TRUE(m.has_value()) << m.error().message;
    EXPECT_TRUE(m->is_valid());
    EXPECT_FALSE(m->has_descriptors());                 // no own set 0
    EXPECT_TRUE(m->pipeline_layout().is_valid());

    dev.destroy_descriptor_set_layout(*extra_r);
}

// ---- Case E: missing fragment stage rejected -------------------------------

TEST(MaterialContracts, MissingFragmentStageRejected)
{
    cd::rhi::NullDevice dev;
    cd::material::MaterialDesc md {};
    md.vertex_spirv = kStubSpirv;  // vertex present, fragment absent.
    md.name         = "no_fs";

    auto m = cd::material::Material::create(dev, nullptr, md);
    ASSERT_FALSE(m.has_value());
    EXPECT_EQ(m.error().code,
              static_cast<std::uint32_t>(cd::material::material_errors::Code::kInvalidArgument));
}

}  // namespace
