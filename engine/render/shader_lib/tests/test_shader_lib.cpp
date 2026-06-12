// =============================================================================
// CHROMODYNAMIC — engine/render/shader_lib/tests/test_shader_lib.cpp
// phase1133 (SL-C step 2): module catalogue + headless compile-all gate
// (ADR-20260612-shader-library-architecture §2.5 item 1).
//
// Pattern: Arrange / Act / Assert. GPU-free — glslang compiles to SPIR-V
// in-process; tests SKIP when the engine is built without glslang.
// =============================================================================
#include <cd/shader/Compiler.hpp>
#include <cd/shader_lib/ModuleRegistry.hpp>
#include <cd/shader_lib/VariantDomain.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace
{

// ---- Catalogue ---------------------------------------------------------------

TEST(ShaderLibRegistry, CatalogueIsNonEmptyAndSorted)
{
    const auto mods = cd::shader_lib::modules();
    ASSERT_GE(mods.size(), 3u);  // math_common + brdf + tonemap seeds
    for (std::size_t i = 1; i < mods.size(); ++i)
        EXPECT_LT(mods[i - 1].virtual_path, mods[i].virtual_path);
    for (const auto& m : mods)
    {
        EXPECT_FALSE(m.content.empty()) << m.virtual_path;
        // Every module carries its include guard (idempotency contract).
        EXPECT_NE(m.content.find("#ifndef CD_SL_"), std::string_view::npos)
            << m.virtual_path;
    }
}

TEST(ShaderLibRegistry, FindAcceptsCanonicalAndBarePaths)
{
    EXPECT_NE(cd::shader_lib::find_module("cd/shader_lib/brdf.glsl"), nullptr);
    EXPECT_NE(cd::shader_lib::find_module("brdf.glsl"), nullptr);
    EXPECT_EQ(cd::shader_lib::find_module("no_such_module.glsl"), nullptr);
    EXPECT_EQ(cd::shader_lib::find_module(""), nullptr);
}

TEST(ShaderLibRegistry, ResolverServesModules)
{
    cd::shader_lib::ModuleResolver resolver;
    const auto r = resolver.resolve("cd/shader_lib/math_common.glsl", "", true);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->virtual_path, "cd/shader_lib/math_common.glsl");
    EXPECT_FALSE(r->content.empty());
    EXPECT_FALSE(resolver.resolve("missing.glsl", "", true).has_value());
}

// ---- Headless compile-all gate ----------------------------------------------

[[nodiscard]] std::string wrap_module_in_fragment(std::string_view module_path)
{
    std::string src = "#version 450\n"
                      "#extension GL_GOOGLE_include_directive : enable\n"
                      "#include <";
    src += module_path;
    src += ">\n"
           "layout(location = 0) out vec4 o;\n"
           "void main() { o = vec4(0.0); }\n";
    return src;
}

TEST(ShaderLibCompile, EveryModuleCompilesStandalone)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    cd::shader_lib::ModuleResolver resolver;
    for (const auto& m : cd::shader_lib::modules())
    {
        const std::string src = wrap_module_in_fragment(m.virtual_path);
        cd::shader::CompileDesc desc {};
        desc.source = src;
        desc.stage = cd::shader::ShaderStage::kFragment;
        desc.source_name = m.virtual_path;
        desc.include_resolver = &resolver;
        const auto r = c->compile(desc);
        ASSERT_TRUE(r.has_value())
            << m.virtual_path << ": "
            << (r.has_value() ? "" : std::string(r.error().message));
        EXPECT_FALSE(r->spirv.empty()) << m.virtual_path;
    }
}

// Composition: brdf + tonemap together pull math_common TWICE through
// two paths — the include guards must dedupe (idempotency contract).
TEST(ShaderLibCompile, ModulesComposeWithGuardDedupe)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    const std::string src =
        "#version 450\n"
        "#extension GL_GOOGLE_include_directive : enable\n"
        "#include <cd/shader_lib/brdf.glsl>\n"
        "#include <cd/shader_lib/tonemap.glsl>\n"
        "#include <cd/shader_lib/math_common.glsl>\n"  // third hit, still fine
        "layout(location = 0) out vec4 o;\n"
        "void main()\n"
        "{\n"
        "    vec3 f = cd_f_schlick(vec3(0.04), 0.5);\n"
        "    vec3 t = cd_tonemap_aces_fitted(f);\n"
        "    o = vec4(t, 1.0);\n"
        "}\n";

    cd::shader_lib::ModuleResolver resolver;
    cd::shader::CompileDesc desc {};
    desc.source = src;
    desc.stage = cd::shader::ShaderStage::kFragment;
    desc.include_resolver = &resolver;
    const auto r = c->compile(desc);
    ASSERT_TRUE(r.has_value())
        << (r.has_value() ? "" : std::string(r.error().message));
    EXPECT_FALSE(r->spirv.empty());
}

