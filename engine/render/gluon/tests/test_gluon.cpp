// =============================================================================
// CHROMODYNAMIC — engine/render/gluon/tests/test_gluon.cpp
// phase1133 (SL-C step 2): module catalogue + headless compile-all gate
// (ADR-20260612-shader-library-architecture §2.5 item 1).
//
// Pattern: Arrange / Act / Assert. GPU-free — glslang compiles to SPIR-V
// in-process; tests SKIP when the engine is built without glslang.
// =============================================================================
#include <cd/shader/CachedCompiler.hpp>
#include <cd/shader/Compiler.hpp>
#include <cd/gluon/ModuleRegistry.hpp>
#include <cd/gluon/VariantDomain.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace
{

// ---- Catalogue ---------------------------------------------------------------

TEST(ShaderLibRegistry, CatalogueIsNonEmptyAndSorted)
{
    const auto mods = cd::gluon::modules();
    ASSERT_GE(mods.size(), 3u);  // math_common + brdf + tonemap seeds
    for (std::size_t i = 1; i < mods.size(); ++i)
        EXPECT_LT(mods[i - 1].virtual_path, mods[i].virtual_path);
    for (const auto& m : mods)
    {
        EXPECT_FALSE(m.content.empty()) << m.virtual_path;
        // Every module carries its include guard (idempotency contract).
        EXPECT_NE(m.content.find("#ifndef CD_GLUON_"), std::string_view::npos)
            << m.virtual_path;
    }
}

TEST(ShaderLibRegistry, FindAcceptsCanonicalAndBarePaths)
{
    EXPECT_NE(cd::gluon::find_module("cd/gluon/brdf.glsl"), nullptr);
    EXPECT_NE(cd::gluon::find_module("brdf.glsl"), nullptr);
    EXPECT_EQ(cd::gluon::find_module("no_such_module.glsl"), nullptr);
    EXPECT_EQ(cd::gluon::find_module(""), nullptr);
}

TEST(ShaderLibRegistry, ResolverServesModules)
{
    cd::gluon::ModuleResolver resolver;
    const auto r = resolver.resolve("cd/gluon/math_common.glsl", "", true);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->virtual_path, "cd/gluon/math_common.glsl");
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

    cd::gluon::ModuleResolver resolver;
    for (const auto& m : cd::gluon::modules())
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
        "#include <cd/gluon/brdf.glsl>\n"
        "#include <cd/gluon/tonemap.glsl>\n"
        "#include <cd/gluon/math_common.glsl>\n"  // third hit, still fine
        "layout(location = 0) out vec4 o;\n"
        "void main()\n"
        "{\n"
        "    vec3 f = cd_f_schlick(vec3(0.04), 0.5);\n"
        "    vec3 t = cd_tonemap_aces_fitted(f);\n"
        "    o = vec4(t, 1.0);\n"
        "}\n";

    cd::gluon::ModuleResolver resolver;
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
        "#include <cd/gluon/brdf.glsl>\n"
        "#include <cd/gluon/tonemap.glsl>\n"
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

    cd::gluon::ModuleResolver resolver;
    cd::shader::CompileDesc desc {};
    desc.source = src;
    desc.stage = cd::shader::ShaderStage::kFragment;
    desc.include_resolver = &resolver;
    const auto r = c->compile(desc);
    ASSERT_TRUE(r.has_value())
        << (r.has_value() ? "" : std::string(r.error().message));
    EXPECT_FALSE(r->spirv.empty());
}

// Canonical BRDF unification (hello_engine prim path): the roughness-
// aware Schlick Fresnel added to brdf.glsl for the ambient / IBL kD
// split must compile + link standalone. Mirrors the prim.frag IBL site.
TEST(ShaderLibCompile, RoughnessFresnelCompiles)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    const std::string src =
        "#version 450\n"
        "#extension GL_GOOGLE_include_directive : enable\n"
        "#include <cd/gluon/brdf.glsl>\n"
        "layout(location = 0) in vec3 v_n;\n"
        "layout(location = 1) in vec3 v_v;\n"
        "layout(location = 0) out vec4 o;\n"
        "void main()\n"
        "{\n"
        "    vec3 n = normalize(v_n); vec3 v = normalize(v_v);\n"
        "    float nov = dot(n, v); float rough = 0.6;\n"
        "    vec3 f0 = vec3(0.04);\n"
        "    vec3 f = cd_f_schlick_roughness(f0, nov, rough);\n"
        "    vec3 kd = (vec3(1.0) - f) * 0.75;\n"
        "    o = vec4(kd, 1.0);\n"
        "}\n";

    cd::gluon::ModuleResolver resolver;
    cd::shader::CompileDesc desc {};
    desc.source = src;
    desc.stage = cd::shader::ShaderStage::kFragment;
    desc.include_resolver = &resolver;
    const auto r = c->compile(desc);
    ASSERT_TRUE(r.has_value())
        << (r.has_value() ? "" : std::string(r.error().message));
    EXPECT_FALSE(r->spirv.empty());
}

