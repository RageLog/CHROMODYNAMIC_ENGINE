// =============================================================================
// CHROMODYNAMIC — cd/shader/GlslangCompiler.cpp
//
// glslang-backed implementation of `cd::shader::ICompiler`. Built only when
// CD_ENABLE_GLSLANG=ON (the default). When disabled,
// `make_glslang_compiler()` returns nullptr from a fallback TU so callers
// linking cd::shader still compile.
// =============================================================================
#include <cd/shader/Compiler.hpp>

#include <atomic>
#include <memory>
#include <string>

// glslang is vendor C++ code that does not pass our strict warning set
// cleanly; mark its headers SYSTEM via the CMake helper and silence the
// known offenders here. Inclusion order matters: ResourceLimits ships its
// own header in newer releases.
#if defined(__clang__) || defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wold-style-cast"
    #pragma GCC diagnostic ignored "-Wshadow"
    #pragma GCC diagnostic ignored "-Wsign-conversion"
    #pragma GCC diagnostic ignored "-Wconversion"
    #pragma GCC diagnostic ignored "-Wmissing-declarations"
#endif
#if defined(_MSC_VER)
    #pragma warning(push, 0)
#endif
#include <SPIRV/GlslangToSpv.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#if defined(_MSC_VER)
    #pragma warning(pop)
#endif
#if defined(__clang__) || defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

namespace cd::shader
{

namespace
{

// glslang::InitializeProcess / FinalizeProcess are process-wide and must be
// called exactly once. We refcount via an atomic so multiple compiler
// instances share a single boot.
std::atomic<int>& glslang_refcount() noexcept
{
    static std::atomic<int> rc { 0 };
    return rc;
}

void glslang_boot()
{
    if (glslang_refcount().fetch_add(1, std::memory_order_acq_rel) == 0)
    {
        glslang::InitializeProcess();
    }
}

void glslang_shutdown()
{
    if (glslang_refcount().fetch_sub(1, std::memory_order_acq_rel) == 1)
    {
        glslang::FinalizeProcess();
    }
}

[[nodiscard]] EShLanguage to_eshlang(ShaderStage s) noexcept
{
    switch (s)
    {
        case ShaderStage::kVertex:
            return EShLangVertex;
        case ShaderStage::kFragment:
            return EShLangFragment;
        case ShaderStage::kCompute:
            return EShLangCompute;
        case ShaderStage::kGeometry:
            return EShLangGeometry;
        case ShaderStage::kTessControl:
            return EShLangTessControl;
        case ShaderStage::kTessEval:
            return EShLangTessEvaluation;
        // Phase 136 — RT pipeline stages.
        case ShaderStage::kRaygen:
            return EShLangRayGen;
        case ShaderStage::kMiss:
            return EShLangMiss;
        case ShaderStage::kClosestHit:
            return EShLangClosestHit;
        case ShaderStage::kAnyHit:
            return EShLangAnyHit;
        case ShaderStage::kIntersection:
            return EShLangIntersect;
        case ShaderStage::kCallable:
            return EShLangCallable;
        // Phase 765 W2A — F5 — mesh-shader stages (glslang ≥ 11 ships
        // EShLangMesh / EShLangTask; older glslang versions are not
        // supported by the engine baseline).
        case ShaderStage::kMesh:
            return EShLangMesh;
        case ShaderStage::kTask:
            return EShLangTask;
    }
    return EShLangVertex;
}

[[nodiscard]] glslang::EShTargetClientVersion target_vulkan(TargetEnv t) noexcept
{
    switch (t)
    {
        case TargetEnv::kVulkan12:
            return glslang::EShTargetVulkan_1_2;
        case TargetEnv::kVulkan13:
            return glslang::EShTargetVulkan_1_3;
    }
    return glslang::EShTargetVulkan_1_3;
}

[[nodiscard]] glslang::EShTargetLanguageVersion target_spirv(TargetEnv t) noexcept
{
    // Vulkan 1.2 → SPIR-V 1.5, Vulkan 1.3 → SPIR-V 1.6 (per Vulkan spec table).
    switch (t)
    {
        case TargetEnv::kVulkan12:
            return glslang::EShTargetSpv_1_5;
        case TargetEnv::kVulkan13:
            return glslang::EShTargetSpv_1_6;
    }
    return glslang::EShTargetSpv_1_6;
}

/// phase1132 (SL-C step 1): glslang Includer bridging IIncludeResolver.
/// Depth-capped at 16 per ADR-20260612-shader-library-architecture §2.2
/// (cycles bounce off the cap; modules also carry include guards).
class ResolverIncluder final : public glslang::TShader::Includer
{
public:
    explicit ResolverIncluder(IIncludeResolver& resolver) noexcept
        : resolver_ { &resolver }
    {
    }

    IncludeResult* includeSystem(const char* header_name,
                                 const char* includer_name,
                                 std::size_t inclusion_depth) override
    {
        return resolve_request(header_name, includer_name, inclusion_depth, true);
    }

    IncludeResult* includeLocal(const char* header_name,
                                const char* includer_name,
                                std::size_t inclusion_depth) override
    {
        return resolve_request(header_name, includer_name, inclusion_depth, false);
    }

    void releaseInclude(IncludeResult* result) override
    {
        if (result != nullptr)
        {
            // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) glslang's
            // Includer contract is raw new/delete across the callback pair.
            delete static_cast<Payload*>(result->userData);
            // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
            delete result;
        }
    }

private:
    struct Payload
    {
        std::string path;
        std::string content;
    };

    static constexpr std::size_t kMaxIncludeDepth = 16;

