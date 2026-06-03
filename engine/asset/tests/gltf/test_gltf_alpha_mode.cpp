// =============================================================================
// CHROMODYNAMIC — cd::asset_gltf alphaMode round-trip tests (T1.10, phase 640)
//
// Curtain reflection bug (phase629 hand-fix, audit
// docs/AUDIT/learned-lessons-curtain-reflection-2026-06-03.md) traced to the
// engine ignoring `alphaMode` from glTF source assets. Both the loader and
// `cd::material::MaterialInstance` now carry the state, but neither is
// exercised end-to-end without a sample app — these regression tests close
// that gap.
//
// Strategy: synthesise a minimal valid glTF in memory with the alphaMode
// field set to each of the three spec values (OPAQUE / MASK / BLEND), feed
// it to `cd::asset::gltf::load_gltf_from_memory`, and assert the decoded
// `GltfMaterial::alpha_mode` and `alpha_cutoff` match the source JSON.
//
// Coverage:
//   1. OPAQUE  — alphaMode field absent (spec default).
//   2. MASK    — alphaMode="MASK", alphaCutoff=0.6 (non-default cutoff
//                so we know the value really round-tripped, not just the
//                fallback 0.5).
//   3. BLEND   — alphaMode="BLEND".
//
// A 4th sub-test pipes the decoded values into
// `cd::material::MaterialInstance::set_alpha_*` to prove the render-tier
// API surface accepts the same enum without a static_cast at the call site
// (the shared `cd::material::AlphaMode` enum is the contract).
// =============================================================================
#include <cd/asset/gltf/GltfLoader.hpp>
#include <cd/material/AlphaMode.hpp>
#include <cd/material/Material.hpp>
#include <cd/rhi/NullDevice.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace
{

/// Build a minimal glTF JSON document that declares one material with the
/// caller-provided alphaMode JSON snippet. The mesh / buffer machinery is
/// the bare minimum tinygltf will accept: a 1-triangle POSITION-only
/// primitive sourced from a base64-encoded data URI.
///
/// `alpha_mode_snippet` is concatenated into the material object, e.g.
///   R"(,"alphaMode":"MASK","alphaCutoff":0.6)"
/// — pass an empty string to exercise the spec's default (OPAQUE / 0.5).
[[nodiscard]] std::string make_minimal_gltf_with_alpha(std::string_view alpha_mode_snippet)
{
    // 3 vec3 positions = 36 bytes, 3 uint16 indices = 6 bytes -> 42 bytes.
    // base64 of [-1,0,0, 1,0,0, 0,1,0] + [0,1,2] is precomputed below so
    // tests don't depend on an in-process base64 encoder (cd::asset_json /
    // tinygltf both have decoders that handle the data: URI variant we
    // emit). The exact triangle vertices are irrelevant for this test —
    // only the material parse path matters.
    constexpr std::string_view kTriBin64 =
        "AACAvwAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIA";

    std::string j;
    j.reserve(512);
    j += R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":42,"uri":"data:application/octet-stream;base64,)";
    j += kTriBin64;
    j += R"("}],"bufferViews":[)";
    j += R"({"buffer":0,"byteOffset":0,"byteLength":36,"target":34962},)";
    j += R"({"buffer":0,"byteOffset":36,"byteLength":6,"target":34963}],)";
    j += R"("accessors":[)";
    j += R"({"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},)";
    j += R"({"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],)";
    j += R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0}]}],)";
    j += R"("materials":[{"name":"alpha_test","pbrMetallicRoughness":{"baseColorFactor":[1,1,1,1]})";
    j += alpha_mode_snippet;
    j += R"(}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
    return j;
}

[[nodiscard]] cd::core::Result<cd::asset::gltf::GltfScene>
load_alpha_test_scene(std::string_view alpha_snippet)
{
    const std::string json = make_minimal_gltf_with_alpha(alpha_snippet);
    return cd::asset::gltf::load_gltf_from_memory(
        reinterpret_cast<const std::uint8_t*>(json.data()),
        json.size()
    );
}

}  // namespace

// -----------------------------------------------------------------------------
// 1) alphaMode absent  → kOpaque, default cutoff 0.5
// -----------------------------------------------------------------------------

TEST(GltfAlphaMode, OpaqueWhenAlphaModeFieldAbsent)
{
    auto r = load_alpha_test_scene(/*alpha_snippet=*/"");
    ASSERT_TRUE(r.has_value()) << r.error().message;

    ASSERT_EQ(r->materials.size(), 1U);
    const auto& m = r->materials.front();
    EXPECT_EQ(m.alpha_mode, cd::asset::gltf::GltfAlphaMode::kOpaque);
    EXPECT_FLOAT_EQ(m.alpha_cutoff, 0.5F);
}

// -----------------------------------------------------------------------------
// 2) alphaMode = "MASK" + alphaCutoff = 0.6  → kMask, 0.6
// -----------------------------------------------------------------------------

