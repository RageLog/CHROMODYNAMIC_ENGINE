// =============================================================================
// CHROMODYNAMIC — engine/render/rhi_d3d12/src/D3D12ShaderCompile.cpp
// =============================================================================
#include <cd/rhi_d3d12/D3D12ShaderCompile.hpp>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #if defined(__clang__)
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wlanguage-extension-token"
    #endif
    #include <windows.h>
    #include <wrl/client.h>
    #include <d3dcompiler.h>
    #if defined(__clang__)
        #pragma clang diagnostic pop
    #endif
#endif

#include <cstring>
#include <string>

namespace cd::rhi_d3d12
{

#if defined(_WIN32)

namespace
{

using Microsoft::WRL::ComPtr;

[[nodiscard]] const char* target_for(cd::rhi::ShaderStage stage) noexcept
{
    using S = cd::rhi::ShaderStage;
    switch (stage)
    {
        case S::kVertex:   return "vs_5_1";
        case S::kFragment: return "ps_5_1";
        case S::kCompute:  return "cs_5_1";
        case S::kGeometry: return "gs_5_1";
        case S::kTessControl: return "hs_5_1";
        case S::kTessEval:    return "ds_5_1";
        default: return nullptr;
    }
}

}  // namespace

cd::core::Result<std::vector<std::uint8_t>> compile_hlsl(const CompileOptions& opts)
{
    const char* target = target_for(opts.stage);
    if (target == nullptr)
    {
        return std::unexpected(shader_errors::make(
            shader_errors::Code::kUnsupportedStage,
            "ShaderStage has no Shader Model 5 target (use DXC for SM6+)"));
    }

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
    switch (opts.optimization_level)
    {
        case 0:  flags |= D3DCOMPILE_OPTIMIZATION_LEVEL0; break;
        case 1:  flags |= D3DCOMPILE_OPTIMIZATION_LEVEL1; break;
        case 2:  flags |= D3DCOMPILE_OPTIMIZATION_LEVEL2; break;
        default: flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3; break;
    }
    if (opts.optimization_level == 0)
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;

    // Caller's string_views aren't necessarily null-terminated.
    const std::string entry { opts.entry_point };
    const std::string source_name { opts.source_name };

    ComPtr<ID3DBlob> code_blob;
    ComPtr<ID3DBlob> err_blob;
    HRESULT hr = D3DCompile2(
        opts.source.data(),
        opts.source.size(),
        source_name.c_str(),
        nullptr,                       // pDefines
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entry.c_str(),
        target,
        flags, 0,
        0, nullptr, 0,
        &code_blob,
        &err_blob);

    if (FAILED(hr))
    {
        std::string msg = "D3DCompile2 failed (HRESULT 0x";
        char hbuf[16] {};
        std::snprintf(hbuf, sizeof(hbuf), "%08lx", static_cast<unsigned long>(hr));
        msg.append(hbuf);
        msg.append("): ");
        if (err_blob && err_blob->GetBufferSize() > 0)
        {
            msg.append(static_cast<const char*>(err_blob->GetBufferPointer()),
                       err_blob->GetBufferSize());
        }
        else
        {
            msg.append("<no diagnostics from compiler>");
        }
        return std::unexpected(shader_errors::make(
            shader_errors::Code::kCompileFailed, msg));
    }

    std::vector<std::uint8_t> bytecode(code_blob->GetBufferSize());
    std::memcpy(bytecode.data(), code_blob->GetBufferPointer(), bytecode.size());
    return bytecode;
}

#else  // _WIN32

cd::core::Result<std::vector<std::uint8_t>> compile_hlsl(const CompileOptions&)
{
    return std::unexpected(shader_errors::make(
        shader_errors::Code::kBackendUnavailable,
        "D3D12 shader compile only available on Windows"));
}

#endif  // _WIN32

}  // namespace cd::rhi_d3d12