// Canonical ltc_specular.glsl (replaces transitional ltc_standard_pbr.glsl):
// the cd_-prefixed GGX-specular extension of ltc_polygon.glsl compiles
// standalone and cd_ltc_polygon_specular is callable through the resolver.
TEST(ShaderLibCompile, LtcSpecularCompiles)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    const std::string src =
        "#version 450\n"
        "#extension GL_GOOGLE_include_directive : enable\n"
        "#include <cd/gluon/ltc_specular.glsl>\n"
        "layout(location = 0) in vec3 v_n;\n"
        "layout(location = 0) out vec4 o;\n"
        "void main()\n"
        "{\n"
        "    vec3 n = normalize(v_n);\n"
        "    float s = cd_ltc_polygon_specular(n,\n"
        "        vec3(-1.0,  1.0, 1.0), vec3(1.0,  1.0, 1.0),\n"
        "        vec3( 1.0, -1.0, 1.0), vec3(-1.0, -1.0, 1.0),\n"
        "        0.4, 0.7);\n"
        "    o = vec4(vec3(s), 1.0);\n"
        "}\n";

    cd::gluon::ModuleResolver resolver;
    cd::shader::CompileDesc desc {};
    desc.source = src;
    desc.stage = cd::shader::ShaderStage::kFragment;
    desc.include_resolver = &resolver;
    const auto r = c->compile(desc);
    ASSERT_TRUE(r.has_value())
        << (r.has_value() ? "" : std::string(r.error().message));
    EXPECT_FALSE(r->spirv.empty());
}

// ---- P0-A wave 1 (ADR-20260613): packing / hash_noise / sampling pins ------

// packing.glsl: octahedral round-trip (cd_oct_decode(cd_oct_encode(n))) +
// 2-channel normal reconstruct + unorm pack — DOMINANT lift from ddgi.
TEST(ShaderLibCompile, PackingRoundTripCompiles)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    const std::string src =
        "#version 450\n"
        "#extension GL_GOOGLE_include_directive : enable\n"
        "#include <cd/gluon/packing.glsl>\n"
        "layout(location = 0) in vec3 v_n;\n"
        "layout(location = 0) out vec4 o;\n"
        "void main()\n"
        "{\n"
        "    vec3 n = normalize(v_n);\n"
        "    vec3 r = cd_oct_decode(cd_oct_encode(n));\n"
        "    vec3 rn = cd_normal_reconstruct_z(n.xy);\n"
        "    float q = cd_unpack_unorm(cd_pack_unorm(n.z * 0.5 + 0.5, 8.0), 8.0);\n"
        "    o = vec4(r * 0.5 + rn * 0.5, q);\n"
        "}\n";

    cd::gluon::ModuleResolver resolver;
    cd::shader::CompileDesc desc {};
    desc.source = src;
    desc.stage = cd::shader::ShaderStage::kFragment;
    desc.include_resolver = &resolver;
    const auto r = c->compile(desc);
    ASSERT_TRUE(r.has_value())
        << (r.has_value() ? "" : std::string(r.error().message));
    EXPECT_FALSE(r->spirv.empty());
}

// hash_noise.glsl: PCG rand (restir DOMINANT) + value-noise/fBm (composite
// DOMINANT) + gradient/curl (SOTA) all callable through the resolver.
TEST(ShaderLibCompile, HashNoiseCompiles)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    const std::string src =
        "#version 450\n"
        "#extension GL_GOOGLE_include_directive : enable\n"
        "#include <cd/gluon/hash_noise.glsl>\n"
        "layout(location = 0) in vec2 v_uv;\n"
        "layout(location = 0) out vec4 o;\n"
        "void main()\n"
        "{\n"
        "    uint s = 12345u;\n"
        "    float r = cd_rand(s);\n"
        "    float vn = cd_value_noise(v_uv);\n"
        "    float fb = cd_fbm4(v_uv);\n"
        "    float gn = cd_gradient_noise(v_uv);\n"
        "    vec2  cn = cd_curl_noise(v_uv);\n"
        "    o = vec4(r, vn, fb, gn + cn.x);\n"
        "}\n";

    cd::gluon::ModuleResolver resolver;
    cd::shader::CompileDesc desc {};
    desc.source = src;
    desc.stage = cd::shader::ShaderStage::kFragment;
    desc.include_resolver = &resolver;
    const auto r = c->compile(desc);
    ASSERT_TRUE(r.has_value())
        << (r.has_value() ? "" : std::string(r.error().message));
    EXPECT_FALSE(r->spirv.empty());
}