TEST(GltfAlphaMode, MaskWithCustomCutoffRoundtrips)
{
    auto r = load_alpha_test_scene(R"(,"alphaMode":"MASK","alphaCutoff":0.6)");
    ASSERT_TRUE(r.has_value()) << r.error().message;

    ASSERT_EQ(r->materials.size(), 1U);
    const auto& m = r->materials.front();
    EXPECT_EQ(m.alpha_mode, cd::asset::gltf::GltfAlphaMode::kMask);
    EXPECT_FLOAT_EQ(m.alpha_cutoff, 0.6F);
}

// -----------------------------------------------------------------------------
// 3) alphaMode = "BLEND"  → kBlend (cutoff field is decoded but unused)
// -----------------------------------------------------------------------------

TEST(GltfAlphaMode, BlendDecodes)
{
    auto r = load_alpha_test_scene(R"(,"alphaMode":"BLEND")");
    ASSERT_TRUE(r.has_value()) << r.error().message;

    ASSERT_EQ(r->materials.size(), 1U);
    const auto& m = r->materials.front();
    EXPECT_EQ(m.alpha_mode, cd::asset::gltf::GltfAlphaMode::kBlend);
}

// -----------------------------------------------------------------------------
// 4) Loader → cd::material::MaterialInstance plumbing
//
// Confirms the render-tier API surface accepts the same logical state. The
// `cd::asset::gltf::GltfAlphaMode` and `cd::material::AlphaMode` enums share
// numeric values by construction (see AlphaMode.hpp comment); a `static_cast`
// is the documented round-trip path until a free helper lands in a follow-up
// sprint. The cast is exercised here so any drift between the two enums
// surfaces immediately.
// -----------------------------------------------------------------------------

TEST(GltfAlphaMode, MaterialInstanceCarriesParsedAlphaState)
{
    auto r = load_alpha_test_scene(R"(,"alphaMode":"MASK","alphaCutoff":0.6)");
    ASSERT_TRUE(r.has_value()) << r.error().message;
    const auto& src = r->materials.front();

    // Build a MaterialInstance bound to a NullDevice — sufficient for the
    // CPU-side accessor probe (no GPU traffic).
    cd::rhi::NullDevice dev;
    constexpr std::array<std::uint32_t, 5> kStubSpirv {
        0x07230203U, 0x00010000U, 0x00000000U, 1U, 0U
    };
    constexpr std::array<cd::rhi::DescriptorSetLayoutBinding, 1> kBindings {
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 0,
            .type = cd::rhi::DescriptorType::kUniformBuffer,
            .count = 1,
            .stages = cd::rhi::ShaderStage::kFragment,
        }
    };
    cd::material::MaterialDesc desc {};
    desc.vertex_spirv = kStubSpirv;
    desc.fragment_spirv = kStubSpirv;
    desc.descriptor_bindings = kBindings;
    desc.name = "alpha_test";

    auto mat = cd::material::Material::create(dev, nullptr, desc);
    ASSERT_TRUE(mat.has_value()) << mat.error().message;
    auto inst = cd::material::MaterialInstance::create(dev, *mat);
    ASSERT_TRUE(inst.has_value()) << inst.error().message;

    // Defaults match the glTF 2.0 spec (OPAQUE / 0.5).
    EXPECT_EQ(inst->alpha_mode(), cd::material::AlphaMode::kOpaque);
    EXPECT_FLOAT_EQ(inst->alpha_cutoff(), 0.5F);

    // Forward the loader-side state through the documented static_cast.
    inst->set_alpha_mode(static_cast<cd::material::AlphaMode>(src.alpha_mode));
    inst->set_alpha_cutoff(src.alpha_cutoff);

    EXPECT_EQ(inst->alpha_mode(), cd::material::AlphaMode::kMask);
    EXPECT_FLOAT_EQ(inst->alpha_cutoff(), 0.6F);

    // Helper predicates reflect the new state.
    EXPECT_TRUE(cd::material::needs_alpha_test(inst->alpha_mode()));
    EXPECT_FALSE(cd::material::needs_blend(inst->alpha_mode()));

    // Out-of-range cutoff is clamped per the contract.
    inst->set_alpha_cutoff(1.5F);
    EXPECT_FLOAT_EQ(inst->alpha_cutoff(), 1.0F);
    inst->set_alpha_cutoff(-0.25F);
    EXPECT_FLOAT_EQ(inst->alpha_cutoff(), 0.0F);

    // AlphaParams setter is a convenience wrapper for the two-step path.
    inst->set_alpha_params(cd::material::AlphaParams { cd::material::AlphaMode::kBlend, 0.5F });
    EXPECT_EQ(inst->alpha_mode(), cd::material::AlphaMode::kBlend);
    EXPECT_TRUE(cd::material::needs_blend(inst->alpha_mode()));
}
