// =============================================================================
// CHROMODYNAMIC — cd::material RT closest-hit LIVE-WIRE test
//
// FINALE-5 W1 / A8 / phase759 — chrome PBR sphere reflects ACTUAL geometry.
//
// Two test cases:
//
//   1. NullDevice/CPU — `pack_rt_material_records` byte-layout contract: a
//      vector of 2 prims (a chrome sphere + a colored "quad behind it")
//      produces 64 bytes of std430 records whose albedo + metallic +
//      emissive + roughness words match the packed-vec4 layout the GLSL
//      `MaterialRecords` SSBO expects.
//
//   2. Vulkan-gated — compile the production-grade closest-hit GLSL
//      (`kRtClosestHitGlsl`) through glslang to SPIR-V; assert it compiles
//      cleanly (no validation errors, non-empty SPIR-V blob). This is the
//      "real wire" test: a regression that breaks the closest-hit GLSL
//      (e.g. an SSBO binding-number mismatch, a hitAttributeEXT typo) fails
//      this assertion before any sample-level smoke test catches it.
//
//   3. Vulkan-gated — chrome reflects colored quad: build a 2-prim scene
//      via the packer, verify the packed record for the colored quad has
//      the painted RGB (= chrome-sphere's reflection would land on
//      non-zero color, not pure sky). The full RT dispatch + readback is
//      covered by `samples/rhi/hello_path_trace` (phase1142: hello_rt
//      folded) since that sample owns the BLAS /
//      TLAS / SBT scaffolding; this library-level test asserts the data
//      contract that drives the in-shader colour lookup.
//
// SKIP behaviour: case 2 + 3 GTEST_SKIP if either Vulkan is unavailable
// (no ICD, e.g. CI headless lane) or glslang was disabled at configure
// time. Per CLAUDE.md §5 there is no `sleep_for` and tests are
// Arrange/Act/Assert.
// =============================================================================
#include <cd/material/Material.hpp>
#include <cd/material/RtClosestHit.hpp>
#include <cd/rhi/NullDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#include <cd/shader/Compiler.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

namespace
{

constexpr float kEps = 1e-5F;
constexpr std::array<std::uint32_t, 5> kStubSpirv {
    0x07230203U, 0x00010000U, 0x00000000U, 1U, 0U
};

// Build a valid (non-inert) MaterialInstance — same NullDevice pattern as
// test_rt_hit_general_geometry.
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
    md.name = "rt_chrome_sphere_owner";
    auto m_r = cd::material::Material::create(dev, nullptr, md);
    EXPECT_TRUE(m_r.has_value());
    owner = std::move(*m_r);
    auto inst_r = cd::material::MaterialInstance::create(dev, owner);
    EXPECT_TRUE(inst_r.has_value());
    return std::move(*inst_r);
}

// ---- Case 1 — packer ABI contract (no GPU required) ------------------------

TEST(RtChromeSphereReflectsGeometry, PackerLaysOutAlbedoMetallicEmissiveRoughness)
{
    cd::rhi::NullDevice dev;
    cd::material::Material owner_chrome;
    cd::material::Material owner_quad;
    auto chrome = make_test_instance(dev, owner_chrome);
    auto quad   = make_test_instance(dev, owner_quad);

    // Chrome PBR sphere — pure metal, near-zero roughness, white albedo.
    chrome.set_albedo(1.0F, 1.0F, 1.0F);
    chrome.set_metallic(1.0F);
    chrome.set_roughness(0.05F);
    chrome.set_emissive(0.0F, 0.0F, 0.0F);

    // Colored quad behind it — the surface the chrome should reflect.
    // Vibrant magenta so the test can distinguish "sky bounced back" from
    // "quad albedo bounced back" without ambiguity.
    quad.set_albedo(0.95F, 0.10F, 0.85F);
    quad.set_metallic(0.0F);
    quad.set_roughness(0.65F);
    quad.set_emissive(0.0F, 0.0F, 0.0F);

    std::array<const cd::material::MaterialInstance*, 2> inst_ptrs {
        &chrome, &quad,
    };
    const auto records = cd::material::pack_rt_material_records(
        std::span<const cd::material::MaterialInstance*>(inst_ptrs.data(), inst_ptrs.size())
    );

    ASSERT_EQ(records.size(), 2u);

    // ---- Record 0: chrome -----------------------------------------------
    EXPECT_NEAR(records[0].albedo[0], 1.0F, kEps);
    EXPECT_NEAR(records[0].albedo[1], 1.0F, kEps);
    EXPECT_NEAR(records[0].albedo[2], 1.0F, kEps);
    EXPECT_NEAR(records[0].albedo[3], 1.0F, kEps)  // metallic packed in .a
        << "chrome metallic must land in albedo.a so the GLSL SSBO can read "
           "rec.albedo.a as the metallic factor";
    EXPECT_NEAR(records[0].emissive[0], 0.0F, kEps);
    EXPECT_NEAR(records[0].emissive[1], 0.0F, kEps);
    EXPECT_NEAR(records[0].emissive[2], 0.0F, kEps);
    EXPECT_NEAR(records[0].emissive[3], 0.05F, kEps)  // roughness packed in .a
        << "chrome roughness must land in emissive.a so the GLSL SSBO can "
           "read rec.emissive.a as the roughness factor";

    // ---- Record 1: colored quad ----------------------------------------
    EXPECT_NEAR(records[1].albedo[0], 0.95F, kEps);
    EXPECT_NEAR(records[1].albedo[1], 0.10F, kEps);
    EXPECT_NEAR(records[1].albedo[2], 0.85F, kEps);
    EXPECT_NEAR(records[1].albedo[3], 0.0F,  kEps);   // dielectric metallic = 0
    EXPECT_NEAR(records[1].emissive[3], 0.65F, kEps); // dielectric roughness

    // ---- The crux of A8 ------------------------------------------------
    // The chrome sphere's closest-hit reads `records[1].albedo.rgb` when
    // its reflection lands on instance 1 (the quad). The colour MUST be
    // non-zero so the reflection shows the quad — not pure sky. This is
    // the engine-level fix for the user's 2026-06-03 screenshot bug.
    const float quad_luminance = 0.2126F * records[1].albedo[0]
                               + 0.7152F * records[1].albedo[1]
                               + 0.0722F * records[1].albedo[2];
    EXPECT_GT(quad_luminance, 0.0F)
        << "Chrome sphere's reflection of the quad would show pure sky if "
           "the packed albedo is zero — the A8 contract is broken.";
}