// sampling.glsl: Hammersley + GGX importance sample (Karis split-sum) +
// cosine hemisphere + concentric disk — the IBL runtime-eval entry points.
TEST(ShaderLibCompile, SamplingCompiles)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    const std::string src =
        "#version 450\n"
        "#extension GL_GOOGLE_include_directive : enable\n"
        "#include <cd/gluon/sampling.glsl>\n"
        "layout(location = 0) in vec3 v_n;\n"
        "layout(location = 0) out vec4 o;\n"
        "void main()\n"
        "{\n"
        "    vec3 n = normalize(v_n);\n"
        "    vec2 xi = cd_hammersley(7u, 64u);\n"
        "    vec3 h  = cd_importance_sample_ggx(xi, 0.4, n);\n"
        "    vec3 ch = cd_cosine_sample_hemisphere(xi, n);\n"
        "    vec3 sp = cd_uniform_sample_sphere(xi);\n"
        "    vec2 dk = cd_uniform_sample_disk(xi);\n"
        "    o = vec4(h * 0.5 + ch * 0.25 + sp * 0.25, dk.x);\n"
        "}\n";

    cd::gluon::ModuleResolver resolver;
    cd::shader::CompileDesc desc {};
    desc.source = src;
    desc.stage = cd::shader::ShaderStage::kFragment;
    desc.include_resolver = &resolver;
    const auto r = c->compile(desc);
    ASSERT_TRUE(r.has_value())
        << (r.has_value() ? "" : std::string(r.error().message));
    EXPECT_FALSE(r->spirv.empty());
}

// ---- P0-A wave 2 (ADR-20260613): color_space / shadow_filtering / ibl ------

// color_space.glsl: exact sRGB transfer (ColorPicker DOMINANT) + Rec.2020
// (HdrDisplay DOMINANT) + Kelvin→RGB (ColorTemperature DOMINANT) + luminance /
// YCoCg round-trip / exposure (SOTA) all callable through the resolver.
TEST(ShaderLibCompile, ColorSpaceCompiles)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    const std::string src =
        "#version 450\n"
        "#extension GL_GOOGLE_include_directive : enable\n"
        "#include <cd/gluon/color_space.glsl>\n"
        "layout(location = 0) in vec3 v_c;\n"
        "layout(location = 0) out vec4 o;\n"
        "void main()\n"
        "{\n"
        "    vec3 lin = cd_srgb_to_linear3(v_c);\n"
        "    vec3 s   = cd_linear_to_srgb3(lin);\n"
        "    vec3 w   = cd_linear_srgb_to_rec2020(lin);\n"
        "    float y  = cd_luminance(lin) + cd_luminance_601(lin);\n"
        "    vec3 yc  = cd_ycocg_to_rgb(cd_rgb_to_ycocg(lin));\n"
        "    vec3 k   = cd_cct_to_linear_rgb(6500.0);\n"
        "    vec3 e   = cd_exposure(k, 1.0) * cd_ev100_to_exposure(12.0);\n"
        "    o = vec4(s * 0.4 + w * 0.2 + yc * 0.2 + e * 0.2, y);\n"
        "}\n";

    cd::gluon::ModuleResolver resolver;
    cd::shader::CompileDesc desc {};
    desc.source = src;
    desc.stage = cd::shader::ShaderStage::kFragment;
    desc.include_resolver = &resolver;
    const auto r = c->compile(desc);
    ASSERT_TRUE(r.has_value())
        << (r.has_value() ? "" : std::string(r.error().message));
    EXPECT_FALSE(r->spirv.empty());
}

// shadow_filtering.glsl: 3×3 PCF (prim.frag DOMINANT, sampler2D parameter) +
// slope bias + 5×5 / Poisson / cascade-select (SOTA) through the resolver.
TEST(ShaderLibCompile, ShadowFilteringCompiles)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    const std::string src =
        "#version 450\n"
        "#extension GL_GOOGLE_include_directive : enable\n"
        "#include <cd/gluon/shadow_filtering.glsl>\n"
        "layout(set = 0, binding = 0) uniform sampler2D u_shadow;\n"
        "layout(location = 0) in vec4 v_sp;\n"
        "layout(location = 1) in vec3 v_n;\n"
        "layout(location = 2) in vec3 v_l;\n"
        "layout(location = 0) out vec4 o;\n"
        "void main()\n"
        "{\n"
        "    vec3 n = normalize(v_n); vec3 l = normalize(v_l);\n"
        "    float s3 = cd_pcf_shadow_3x3(u_shadow, v_sp, n, l);\n"
        "    float s5 = cd_pcf_shadow_5x5(u_shadow, v_sp, n, l);\n"
        "    float sp = cd_pcf_shadow_poisson(u_shadow, v_sp, n, l, 2.0, 0.7);\n"
        "    int ci = cd_shadow_cascade_select(12.0, vec4(5.0, 15.0, 50.0, 200.0), 4);\n"
        "    o = vec4(s3, s5, sp, float(ci));\n"
        "}\n";

    cd::gluon::ModuleResolver resolver;
    cd::shader::CompileDesc desc {};
    desc.source = src;
    desc.stage = cd::shader::ShaderStage::kFragment;
    desc.include_resolver = &resolver;
    const auto r = c->compile(desc);
    ASSERT_TRUE(r.has_value())
        << (r.has_value() ? "" : std::string(r.error().message));
    EXPECT_FALSE(r->spirv.empty());
}

