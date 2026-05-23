// =============================================================================
// CHROMODYNAMIC — cd::cluster_pbr tests
// =============================================================================
#include <cd/cluster_pbr/Lookup.hpp>
#include <cd/shader/Compiler.hpp>
#include <gtest/gtest.h>

#include <string>

namespace
{

TEST(ClusterPbrLookup, GlslSourceIsNonEmptyAndDeclaresFunctions)
{
    const auto src = cd::cluster_pbr::cluster_lookup_glsl();
    EXPECT_FALSE(src.empty());
    EXPECT_NE(src.find("cluster_lookup_id"), std::string_view::npos);
    EXPECT_NE(src.find("cluster_lookup_light_range"), std::string_view::npos);
    EXPECT_NE(src.find("cluster_lookup_light_count"), std::string_view::npos);
}

TEST(ClusterPbrLookup, PushConstantBlockIs32Bytes)
{
    EXPECT_EQ(sizeof(cd::cluster_pbr::LookupPushConstants), 32U);
}

TEST(ClusterPbrLookup, DescriptorBindingsAreSequential)
{
    EXPECT_EQ(cd::cluster_pbr::DescriptorBindings::kLightsBinding, 0U);
    EXPECT_EQ(cd::cluster_pbr::DescriptorBindings::kClusterOffsetsBinding, 1U);
    EXPECT_EQ(cd::cluster_pbr::DescriptorBindings::kLightIndicesBinding, 2U);
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
    src += cd::cluster_pbr::cluster_lookup_glsl();
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
    desc.target = cd::shader::TargetEnv::kVulkan_1_3;
    desc.source_name = "test_cluster_pbr.frag";
    auto r = compiler->compile(desc);
    ASSERT_TRUE(r.has_value()) << "GLSL compile failed: "
                                << r.error().code;
    EXPECT_GT(r->spirv.size(), 0U);
}

}  // namespace