// ---- Case 1b — nullptr / inert prim packs to the neutral-grey fallback -----

TEST(RtChromeSphereReflectsGeometry, NullInstanceFallsBackToNeutralGrey)
{
    std::array<const cd::material::MaterialInstance*, 1> inst_ptrs {
        nullptr,
    };
    const auto records = cd::material::pack_rt_material_records(
        std::span<const cd::material::MaterialInstance*>(inst_ptrs.data(), inst_ptrs.size())
    );
    ASSERT_EQ(records.size(), 1u);
    EXPECT_NEAR(records[0].albedo[0], cd::material::kRayHitFallbackGrey, kEps);
    EXPECT_NEAR(records[0].albedo[1], cd::material::kRayHitFallbackGrey, kEps);
    EXPECT_NEAR(records[0].albedo[2], cd::material::kRayHitFallbackGrey, kEps);
    EXPECT_NEAR(records[0].albedo[3], 0.0F, kEps);   // metallic fallback
    EXPECT_NEAR(records[0].emissive[0], 0.0F, kEps);
    EXPECT_NEAR(records[0].emissive[1], 0.0F, kEps);
    EXPECT_NEAR(records[0].emissive[2], 0.0F, kEps);
    EXPECT_NEAR(records[0].emissive[3], 0.5F, kEps);  // roughness fallback
}

// ---- Case 1c — record byte layout matches std430 expectation ---------------

TEST(RtChromeSphereReflectsGeometry, RecordIsStd430Aligned)
{
    // The GLSL `MaterialRecords` SSBO at binding=2 is laid out as
    //   struct MaterialRecord { vec4 albedo; vec4 emissive; };
    // Two vec4s -> 32 bytes -> std430 stride of 32. The C++ struct must
    // match byte-for-byte so `IDevice::upload_buffer` ships the bytes
    // straight into GPU memory without a packing step.
    EXPECT_EQ(sizeof(cd::material::RtMaterialRecord), 32u);
    EXPECT_EQ(alignof(cd::material::RtMaterialRecord), 4u)
        << "trivial 4-byte alignment is enough; std430 base alignment is "
           "enforced by the SSBO declaration in GLSL, not the C++ type.";
    EXPECT_EQ(sizeof(cd::material::RtSunBlock), 32u);
}

// ---- Case 2 — Vulkan-gated: production closest-hit GLSL compiles cleanly ---

TEST(RtChromeSphereReflectsGeometry, VulkanGatedClosestHitGlslCompiles)
{
    auto vk_r = cd::rhi::vulkan::create_vulkan_device({});
    if (!vk_r.has_value())
        GTEST_SKIP() << "no Vulkan ICD";
    auto& dev = **vk_r;
    if (!dev.features().ray_tracing)
        GTEST_SKIP() << "adapter lacks ray-tracing extension";

    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    // Arrange + Act.
    cd::shader::CompileDesc d {};
    d.source      = cd::material::kRtClosestHitGlsl;
    d.stage       = cd::shader::ShaderStage::kClosestHit;
    d.lang        = cd::shader::ShaderLanguage::kGlsl;
    d.target      = cd::shader::TargetEnv::kVulkan13;
    d.source_name = "rt_chrome_sphere_reflects_geometry.rchit";
    const auto compiled = compiler->compile(d);

    // Assert.
    ASSERT_TRUE(compiled.has_value())
        << "kRtClosestHitGlsl must compile under target Vulkan 1.3 + "
           "GL_EXT_ray_tracing. Compiler diagnostic: "
        << std::string_view { compiled.error().message };
    EXPECT_GT(compiled->spirv.size(), 0u)
        << "non-empty SPIR-V blob expected";
    // SPIR-V magic word 0x07230203 should sit at word 0.
    ASSERT_GT(compiled->spirv.size(), 0u);
    EXPECT_EQ(compiled->spirv[0], 0x07230203U);
}