// ibl_sampling.glsl: analytic split-sum env-BRDF (Karis/Lazarov) + roughness→
// mip (PrefilteredSpecular DOMINANT) + SH9 irradiance (LightProbe DOMINANT)
// — the runtime IBL eval entry points (bake NOT regenerated, §2.6).
TEST(ShaderLibCompile, IblSamplingCompiles)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    const std::string src =
        "#version 450\n"
        "#extension GL_GOOGLE_include_directive : enable\n"
        "#include <cd/gluon/ibl_sampling.glsl>\n"
        "layout(location = 0) in vec3 v_n;\n"
        "layout(location = 1) in vec3 v_v;\n"
        "layout(location = 0) out vec4 o;\n"
        "void main()\n"
        "{\n"
        "    vec3 n = normalize(v_n); vec3 v = normalize(v_v);\n"
        "    float nov = max(dot(n, v), 0.0); float rough = 0.5;\n"
        "    vec2 ab  = cd_env_brdf_approx(rough, nov);\n"
        "    vec3 spec = cd_specular_ibl(vec3(0.04), rough, nov, vec3(0.6));\n"
        "    float mip = cd_roughness_to_mip(rough, 8.0);\n"
        "    vec3 sh[9];\n"
        "    for (int i = 0; i < 9; ++i) sh[i] = vec3(0.1);\n"
        "    vec3 irr = cd_sh9_irradiance(sh, n);\n"
        "    o = vec4(spec + irr + vec3(ab, mip * 0.1), 1.0);\n"
        "}\n";

    cd::gluon::ModuleResolver resolver;
    cd::shader::CompileDesc desc {};
    desc.source = src;
    desc.stage = cd::shader::ShaderStage::kFragment;
    desc.include_resolver = &resolver;
    const auto r = c->compile(desc);
    ASSERT_TRUE(r.has_value())
        << (r.has_value() ? "" : std::string(r.error().message));
    EXPECT_FALSE(r->spirv.empty());
}

// ---- P1 wave 1 (ADR-20260613): brdf_sheen / brdf_clearcoat / brdf_sss ------

// brdf_sheen.glsl: Charlie-D (Estevez-Kulla 2017) + Neubelt-V + inline rim
// lobe, all VERBATIM from cd::brdf::sheen_clearcoat, callable via resolver.
TEST(ShaderLibCompile, BrdfSheenCompiles)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    const std::string src =
        "#version 450\n"
        "#extension GL_GOOGLE_include_directive : enable\n"
        "#include <cd/gluon/brdf_sheen.glsl>\n"
        "layout(location = 0) in vec3 v_n;\n"
        "layout(location = 1) in vec3 v_v;\n"
        "layout(location = 2) in vec3 v_l;\n"
        "layout(location = 0) out vec4 o;\n"
        "void main()\n"
        "{\n"
        "    vec3 n = normalize(v_n); vec3 v = normalize(v_v);\n"
        "    vec3 l = normalize(v_l); vec3 h = normalize(v + l);\n"
        "    float noh = dot(n, h); float nov = dot(n, v); float nol = dot(n, l);\n"
        "    float D = cd_charlie_d(0.4, noh);\n"
        "    float V = cd_v_neubelt(nov, nol);\n"
        "    vec3 rim = cd_sheen_inline_lobe(nov, 0.5);\n"
        "    o = vec4(rim + vec3(D * V), 1.0);\n"
        "}\n";

    cd::gluon::ModuleResolver resolver;
    cd::shader::CompileDesc desc {};
    desc.source = src;
    desc.stage = cd::shader::ShaderStage::kFragment;
    desc.include_resolver = &resolver;
    const auto r = c->compile(desc);
    ASSERT_TRUE(r.has_value())
        << (r.has_value() ? "" : std::string(r.error().message));
    EXPECT_FALSE(r->spirv.empty());
}

// brdf_clearcoat.glsl: Filament clearcoat D*V (0.045 floor) + inline rim
// lobe, VERBATIM from cd::brdf::sheen_clearcoat, callable via resolver.
TEST(ShaderLibCompile, BrdfClearcoatCompiles)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    const std::string src =
        "#version 450\n"
        "#extension GL_GOOGLE_include_directive : enable\n"
        "#include <cd/gluon/brdf_clearcoat.glsl>\n"
        "layout(location = 0) in vec3 v_n;\n"
        "layout(location = 1) in vec3 v_v;\n"
        "layout(location = 2) in vec3 v_l;\n"
        "layout(location = 0) out vec4 o;\n"
        "void main()\n"
        "{\n"
        "    vec3 n = normalize(v_n); vec3 v = normalize(v_v);\n"
        "    vec3 l = normalize(v_l); vec3 h = normalize(v + l);\n"
        "    float noh = dot(n, h); float nov = dot(n, v); float nol = dot(n, l);\n"
        "    float cc = cd_clearcoat_dv(0.1, noh, nov, nol);\n"
        "    vec3 lobe = cd_clearcoat_inline_lobe(vec3(0.6), nov, 0.5);\n"
        "    o = vec4(lobe + vec3(cc), 1.0);\n"
        "}\n";

    cd::gluon::ModuleResolver resolver;
    cd::shader::CompileDesc desc {};
    desc.source = src;
    desc.stage = cd::shader::ShaderStage::kFragment;
    desc.include_resolver = &resolver;
    const auto r = c->compile(desc);
    ASSERT_TRUE(r.has_value())
        << (r.has_value() ? "" : std::string(r.error().message));
    EXPECT_FALSE(r->spirv.empty());
}

