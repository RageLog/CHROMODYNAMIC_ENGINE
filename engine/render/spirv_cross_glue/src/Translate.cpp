// =============================================================================
// CHROMODYNAMIC — cd/spirv_cross_glue/Translate.cpp
// B-infra1 — SPIRV-Cross C++ API bridge.
//
// All three backends (GLSL, HLSL, MSL) are compiled unconditionally — the
// CMakeLists.txt already gates SPIRV_CROSS_ENABLE_{GLSL,HLSL,MSL}=ON and
// links the corresponding static libraries.
//
// Exception policy: SPIRV-Cross throws `spirv_cross::CompilerError` (a
// std::runtime_error subclass) for malformed input. We catch at the API
// boundary and convert to TranslateResult::error so callers never need an
// exception-aware call stack.
// =============================================================================
#include <cd/spirv_cross_glue/Translate.hpp>

// SPIRV-Cross is vendored/FetchContent; suppress its diagnostics so they
// don't trip the engine's -Wall -Werror build.
#if defined(_MSC_VER)
    #pragma warning(push, 0)
#endif
#if defined(__clang__) || defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wold-style-cast"
    #pragma GCC diagnostic ignored "-Wshadow"
    #pragma GCC diagnostic ignored "-Wsign-conversion"
    #pragma GCC diagnostic ignored "-Wconversion"
    #pragma GCC diagnostic ignored "-Wmissing-declarations"
    #pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <spirv_glsl.hpp>
#include <spirv_hlsl.hpp>
#include <spirv_msl.hpp>

#if defined(_MSC_VER)
    #pragma warning(pop)
#endif
#if defined(__clang__) || defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

#include <stdexcept>
#include <vector>

namespace cd::spirv_cross_glue
{

namespace
{

// Auto-pick version constants (version == 0 path).
constexpr std::uint32_t kDefaultGlslVersion = 450U;    // GLSL 4.50
constexpr std::uint32_t kDefaultHlslVersion = 60U;     // HLSL SM 6.0
constexpr std::uint32_t kDefaultMslVersion  = 20200U;  // MSL 2.2

[[nodiscard]] TranslateResult translate_glsl(
    const std::vector<std::uint32_t>& words,
    std::uint32_t                      version)
{
    try
    {
        spirv_cross::CompilerGLSL compiler { words };
        spirv_cross::CompilerGLSL::Options opts {};
        opts.version = (version == 0U) ? kDefaultGlslVersion : version;
        opts.es = false;
        opts.vulkan_semantics = false;  // emit GL-compatible GLSL, not Vulkan-GLSL
        compiler.set_common_options(opts);
        return TranslateResult { compiler.compile(), {} };
    }
    catch (const std::exception& ex)
    {
        return TranslateResult { {}, std::string("spirv-cross GLSL: ") + ex.what() };
    }
}

[[nodiscard]] TranslateResult translate_hlsl(
    const std::vector<std::uint32_t>& words,
    std::uint32_t                      version)
{
    try
    {
        spirv_cross::CompilerHLSL compiler { words };
        spirv_cross::CompilerHLSL::Options hlsl_opts {};
        // shader_model is expressed as major*10 + minor in SPIRV-Cross options.
        // Default: SM 6.0 → 60. If caller passes e.g. 51 → SM 5.1.
        hlsl_opts.shader_model = static_cast<std::uint32_t>((version == 0U) ? kDefaultHlslVersion : version);
        compiler.set_hlsl_options(hlsl_opts);
        return TranslateResult { compiler.compile(), {} };
    }
    catch (const std::exception& ex)
    {
        return TranslateResult { {}, std::string("spirv-cross HLSL: ") + ex.what() };
    }
}

[[nodiscard]] TranslateResult translate_msl(
    const std::vector<std::uint32_t>& words,
    std::uint32_t                      version)
{
    try
    {
        spirv_cross::CompilerMSL compiler { words };
        spirv_cross::CompilerMSL::Options msl_opts {};
        // msl_version is a packed (major*10000 + minor*100) integer.
        // Default: MSL 2.2 → 20200.
        msl_opts.msl_version = (version == 0U) ? kDefaultMslVersion : version;
        compiler.set_msl_options(msl_opts);
        return TranslateResult { compiler.compile(), {} };
    }
    catch (const std::exception& ex)
    {
        return TranslateResult { {}, std::string("spirv-cross MSL: ") + ex.what() };
    }
}

}  // namespace

TranslateResult translate(
    std::span<const std::uint32_t> spirv,
    Target                         target,
    std::uint32_t                  version)
{
    if (spirv.empty())
    {
        return TranslateResult { {}, "spirv-cross: empty SPIR-V input" };
    }

    // spirv_cross constructors accept std::vector<uint32_t>; copy once.
    const std::vector<std::uint32_t> words { spirv.begin(), spirv.end() };

    switch (target)
    {
        case Target::kGlsl:
            return translate_glsl(words, version);
        case Target::kHlsl:
            return translate_hlsl(words, version);
        case Target::kMsl:
            return translate_msl(words, version);
    }

    // Unreachable: switch is exhaustive over the enum.
    return TranslateResult { {}, "spirv-cross: unknown target" };
}

}  // namespace cd::spirv_cross_glue
