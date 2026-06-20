// =============================================================================
// CHROMODYNAMIC — cd::cluster_pbr tests
// =============================================================================
#include <cd/cluster/pbr/Lookup.hpp>
#include <cd/shader/Compiler.hpp>
#include <gtest/gtest.h>

#include <cstddef>
#include <string>

namespace
{

TEST(ClusterPbrLookup, GlslSourceIsNonEmptyAndDeclaresFunctions)
{
    const auto src = cd::cluster::pbr::cluster_lookup_glsl();
    EXPECT_FALSE(src.empty());
    EXPECT_NE(src.find("cluster_lookup_id"), std::string_view::npos);
    EXPECT_NE(src.find("cluster_lookup_light_range"), std::string_view::npos);
    EXPECT_NE(src.find("cluster_lookup_light_count"), std::string_view::npos);
}

TEST(ClusterPbrLookup, PushConstantBlockIs32Bytes)
{
    EXPECT_EQ(sizeof(cd::cluster::pbr::LookupPushConstants), 32U);
}

TEST(ClusterPbrLookup, DescriptorBindingsAreSequential)
{
    EXPECT_EQ(cd::cluster::pbr::DescriptorBindings::kLightsBinding, 0U);
    EXPECT_EQ(cd::cluster::pbr::DescriptorBindings::kClusterOffsetsBinding, 1U);
    EXPECT_EQ(cd::cluster::pbr::DescriptorBindings::kLightIndicesBinding, 2U);
}

TEST(ClusterPbrLookup, GlslCompilesAsPartOfAFragmentShader)
{
    auto compiler = cd::shader::make_glslang_compiler();
    if (!compiler)
        GTEST_SKIP() << "glslang backend not enabled";

    // Smoke fragment shader that #includes the helper at the top
    // (via direct string concatenation since glslang's include
    // resolver isn't wired here) and exercises every public function.
    std::string src;
    src += "#version 460\n";
    src += "#extension GL_GOOGLE_include_directive : enable\n";
    src += "layout(location = 0) in vec3 in_view_pos;\n";
    src += "layout(location = 0) out vec4 out_color;\n";
    src += cd::cluster::pbr::cluster_lookup_glsl();
    src += R"GLSL(
void main() {
    uint cid = cluster_lookup_id(in_view_pos);
    uvec2 range = cluster_lookup_light_range(cid);
    float acc = 0.0;
    for (uint k = range.x; k < range.y; ++k) {
        uint light_id = u_cluster_pbr_indices.indices[k];
        ClusterPbrLight L = u_cluster_pbr_lights.lights[light_id];
        acc += L.position_radius.w;
    }
    out_color = vec4(acc, float(cluster_lookup_light_count()), 0.0, 1.0);
}
)GLSL";

    cd::shader::CompileDesc desc;
    desc.source = src;
    desc.stage = cd::shader::ShaderStage::kFragment;
    desc.lang = cd::shader::ShaderLanguage::kGlsl;
    desc.target = cd::shader::TargetEnv::kVulkan13;
    desc.source_name = "test_cluster_pbr.frag";
    auto r = compiler->compile(desc);
    ASSERT_TRUE(r.has_value()) << "GLSL compile failed: "
                                << r.error().code;
    EXPECT_GT(r->spirv.size(), 0U);
}

// =============================================================================
// 80->100 marathon — ADD-ONLY host-side coverage of the PBR lookup contract.
// No GLSL math is modified; these pin the EXISTING descriptor-set / push-
// constant layout and the GLSL helper's structural contract so the Forward+
// material wiring stays byte-identical.
// =============================================================================