// brdf_sss.glsl: Burley wrap-diffusion inline lobe (Burley 2015) VERBATIM
// from cd::brdf::sss, callable via resolver. The separable-blur CS body is
// intentionally NOT migrated (fragment-usable lobe only).
TEST(ShaderLibCompile, BrdfSssCompiles)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    const std::string src =
        "#version 450\n"
        "#extension GL_GOOGLE_include_directive : enable\n"
        "#include <cd/gluon/brdf_sss.glsl>\n"
        "layout(location = 0) in vec3 v_n;\n"
        "layout(location = 1) in vec3 v_l;\n"
        "layout(location = 0) out vec4 o;\n"
        "void main()\n"
        "{\n"
        "    vec3 n = normalize(v_n); vec3 l = normalize(v_l);\n"
        "    vec3 sss = cd_sss_inline_wrap(n, l, 3.0, 0.5);\n"
        "    o = vec4(sss, 1.0);\n"
        "}\n";

    cd::gluon::ModuleResolver resolver;
    cd::shader::CompileDesc desc {};
    desc.source = src;
    desc.stage = cd::shader::ShaderStage::kFragment;
    desc.include_resolver = &resolver;
    const auto r = c->compile(desc);
    ASSERT_TRUE(r.has_value())
        << (r.has_value() ? "" : std::string(r.error().message));
    EXPECT_FALSE(r->spirv.empty());
}

// ---- P1 wave 2 (ADR-20260613): brdf_diffuse_ext / brdf_anisotropy /
//      tonemap_ext / color_grading (SOTA-standard, no engine GLSL copy) ------

// brdf_diffuse_ext.glsl: Oren-Nayar qualitative (1994) + Fujii fast fit,
// both carrying the /PI direct-lighting convention, callable via resolver.
TEST(ShaderLibCompile, BrdfDiffuseExtCompiles)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    const std::string src =
        "#version 450\n"
        "#extension GL_GOOGLE_include_directive : enable\n"
        "#include <cd/gluon/brdf_diffuse_ext.glsl>\n"
        "layout(location = 0) in vec3 v_n;\n"
        "layout(location = 1) in vec3 v_v;\n"
        "layout(location = 2) in vec3 v_l;\n"
        "layout(location = 0) out vec4 o;\n"
        "void main()\n"
        "{\n"
        "    vec3 n = normalize(v_n); vec3 v = normalize(v_v);\n"
        "    vec3 l = normalize(v_l);\n"
        "    float nol = dot(n, l); float nov = dot(n, v); float lov = dot(l, v);\n"
        "    vec3 on = cd_fd_oren_nayar(vec3(0.6), nol, nov, lov, 0.5);\n"
        "    vec3 fj = cd_fd_oren_nayar_fast(vec3(0.6), nol, nov, lov, 0.4);\n"
        "    o = vec4(on * 0.5 + fj * 0.5, 1.0);\n"
        "}\n";

    cd::gluon::ModuleResolver resolver;
    cd::shader::CompileDesc desc {};
    desc.source = src;
    desc.stage = cd::shader::ShaderStage::kFragment;
    desc.include_resolver = &resolver;
    const auto r = c->compile(desc);
    ASSERT_TRUE(r.has_value())
        << (r.has_value() ? "" : std::string(r.error().message));
    EXPECT_FALSE(r->spirv.empty());
}

// brdf_anisotropy.glsl: anisotropic GGX-D (Burley/Kulla) + height-correlated
// anisotropic Smith-V (Heitz), tangent/bitangent parameterised, via resolver.
TEST(ShaderLibCompile, BrdfAnisotropyCompiles)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    const std::string src =
        "#version 450\n"
        "#extension GL_GOOGLE_include_directive : enable\n"
        "#include <cd/gluon/brdf_anisotropy.glsl>\n"
        "layout(location = 0) in vec3 v_n;\n"
        "layout(location = 1) in vec3 v_t;\n"
        "layout(location = 2) in vec3 v_v;\n"
        "layout(location = 3) in vec3 v_l;\n"
        "layout(location = 0) out vec4 o;\n"
        "void main()\n"
        "{\n"
        "    vec3 n = normalize(v_n); vec3 t = normalize(v_t);\n"
        "    vec3 b = cross(n, t);\n"
        "    vec3 v = normalize(v_v); vec3 l = normalize(v_l);\n"
        "    vec3 h = normalize(v + l);\n"
        "    float at = 0.25; float ab = 0.05;\n"
        "    float D = cd_d_ggx_aniso(at, ab, dot(t, h), dot(b, h), dot(n, h));\n"
        "    float V = cd_v_smith_ggx_aniso(at, ab,\n"
        "        dot(t, v), dot(b, v), dot(n, v),\n"
        "        dot(t, l), dot(b, l), dot(n, l));\n"
        "    o = vec4(vec3(D * V), 1.0);\n"
        "}\n";

    cd::gluon::ModuleResolver resolver;
    cd::shader::CompileDesc desc {};
    desc.source = src;
    desc.stage = cd::shader::ShaderStage::kFragment;
    desc.include_resolver = &resolver;
    const auto r = c->compile(desc);
    ASSERT_TRUE(r.has_value())
        << (r.has_value() ? "" : std::string(r.error().message));
    EXPECT_FALSE(r->spirv.empty());
}

