// =============================================================================
// CHROMODYNAMIC — cd::material RT closest-hit general-geometry contract
//
// Phase 657 / T1.12 — chrome PBR sphere reflection bug (Sponza scene): a
// chrome-mirror RT trace that hits a non-sphere prim (wall, curtain,
// vegetation) needs the prim's material albedo + emissive so the reflection
// shows ACTUAL surrounding geometry instead of black / IBL fallback.
//
// The full closest-hit GLSL branch lives behind an inline shader-string
// boundary (samples/rhi/hello_rt + the future engine-owned RT pipeline) and
// is deferred per the step-5 scope-down. This commit ships the **stable
// CPU-side contract** the future shader-record builder will read:
//
//   cd::material::ray_hit_sample(MaterialInstance&) -> RayHitSample
//
// Two cases mirroring the T1.12 brief:
//   1. Synthetic glTF prim — a valid MaterialInstance with the painted
//      Sponza curtain green sampled by `ray_hit_sample` returns a
//      non-zero RGB color (the curtain albedo). This is the case the
//      chrome-mirror reflection needs to render walls + curtains.
//   2. Inert MaterialInstance — `ray_hit_sample` returns the neutral-grey
//      fallback so a future closest-hit shader that blindly multiplies
//      `payload.color *= sample.albedo` still shows SOMETHING in the
//      reflection (preserving the M0 chrome reflection baseline: not
//      black). This is the production safety net for prims whose
//      material binding is not (yet) wired through.
//
// Source:
//   - User screenshot 2026-06-03 (Sponza chrome PBR sphere reflection bug)
//   - docs/AUDIT/learned-lessons-pbr-rt-and-curtain-alpha-2026-06-03.md
//     (Bug A, T1.12)
//   - Phase 641 audit (queued lesson)
// =============================================================================
#include <cd/material/Material.hpp>
#include <cd/rhi/NullDevice.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace
{

constexpr float kEps = 1e-5F;
constexpr std::array<std::uint32_t, 5> kStubSpirv { 0x07230203U, 0x00010000U, 0x00000000U, 1U, 0U };

// Build a valid (non-inert) MaterialInstance — same NullDevice pattern as
// test_material_instance_mr_contract. Mirrors a real glTF prim that the
// Sponza loader would emit at scene-ingest time.
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
    md.vertex_spirv = kStubSpirv;
    md.fragment_spirv = kStubSpirv;
    md.descriptor_bindings = bindings;
    md.name = "rt_hit_general_geometry_owner";
    auto m_r = cd::material::Material::create(dev, nullptr, md);
    EXPECT_TRUE(m_r.has_value());
    owner = std::move(*m_r);
    auto inst_r = cd::material::MaterialInstance::create(dev, owner);
    EXPECT_TRUE(inst_r.has_value());
    return std::move(*inst_r);
}

// ---- Case 1 — synthetic glTF prim ------------------------------------------
//
// A non-sphere general-geometry hit. The chrome sphere's closest-hit ray
// lands on a Sponza curtain prim; `ray_hit_sample` returns the curtain's
// painted RGB color so the reflection shows the curtain instead of black.

TEST(RtHitGeneralGeometry, ValidGltfPrimReturnsMaterialAlbedo)
{
    cd::rhi::NullDevice dev;
    cd::material::Material owner;
    auto inst = make_test_instance(dev, owner);

    // Sponza-style curtain green + a kiss of emissive (LED trim).
    inst.set_albedo(0.10F, 0.65F, 0.20F);
    inst.set_emissive(0.0F, 0.05F, 0.0F);
    inst.set_metallic(0.0F);
    inst.set_roughness(0.85F);

    const auto sample = cd::material::ray_hit_sample(inst);

    EXPECT_TRUE(sample.valid);
    EXPECT_NEAR(sample.albedo[0],   0.10F, kEps);
    EXPECT_NEAR(sample.albedo[1],   0.65F, kEps);
    EXPECT_NEAR(sample.albedo[2],   0.20F, kEps);
    EXPECT_NEAR(sample.emissive[0], 0.0F,  kEps);
    EXPECT_NEAR(sample.emissive[1], 0.05F, kEps);
    EXPECT_NEAR(sample.emissive[2], 0.0F,  kEps);
    EXPECT_NEAR(sample.metallic,    0.0F,  kEps);
    EXPECT_NEAR(sample.roughness,   0.85F, kEps);

    // The crux of T1.12: the sample colour is **non-zero** so a closest-hit
    // shader that does `payload.color = sample.albedo + ...` lights the
    // chrome reflection with the curtain colour instead of black.
    const float luminance = 0.2126F * sample.albedo[0]
                          + 0.7152F * sample.albedo[1]
                          + 0.0722F * sample.albedo[2];
    EXPECT_GT(luminance, 0.0F);
}

// ---- Case 2 — inert MaterialInstance, fallback path ------------------------
//
// The chrome sphere's RT trace hits a prim whose MaterialInstance has not
// yet been wired (or has been released). The fallback **must** kick in so
// the reflection shows neutral grey instead of black — preserving the M0
// chrome reflection baseline. Without this, the reflected pixel is
// indistinguishable from sky / IBL miss and the user sees no geometry at
// all in the chrome sphere.

TEST(RtHitGeneralGeometry, InertInstanceReturnsNeutralGreyFallback)
{
    cd::material::MaterialInstance inert;  // default-constructed, no device.
    ASSERT_FALSE(inert.is_valid());

    const auto sample = cd::material::ray_hit_sample(inert);

    // The contract: valid = false, albedo = neutral grey, emissive = zero.
    EXPECT_FALSE(sample.valid);
    EXPECT_NEAR(sample.albedo[0], cd::material::kRayHitFallbackGrey, kEps);
    EXPECT_NEAR(sample.albedo[1], cd::material::kRayHitFallbackGrey, kEps);
    EXPECT_NEAR(sample.albedo[2], cd::material::kRayHitFallbackGrey, kEps);
    EXPECT_NEAR(sample.emissive[0], 0.0F, kEps);
    EXPECT_NEAR(sample.emissive[1], 0.0F, kEps);
    EXPECT_NEAR(sample.emissive[2], 0.0F, kEps);

    // The neutral-grey value must read as "real reflection" against an HDR
    // sky background — not black. 0.6 luminance is high enough to be
    // visibly distinct from a miss / IBL fallback at sky brightness.
    EXPECT_GT(sample.albedo[0], 0.5F);
    EXPECT_LT(sample.albedo[0], 1.0F);
}

}  // namespace