    IncludeResult* resolve_request(const char* header_name,
                                   const char* includer_name,
                                   std::size_t inclusion_depth,
                                   bool system_include)
    {
        if (header_name == nullptr || inclusion_depth > kMaxIncludeDepth)
            return nullptr;  // glslang surfaces this as a compile error
        auto r = resolver_->resolve(
            header_name,
            includer_name != nullptr ? std::string_view { includer_name }
                                     : std::string_view {},
            system_include);
        if (!r.has_value())
            return nullptr;
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) see releaseInclude.
        auto* payload = new Payload { std::move(r->virtual_path),
                                      std::move(r->content) };
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        return new IncludeResult(payload->path, payload->content.data(),
                                 payload->content.size(), payload);
    }

    IIncludeResolver* resolver_;
};

class GlslangCompiler final : public ICompiler
{
public:
    GlslangCompiler() noexcept
    {
        glslang_boot();
    }

    ~GlslangCompiler() override
    {
        glslang_shutdown();
    }

    GlslangCompiler(const GlslangCompiler&) = delete;
    GlslangCompiler& operator=(const GlslangCompiler&) = delete;
    GlslangCompiler(GlslangCompiler&&) = delete;
    GlslangCompiler& operator=(GlslangCompiler&&) = delete;

    [[nodiscard]] cd::core::Result<CompileResult> compile(const CompileDesc& desc) override
    {
        if (desc.source.empty())
        {
            return std::unexpected(shader_errors::make(shader_errors::Code::kInvalidArgument, "empty shader source"));
        }

        const EShLanguage stage = to_eshlang(desc.stage);
        glslang::TShader shader { stage };

        // glslang stores raw pointers; the underlying strings must outlive
        // shader.parse(). All inputs are bound via desc which we treat as live
        // for the duration of this call.
        const std::string src_str { desc.source };
        const std::string name_str { desc.source_name };
        const std::string entry_str { desc.entry_point };
        const char* sources[] = { src_str.c_str() };
        const int lengths[] = { static_cast<int>(src_str.size()) };
        const char* names[] = { name_str.c_str() };
        shader.setStringsWithLengthsAndNames(sources, lengths, names, 1);
        shader.setEntryPoint(entry_str.c_str());
        shader.setSourceEntryPoint(entry_str.c_str());

        const auto vk_ver = target_vulkan(desc.target);
        const auto spv_ver = target_spirv(desc.target);

        if (desc.lang == ShaderLanguage::kHlsl)
        {
            shader.setEnvInput(glslang::EShSourceHlsl, stage, glslang::EShClientVulkan, 100);
        }
        else
        {
            shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
        }
        shader.setEnvClient(glslang::EShClientVulkan, vk_ver);
        shader.setEnvTarget(glslang::EShTargetSpv, spv_ver);

        // Default DefaultTBuiltInResource ships with glslang; covers the
        // typical clamp limits (uniforms, attributes, …) that hand-rolled
        // shaders never exceed. Engines that ship many shaders typically wrap
        // this with device-reported limits — we leave that to a future sprint.
        auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
        if (desc.lang == ShaderLanguage::kHlsl)
        {
            messages = static_cast<EShMessages>(messages | EShMsgReadHlsl);
        }
        if (desc.generate_debug_info)
        {
            messages = static_cast<EShMessages>(messages | EShMsgDebugInfo);
        }

        const TBuiltInResource* resources = GetDefaultResources();
        constexpr int kDefaultVersion = 450;  // GLSL #version when none is specified.
        // phase1132 (SL-C step 1): when the caller supplies an include
        // resolver, parse through the bridging includer so module
        // #includes resolve; the null path keeps the EXACT legacy
        // overload (includes are errors) — zero behaviour drift.
        bool parse_ok = false;
        if (desc.include_resolver != nullptr)
        {
            ResolverIncluder includer { *desc.include_resolver };
            parse_ok = shader.parse(
                resources, kDefaultVersion, /*forwardCompatible=*/false,
                messages, includer);
        }
        else
        {
            parse_ok = shader.parse(
                resources, kDefaultVersion, /*forwardCompatible=*/false,
                messages);
        }
        if (!parse_ok)
        {
            std::string msg = "glslang parse: ";
            msg += shader.getInfoLog();
            return std::unexpected(shader_errors::make_owning(shader_errors::Code::kCompileFailed, std::move(msg)));
        }

        glslang::TProgram program;
        program.addShader(&shader);
        if (!program.link(messages))
        {
            std::string msg = "glslang link: ";
            msg += program.getInfoLog();
            return std::unexpected(shader_errors::make_owning(shader_errors::Code::kLinkFailed, std::move(msg)));
        }

        CompileResult out;
        out.warnings = shader.getInfoLog();
        glslang::SpvOptions spv_options {};
        spv_options.generateDebugInfo = desc.generate_debug_info;
        spv_options.stripDebugInfo = !desc.generate_debug_info;
        spv_options.disableOptimizer = true;  // ENABLE_OPT=OFF in our glslang build
        spv_options.optimizeSize = false;
        spv_options.validate = true;

        const glslang::TIntermediate* inter = program.getIntermediate(stage);
        if (inter == nullptr)
        {
            return std::unexpected(shader_errors::make(shader_errors::Code::kLinkFailed, "glslang: no intermediate"));
        }
        spv::SpvBuildLogger logger;
        glslang::GlslangToSpv(*inter, out.spirv, &logger, &spv_options);
        if (out.spirv.empty())
        {
            std::string msg = "glslang SPIR-V emission empty: ";
            msg += logger.getAllMessages();
            return std::unexpected(shader_errors::make_owning(shader_errors::Code::kLinkFailed, std::move(msg)));
        }
        return out;
    }
};

}  // namespace

std::unique_ptr<ICompiler> make_glslang_compiler()
{
    return std::make_unique<GlslangCompiler>();
}

}  // namespace cd::shader