// tonemap_ext.glsl: AgX display transform (Sobotka/Blender minimal fit) +
// punchy variant — the prim.frag op=3 "AGX" target, callable via resolver.
TEST(ShaderLibCompile, TonemapExtCompiles)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    const std::string src =
        "#version 450\n"
        "#extension GL_GOOGLE_include_directive : enable\n"
        "#include <cd/gluon/tonemap_ext.glsl>\n"
        "layout(location = 0) in vec3 v_c;\n"
        "layout(location = 0) out vec4 o;\n"
        "void main()\n"
        "{\n"
        "    vec3 agx  = cd_tonemap_agx(v_c);\n"
        "    vec3 agxp = cd_tonemap_agx_punchy(v_c);\n"
        "    o = vec4(agx * 0.5 + agxp * 0.5, 1.0);\n"
        "}\n";

    cd::gluon::ModuleResolver resolver;
    cd::shader::CompileDesc desc {};
    desc.source = src;
    desc.stage = cd::shader::ShaderStage::kFragment;
    desc.include_resolver = &resolver;
    const auto r = c->compile(desc);
    ASSERT_TRUE(r.has_value())
        << (r.has_value() ? "" : std::string(r.error().message));
    EXPECT_FALSE(r->spirv.empty());
}

// color_grading.glsl: lift-gamma-gain + pivot contrast + luminance-centred
// saturation (LOCAL cd_cg_luminance, NO color_space include) + CAT02
// white-balance + channel-mixer, all callable via resolver.
TEST(ShaderLibCompile, ColorGradingCompiles)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    const std::string src =
        "#version 450\n"
        "#extension GL_GOOGLE_include_directive : enable\n"
        "#include <cd/gluon/color_grading.glsl>\n"
        "layout(location = 0) in vec3 v_c;\n"
        "layout(location = 0) out vec4 o;\n"
        "void main()\n"
        "{\n"
        "    vec3 g = cd_lift_gamma_gain(v_c, vec3(0.02), vec3(1.1), vec3(1.05));\n"
        "    g = cd_contrast(g, 1.2, 0.18);\n"
        "    g = cd_saturation(g, 1.15);\n"
        "    g = cd_white_balance(g, 0.2, -0.1);\n"
        "    g = cd_channel_mixer(g, vec3(1.0, 0.0, 0.0),\n"
        "                            vec3(0.0, 1.0, 0.0),\n"
        "                            vec3(0.0, 0.0, 1.0));\n"
        "    o = vec4(g, 1.0);\n"
        "}\n";

    cd::gluon::ModuleResolver resolver;
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

using TestDomain = cd::gluon::VariantDomain<
    cd::gluon::BoolDim<"DIR_LIGHT">,
    cd::gluon::BoolDim<"SHADOW_RECV">,
    cd::gluon::EnumDim<"TONEMAP", 4>>;

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

// ---- BAND-1 untested-module entry-point coverage (ADR-20260616 §2.7) -------
//
// Four modules had no DEDICATED entry-point test (only the indirect
// EveryModuleCompilesStandalone pass): cone_atten, cotangent_frame,
// light_atten, ltc_polygon. Pin each entry point by calling it through the
// resolver so a signature regression fails here, not silently downstream.

// light_atten.glsl: Frostbite windowed inverse-square distance attenuation.
TEST(ShaderLibCompile, LightAttenCompiles)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    const std::string src =
        "#version 450\n"
        "#extension GL_GOOGLE_include_directive : enable\n"
        "#include <cd/gluon/light_atten.glsl>\n"
        "layout(location = 0) in vec3 v_p;\n"
        "layout(location = 0) out vec4 o;\n"
        "void main()\n"
        "{\n"
        "    float d = length(v_p);\n"
        "    float a = distance_atten(d, 25.0);\n"
        "    o = vec4(vec3(a), 1.0);\n"
        "}\n";

    cd::gluon::ModuleResolver resolver;
    cd::shader::CompileDesc desc {};
    desc.source = src;
    desc.stage = cd::shader::ShaderStage::kFragment;
    desc.include_resolver = &resolver;
    const auto r = c->compile(desc);
    ASSERT_TRUE(r.has_value())
        << (r.has_value() ? "" : std::string(r.error().message));
    EXPECT_FALSE(r->spirv.empty());
}

// cone_atten.glsl: spot-cone falloff (t^2).
TEST(ShaderLibCompile, ConeAttenCompiles)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    const std::string src =
        "#version 450\n"
        "#extension GL_GOOGLE_include_directive : enable\n"
        "#include <cd/gluon/cone_atten.glsl>\n"
        "layout(location = 0) in vec3 v_l;\n"
        "layout(location = 0) out vec4 o;\n"
        "void main()\n"
        "{\n"
        "    vec3 l = normalize(v_l);\n"
        "    vec3 spot = vec3(0.0, 0.0, -1.0);\n"
        "    float cos_in  = cos(radians(20.0));\n"
        "    float cos_out = cos(radians(30.0));\n"
        "    float a = cone_atten(dot(-l, spot), cos_in, cos_out);\n"
        "    o = vec4(vec3(a), 1.0);\n"
        "}\n";

    cd::gluon::ModuleResolver resolver;
    cd::shader::CompileDesc desc {};
    desc.source = src;
    desc.stage = cd::shader::ShaderStage::kFragment;
    desc.include_resolver = &resolver;
    const auto r = c->compile(desc);
    ASSERT_TRUE(r.has_value())
        << (r.has_value() ? "" : std::string(r.error().message));
    EXPECT_FALSE(r->spirv.empty());
}

