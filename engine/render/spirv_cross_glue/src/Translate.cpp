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
        const auto sm =
            static_cast<std::uint32_t>((version == 0U) ? kDefaultHlslVersion : version);
        hlsl_opts.shader_model = sm;
        compiler.set_hlsl_options(hlsl_opts);

        // ---- D3D12 binding model: space-per-set + explicit push_constant remap
        // (ADR-20260614-d3d12-binding-model). On SM>=51 SPIRV-Cross already
        // emits resource bindings as `register(<class>M, spaceN)` where N is the
        // Vulkan descriptor-set index and M the binding index — matching the
        // D3D12 root signature's per-set table layout (space N). So NO resource
        // remap is needed here.
        //
        // The push_constant block, however, is emitted register-LESS by default
        // (its desc_set == ResourceBindingPushConstantDescriptorSet == ~0u), so
        // DXC auto-assigns it b0/space0 — which both collides with the set-0 CBV
        // b0 and misses the root signature's root-constant slot at b0/space1.
        // Force it onto b0/space1 via a RootConstants layout that spans the
        // declared push_constant struct size (the same byte range the D3D12 root
        // signature derives for its 32-bit-constants slot).
        if (sm >= 51U)
        {
            const spirv_cross::ShaderResources res = compiler.get_shader_resources();
            if (!res.push_constant_buffers.empty())
            {
                const spirv_cross::Resource& pc = res.push_constant_buffers.front();
                const std::size_t struct_size =
                    compiler.get_declared_struct_size(compiler.get_type(pc.base_type_id));
                // RootConstants byte range must be a multiple of 4 (SPIRV-Cross
                // contract). push_constant blocks are always 4-byte aligned, but
                // round up defensively so a partial trailing word is covered.
                const std::uint32_t end =
                    (static_cast<std::uint32_t>(struct_size) + 3U) & ~3U;
                if (end > 0U)
                {
                    std::vector<spirv_cross::RootConstants> rc(1);
                    rc[0].start   = 0U;
                    rc[0].end     = end;
                    rc[0].binding = 0U;   // b0
                    rc[0].space   = 1U;   // space1 (matches root sig 32-bit-constants slot)
                    compiler.set_root_constant_layouts(std::move(rc));
                }
            }
        }

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