// A real consumer-shaped shader: direct GGX lobe + Burley diffuse +
// Uchimura tonemap — exercises every brdf entry point end-to-end.
TEST(ShaderLibCompile, PbrLobeEndToEnd)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    const std::string src =
        "#version 450\n"
        "#extension GL_GOOGLE_include_directive : enable\n"
        "#include <cd/shader_lib/brdf.glsl>\n"
        "#include <cd/shader_lib/tonemap.glsl>\n"
        "layout(location = 0) in vec3 v_n;\n"
        "layout(location = 1) in vec3 v_v;\n"
        "layout(location = 2) in vec3 v_l;\n"
        "layout(location = 0) out vec4 o;\n"
        "void main()\n"
        "{\n"
        "    vec3 n = normalize(v_n); vec3 v = normalize(v_v);\n"
        "    vec3 l = normalize(v_l); vec3 h = normalize(v + l);\n"
        "    float nol = dot(n, l); float nov = dot(n, v);\n"
        "    float noh = dot(n, h); float voh = dot(v, h);\n"
        "    float rough = 0.4;\n"
        "    vec3 spec = cd_specular_ggx(vec3(0.04), noh, nov, nol, voh, rough);\n"
        "    vec3 diff = cd_fd_burley_pi(vec3(0.5), nov, nol, dot(l, h), rough);\n"
        "    vec3 b1; vec3 b2; cd_onb(n, b1, b2);\n"
        "    vec3 c0 = (diff + spec) * cd_saturate(nol) + 0.001 * (b1 + b2);\n"
        "    o = vec4(cd_tonemap_uchimura(c0), 1.0);\n"
        "}\n";

    cd::shader_lib::ModuleResolver resolver;
    cd::shader::CompileDesc desc {};
    desc.source = src;
    desc.stage = cd::shader::ShaderStage::kFragment;
    desc.include_resolver = &resolver;
    const auto r = c->compile(desc);
    ASSERT_TRUE(r.has_value())
        << (r.has_value() ? "" : std::string(r.error().message));
    EXPECT_FALSE(r->spirv.empty());
}

// ---- phase1134 (SL-C step 4): VariantDomain --------------------------------

using TestDomain = cd::shader_lib::VariantDomain<
    cd::shader_lib::BoolDim<"DIR_LIGHT">,
    cd::shader_lib::BoolDim<"SHADOW_RECV">,
    cd::shader_lib::EnumDim<"TONEMAP", 4>>;

// Filament-style curated validity: a shadow receiver needs the
// directional light.
constexpr auto kTestFilter = [](const TestDomain::Variant& v)
{
    return v.get<"SHADOW_RECV">() == 0u || v.get<"DIR_LIGHT">() == 1u;
};

// The curated budget is pinned AT COMPILE TIME: 2*2*4 = 16 raw, the
// filter rejects the 4 (SHADOW_RECV=1, DIR_LIGHT=0) combinations.
static_assert(TestDomain::space_size() == 16u);
static_assert(TestDomain::valid_count(kTestFilter) == 12u);

TEST(VariantDomain, EnumerationVisitsExactlyTheValidSet)
{
    std::uint64_t visited = 0;
    const auto accepted = TestDomain::for_each_valid(
        kTestFilter,
        [&](const TestDomain::Variant& v)
        {
            ++visited;
            EXPECT_TRUE(v.get<"SHADOW_RECV">() == 0u ||
                        v.get<"DIR_LIGHT">() == 1u);
            EXPECT_LT(v.get<"TONEMAP">(), 4u);
        });
    EXPECT_EQ(accepted, 12u);
    EXPECT_EQ(visited, 12u);
}

TEST(VariantDomain, DefinesSerialiseAlphabetically)
{
    TestDomain::Variant v {};
    v.set<"TONEMAP">(3u);
    v.set<"DIR_LIGHT">(1u);
    const auto defines = TestDomain::to_defines(v);
    ASSERT_EQ(defines.size(), 3u);
    EXPECT_EQ(defines[0].name, "DIR_LIGHT");
    EXPECT_EQ(defines[1].name, "SHADOW_RECV");
    EXPECT_EQ(defines[2].name, "TONEMAP");
    EXPECT_EQ(defines[0].value, 1u);
    EXPECT_EQ(defines[1].value, 0u);
    EXPECT_EQ(defines[2].value, 3u);
}

TEST(VariantDomain, PreambleCompilesWithModules)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    TestDomain::Variant v {};
    v.set<"DIR_LIGHT">(1u);
    v.set<"TONEMAP">(2u);

    std::string src = "#version 450\n"
                      "#extension GL_GOOGLE_include_directive : enable\n";
    src += TestDomain::to_preamble(v);
    src += "#include <cd/shader_lib/tonemap.glsl>\n"
           "layout(location = 0) out vec4 o;\n"
           "void main()\n"
           "{\n"
           "#if DIR_LIGHT\n"
           "    vec3 c0 = cd_tonemap_reinhard(vec3(float(TONEMAP)));\n"
           "#else\n"
           "    vec3 c0 = vec3(0.0);\n"
           "#endif\n"
           "    o = vec4(c0, 1.0);\n"
           "}\n";

    cd::shader_lib::ModuleResolver resolver;
    cd::shader::CompileDesc desc {};
    desc.source = src;
    desc.stage = cd::shader::ShaderStage::kFragment;
    desc.include_resolver = &resolver;
    const auto r = c->compile(desc);
    ASSERT_TRUE(r.has_value())
        << (r.has_value() ? "" : std::string(r.error().message));
    EXPECT_FALSE(r->spirv.empty());
}

}  // namespace