// cotangent_frame.glsl: derivative-based TBN (Mikkelsen 2010).
TEST(ShaderLibCompile, CotangentFrameCompiles)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    const std::string src =
        "#version 450\n"
        "#extension GL_GOOGLE_include_directive : enable\n"
        "#include <cd/gluon/cotangent_frame.glsl>\n"
        "layout(location = 0) in vec3 v_p;\n"
        "layout(location = 1) in vec3 v_n;\n"
        "layout(location = 2) in vec2 v_uv;\n"
        "layout(location = 0) out vec4 o;\n"
        "void main()\n"
        "{\n"
        "    vec3 n = normalize(v_n);\n"
        "    mat3 tbn = cotangent_frame(n, v_p, v_uv);\n"
        "    vec3 mapped = normalize(tbn * vec3(0.0, 0.0, 1.0));\n"
        "    o = vec4(mapped * 0.5 + 0.5, 1.0);\n"
        "}\n";

    cd::gluon::ModuleResolver resolver;
    cd::shader::CompileDesc desc {};
    desc.source = src;
    desc.stage = cd::shader::ShaderStage::kFragment;
    desc.include_resolver = &resolver;
    const auto r = c->compile(desc);
    ASSERT_TRUE(r.has_value())
        << (r.has_value() ? "" : std::string(r.error().message));
    EXPECT_FALSE(r->spirv.empty());
}