TEST(ClusterPbrLookup, PushConstantFieldsMatchAssignComputeLayout)
{
    // The lookup push-constant block must mirror cluster_assign.comp's
    // first seven fields (cells_x/y/z + near/far + fov + aspect) and a
    // trailing pad to 32 bytes. Verify field offsets pin the std430 wire
    // layout the host pipeline relies on.
    using PC = cd::cluster::pbr::LookupPushConstants;
    EXPECT_EQ(offsetof(PC, cells_x), 0U);
    EXPECT_EQ(offsetof(PC, cells_y), 4U);
    EXPECT_EQ(offsetof(PC, cells_z), 8U);
    EXPECT_EQ(offsetof(PC, near_plane), 12U);
    EXPECT_EQ(offsetof(PC, far_plane), 16U);
    EXPECT_EQ(offsetof(PC, fov_y_rad), 20U);
    EXPECT_EQ(offsetof(PC, aspect), 24U);
    EXPECT_EQ(offsetof(PC, pad), 28U);
    static_assert(sizeof(PC) == 32U, "lookup PC must stay 32 bytes");
}

TEST(ClusterPbrLookup, GlslDeclaresTheThreeStorageBuffers)
{
    // The host material allocates SSBOs at bindings 0/1/2 in CLUSTER_PBR_SET;
    // the GLSL must declare all three so the bindings line up.
    const auto src = cd::cluster::pbr::cluster_lookup_glsl();
    EXPECT_NE(src.find("ClusterPbrInLights"), std::string_view::npos);
    EXPECT_NE(src.find("ClusterPbrOffsets"), std::string_view::npos);
    EXPECT_NE(src.find("ClusterPbrIndices"), std::string_view::npos);
    EXPECT_NE(src.find("binding = 0"), std::string_view::npos);
    EXPECT_NE(src.find("binding = 1"), std::string_view::npos);
    EXPECT_NE(src.find("binding = 2"), std::string_view::npos);
}

TEST(ClusterPbrLookup, GlslExposesTheSetOverrideMacro)
{
    // CLUSTER_PBR_SET must be overridable (guarded #define default 2) so a
    // caller can relocate the descriptor set without editing the helper.
    const auto src = cd::cluster::pbr::cluster_lookup_glsl();
    EXPECT_NE(src.find("#ifndef CLUSTER_PBR_SET"), std::string_view::npos);
    EXPECT_NE(src.find("#define CLUSTER_PBR_SET 2"), std::string_view::npos);
    EXPECT_NE(src.find("set = CLUSTER_PBR_SET"), std::string_view::npos);
}

TEST(ClusterPbrLookup, GlslDeclaresPushConstantBlockFields)
{
    // The fragment-shader push-constant block mirrors LookupPushConstants;
    // confirm every field name is present so the host upload matches.
    const auto src = cd::cluster::pbr::cluster_lookup_glsl();
    EXPECT_NE(src.find("ClusterPbrPC"), std::string_view::npos);
    EXPECT_NE(src.find("cells_x"), std::string_view::npos);
    EXPECT_NE(src.find("cells_y"), std::string_view::npos);
    EXPECT_NE(src.find("cells_z"), std::string_view::npos);
    EXPECT_NE(src.find("near_plane"), std::string_view::npos);
    EXPECT_NE(src.find("far_plane"), std::string_view::npos);
    EXPECT_NE(src.find("fov_y_rad"), std::string_view::npos);
    EXPECT_NE(src.find("aspect"), std::string_view::npos);
}

TEST(ClusterPbrLookup, GlslContainsLogZAndAngleHelpers)
{
    // The lookup reuses the same log-Z depth slice + angle->cluster math as
    // the CPU reference; confirm the helper functions are present so the
    // fragment shader computes the same cluster id as the assign pass.
    const auto src = cd::cluster::pbr::cluster_lookup_glsl();
    EXPECT_NE(src.find("cluster_pbr_depth_z"), std::string_view::npos);
    EXPECT_NE(src.find("cluster_pbr_xy"), std::string_view::npos);
    EXPECT_NE(src.find("log("), std::string_view::npos);
    EXPECT_NE(src.find("atan("), std::string_view::npos);
}

TEST(ClusterPbrLookup, GlslLightStructMatchesAssignComputeWire)
{
    // ClusterPbrLight must be a single vec4 position_radius — identical to
    // cluster_assign.comp's LightData — so the same SSBO is reusable.
    const auto src = cd::cluster::pbr::cluster_lookup_glsl();
    EXPECT_NE(src.find("ClusterPbrLight"), std::string_view::npos);
    EXPECT_NE(src.find("vec4 position_radius"), std::string_view::npos);
}

}  // namespace