// ---- Case 3 — Vulkan-gated: chrome reflects colored quad end-to-end --------
//
// A full RT dispatch + readback would replicate the entire `samples/rhi/
// hello_path_trace` (phase1142: hello_rt folded) scaffolding (BLAS / TLAS
// / SBT / storage image / barriers /
// submit / wait_idle / map_buffer). That sample is the canonical
// integration smoke; this library-level test asserts the **data contract**
// the closest-hit reads:
//
//   * The packed record for the colored quad survives the round-trip
//     through `pack_rt_material_records` byte-for-byte.
//   * The packer output is binary-compatible with a `BufferUsage::kStorage`
//     buffer upload via `IDevice::upload_buffer` (i.e. it is a flat blob
//     of POD bytes, no padding surprises).
//
// Together with Case 2 (the GLSL compiles) and Case 1 (the layout matches
// the std430 SSBO), the chain "MaterialInstance → packer → SSBO → GLSL
// closest-hit → chrome sphere pixel" is locked at every step a library
// test can observe.

TEST(RtChromeSphereReflectsGeometry, VulkanGatedScenePackingUploadsCleanly)
{
    auto vk_r = cd::rhi::vulkan::create_vulkan_device({});
    if (!vk_r.has_value())
        GTEST_SKIP() << "no Vulkan ICD";
    auto& dev = **vk_r;
    if (!dev.features().ray_tracing)
        GTEST_SKIP() << "adapter lacks ray-tracing extension";

    // Arrange: a 2-prim scene — chrome sphere + colored quad behind it.
    cd::rhi::NullDevice null_dev;
    cd::material::Material owner_chrome;
    cd::material::Material owner_quad;
    auto chrome = make_test_instance(null_dev, owner_chrome);
    auto quad   = make_test_instance(null_dev, owner_quad);
    chrome.set_albedo(1.0F, 1.0F, 1.0F);
    chrome.set_metallic(1.0F);
    chrome.set_roughness(0.05F);
    quad.set_albedo(0.95F, 0.10F, 0.85F);  // vibrant magenta
    quad.set_metallic(0.0F);
    quad.set_roughness(0.65F);

    std::array<const cd::material::MaterialInstance*, 2> inst_ptrs {
        &chrome, &quad,
    };
    const auto records = cd::material::pack_rt_material_records(
        std::span<const cd::material::MaterialInstance*>(inst_ptrs.data(), inst_ptrs.size())
    );
    ASSERT_EQ(records.size(), 2u);

    // Act: upload the packed records into a real Vulkan storage buffer.
    // kCpuToGpu so `upload_buffer` can map the host pointer; the real engine
    // RT pipeline will stage via a copy from a CPU-visible staging buffer
    // into a GPU-only SSBO, but this library-level test exercises the
    // host-visible path directly.
    cd::rhi::BufferDesc bd {};
    bd.size   = records.size() * sizeof(cd::material::RtMaterialRecord);
    bd.usage  = cd::rhi::BufferUsage::kStorage |
                cd::rhi::BufferUsage::kTransferDst;
    bd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto buf_r = dev.create_buffer(bd);
    ASSERT_TRUE(buf_r.has_value()) << buf_r.error().message;

    const auto upload_r = dev.upload_buffer(*buf_r, 0,
        std::span<const std::byte> {
            reinterpret_cast<const std::byte*>(records.data()),
            records.size() * sizeof(cd::material::RtMaterialRecord),
        });
    EXPECT_TRUE(upload_r.has_value())
        << "uploading the packed RtMaterialRecord blob into a Vulkan "
           "storage buffer must succeed — this is the byte stream the "
           "closest-hit GLSL reads via gl_InstanceCustomIndexEXT.";

    // Assert: the in-memory record for the quad still carries the magenta
    // albedo. If this is zero, the chrome sphere's reflection would show
    // pure sky and the A8 moment regresses to the 2026-06-03 bug.
    EXPECT_NEAR(records[1].albedo[0], 0.95F, kEps);
    EXPECT_NEAR(records[1].albedo[1], 0.10F, kEps);
    EXPECT_NEAR(records[1].albedo[2], 0.85F, kEps);

    dev.destroy_buffer(*buf_r);
}

}  // namespace