// ltc_polygon.glsl: LTC Lambert form-factor (atan2 edge integral).
TEST(ShaderLibCompile, LtcPolygonCompiles)
{
    auto c = cd::shader::make_glslang_compiler();
    if (c == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    const std::string src =
        "#version 450\n"
        "#extension GL_GOOGLE_include_directive : enable\n"
        "#include <cd/gluon/ltc_polygon.glsl>\n"
        "layout(location = 0) in vec3 v_n;\n"
        "layout(location = 0) out vec4 o;\n"
        "void main()\n"
        "{\n"
        "    vec3 n = normalize(v_n);\n"
        "    float d = cd_ltc_polygon_irradiance(n,\n"
        "        vec3(-1.0,  1.0, 1.0), vec3(1.0,  1.0, 1.0),\n"
        "        vec3( 1.0, -1.0, 1.0), vec3(-1.0, -1.0, 1.0));\n"
        "    o = vec4(vec3(d), 1.0);\n"
        "}\n";

    cd::gluon::ModuleResolver resolver;
    cd::shader::CompileDesc desc {};
    desc.source = src;
    desc.stage = cd::shader::ShaderStage::kFragment;
    desc.include_resolver = &resolver;
    const auto r = c->compile(desc);
    ASSERT_TRUE(r.has_value())
        << (r.has_value() ? "" : std::string(r.error().message));
    EXPECT_FALSE(r->spirv.empty());
}

// ---- BAND-1 include-closure cache-key test (ADR-20260616 §2.7) -------------
//
// The CENTRAL gluon contract (ADR-20260612 §2.3): editing a shared module
// must invalidate every cached root that includes it. We drive a CachedCompiler
// with a MUTABLE in-memory resolver so we can edit a shared module's content
// between compiles and observe the cache hit/miss transitions.

namespace
{
// A resolver whose shared module ("cd/gluon/_band1_shared.glsl") content can
// be edited at runtime, plus a dependent module ("cd/gluon/_band1_dep.glsl")
// that #includes the shared one, and an unrelated module.
class MutableResolver final : public cd::shader::IIncludeResolver
{
public:
    std::string shared_content {
        "#ifndef CD_BAND1_SHARED\n#define CD_BAND1_SHARED\n"
        "float band1_shared() { return 1.0; }\n#endif\n" };

    [[nodiscard]] std::optional<Resolved> resolve(
        std::string_view requested,
        std::string_view /*requester*/,
        bool /*system_include*/) override
    {
        if (requested == "cd/gluon/_band1_shared.glsl")
            return Resolved { std::string { requested }, shared_content };
        if (requested == "cd/gluon/_band1_dep.glsl")
            return Resolved {
                std::string { requested },
                "#ifndef CD_BAND1_DEP\n#define CD_BAND1_DEP\n"
                "#include <cd/gluon/_band1_shared.glsl>\n"
                "float band1_dep() { return band1_shared() + 1.0; }\n#endif\n" };
        if (requested == "cd/gluon/_band1_indep.glsl")
            return Resolved {
                std::string { requested },
                "#ifndef CD_BAND1_INDEP\n#define CD_BAND1_INDEP\n"
                "float band1_indep() { return 2.0; }\n#endif\n" };
        return std::nullopt;
    }
};

[[nodiscard]] std::filesystem::path make_unique_cache_dir()
{
    static std::atomic<std::uint64_t> seq { 0 };
    const auto n  = seq.fetch_add(1, std::memory_order_relaxed);
    const auto ts = std::chrono::steady_clock::now().time_since_epoch().count();
    auto dir = std::filesystem::temp_directory_path() /
               ("cd_gluon_closure_cache_" + std::to_string(n) + "_" +
                std::to_string(ts));
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    return dir;
}

[[nodiscard]] std::string dependent_root_src()
{
    return "#version 450\n"
           "#extension GL_GOOGLE_include_directive : enable\n"
           "#include <cd/gluon/_band1_dep.glsl>\n"
           "layout(location = 0) out vec4 o;\n"
           "void main() { o = vec4(band1_dep()); }\n";
}
}  // namespace

TEST(ShaderLibClosureCache, EditingSharedModuleInvalidatesDependent)
{
    auto inner = cd::shader::make_glslang_compiler();
    if (inner == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    const auto cache_dir = make_unique_cache_dir();
    cd::shader::CachedCompiler cached(*inner, cache_dir);
    MutableResolver resolver;

    auto compile_dep = [&]() {
        const std::string src = dependent_root_src();
        cd::shader::CompileDesc desc {};
        desc.source           = src;
        desc.stage            = cd::shader::ShaderStage::kFragment;
        desc.source_name      = "band1_dep_root.frag";
        desc.include_resolver = &resolver;
        return cached.compile(desc);
    };

    // 1) First compile: cache MISS + write.
    {
        const auto r = compile_dep();
        ASSERT_TRUE(r.has_value())
            << (r.has_value() ? "" : std::string(r.error().message));
        EXPECT_FALSE(r->spirv.empty());
    }
    EXPECT_EQ(cached.stats().misses, 1u);
    EXPECT_EQ(cached.stats().hits,   0u);
    EXPECT_EQ(cached.stats().writes, 1u);

    // 2) Recompile with the SAME shared content: cache HIT (no new write).
    {
        const auto r = compile_dep();
        ASSERT_TRUE(r.has_value());
    }
    EXPECT_EQ(cached.stats().misses, 1u);
    EXPECT_EQ(cached.stats().hits,   1u);
    EXPECT_EQ(cached.stats().writes, 1u);

    // 3) EDIT the shared module's content. The dependent root's SOURCE text
    //    is byte-identical, but its include-CLOSURE changed -> the closure
    //    hash changes -> the cache key changes -> this MUST be a fresh MISS
    //    (a stale hit here would be the exact bug closure-hashing prevents).
    resolver.shared_content =
        "#ifndef CD_BAND1_SHARED\n#define CD_BAND1_SHARED\n"
        "float band1_shared() { return 42.0; }\n#endif\n";  // changed body
    {
        const auto r = compile_dep();
        ASSERT_TRUE(r.has_value());
    }
    EXPECT_EQ(cached.stats().misses, 2u) << "edited shared module must miss";
    EXPECT_EQ(cached.stats().hits,   1u);
    EXPECT_EQ(cached.stats().writes, 2u);

    // 4) Recompile again after the edit: HIT (the new closure is now cached).
    {
        const auto r = compile_dep();
        ASSERT_TRUE(r.has_value());
    }
    EXPECT_EQ(cached.stats().misses, 2u);
    EXPECT_EQ(cached.stats().hits,   2u);
}

TEST(ShaderLibClosureCache, EditingSharedModuleDoesNotInvalidateUnrelatedRoot)
{
    auto inner = cd::shader::make_glslang_compiler();
    if (inner == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    const auto cache_dir = make_unique_cache_dir();
    cd::shader::CachedCompiler cached(*inner, cache_dir);
    MutableResolver resolver;

    // An INDEPENDENT root that includes only _band1_indep (NOT the shared one).
    auto compile_indep = [&]() {
        const std::string src =
            "#version 450\n"
            "#extension GL_GOOGLE_include_directive : enable\n"
            "#include <cd/gluon/_band1_indep.glsl>\n"
            "layout(location = 0) out vec4 o;\n"
            "void main() { o = vec4(band1_indep()); }\n";
        cd::shader::CompileDesc desc {};
        desc.source           = src;
        desc.stage            = cd::shader::ShaderStage::kFragment;
        desc.source_name      = "band1_indep_root.frag";
        desc.include_resolver = &resolver;
        return cached.compile(desc);
    };

    ASSERT_TRUE(compile_indep().has_value());
    EXPECT_EQ(cached.stats().misses, 1u);

    // Editing the SHARED module must NOT affect the independent root's key.
    resolver.shared_content =
        "#ifndef CD_BAND1_SHARED\n#define CD_BAND1_SHARED\n"
        "float band1_shared() { return 7.0; }\n#endif\n";
    ASSERT_TRUE(compile_indep().has_value());
    EXPECT_EQ(cached.stats().misses, 1u) << "unrelated root must stay cached";
    EXPECT_EQ(cached.stats().hits,   1u);
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
    src += "#include <cd/gluon/tonemap.glsl>\n"
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

    cd::gluon::ModuleResolver resolver;
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
