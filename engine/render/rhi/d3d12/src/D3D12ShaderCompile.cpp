// =============================================================================
// CHROMODYNAMIC — engine/render/rhi_d3d12/src/D3D12ShaderCompile.cpp
// =============================================================================
#include <cd/rhi/d3d12/D3D12ShaderCompile.hpp>

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
    #include <dxcapi.h>  // DXC IDxcCompiler3 (Phase 15.C)
    // NOTE: pragma stays pushed for the rest of this TU because
    // IID_PPV_ARGS (which expands to __uuidof — a MS-language
    // extension) is used in the function bodies below. The matching
    // pop sits at the very bottom of the file.
#endif

#include <cstring>
#include <string>

namespace cd::rhi::d3d12
{

#if defined(_WIN32)

namespace
{

using Microsoft::WRL::ComPtr;

[[nodiscard]] const char* target_for_sm5(cd::rhi::ShaderStage stage) noexcept
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

[[nodiscard]] const wchar_t* target_for_sm6(cd::rhi::ShaderStage stage, ShaderModel sm) noexcept
{
    using S = cd::rhi::ShaderStage;
    const bool sm65 = (sm == ShaderModel::kSM6_5);
    switch (stage)
    {
        case S::kVertex:      return sm65 ? L"vs_6_5" : L"vs_6_0";
        case S::kFragment:    return sm65 ? L"ps_6_5" : L"ps_6_0";
        case S::kCompute:     return sm65 ? L"cs_6_5" : L"cs_6_0";
        case S::kGeometry:    return sm65 ? L"gs_6_5" : L"gs_6_0";
        case S::kTessControl: return sm65 ? L"hs_6_5" : L"hs_6_0";
        case S::kTessEval:    return sm65 ? L"ds_6_5" : L"ds_6_0";
        case S::kRayGen:      return L"lib_6_5";   // RT shaders compile as libs
        case S::kClosestHit:  return L"lib_6_5";
        case S::kAnyHit:      return L"lib_6_5";
        case S::kMiss:        return L"lib_6_5";
        case S::kIntersection:return L"lib_6_5";
        case S::kCallable:    return L"lib_6_5";
        case S::kMesh:        return L"ms_6_5";
        case S::kTask:        return L"as_6_5";
        default: return nullptr;
    }
}

[[nodiscard]] std::wstring widen(std::string_view s)
{
    std::wstring w;
    w.reserve(s.size());
    for (char c : s) w.push_back(static_cast<wchar_t>(c));
    return w;
}

}  // namespace  (anonymous helpers — compile_* functions follow)

cd::core::Result<std::vector<std::uint8_t>> compile_sm5_d3dcompile(const CompileOptions& opts)
{
    const char* target = target_for_sm5(opts.stage);
    if (target == nullptr)
    {
        return std::unexpected(shader_errors::make(
            shader_errors::Code::kUnsupportedStage,
            "ShaderStage has no Shader Model 5 target (use SM6 via DXC)"));
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

cd::core::Result<std::vector<std::uint8_t>> compile_sm6_dxc(const CompileOptions& opts)
{
    const wchar_t* target = target_for_sm6(opts.stage, opts.model);
    if (target == nullptr)
    {
        return std::unexpected(shader_errors::make(
            shader_errors::Code::kUnsupportedStage,
            "ShaderStage has no SM6 target mapping"));
    }

    ComPtr<IDxcUtils> utils;
    ComPtr<IDxcCompiler3> compiler;
    HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
    if (FAILED(hr))
    {
        return std::unexpected(shader_errors::make(
            shader_errors::Code::kDxcUnavailable,
            "DxcCreateInstance(CLSID_DxcUtils) failed — dxcompiler.dll missing?"));
    }
    hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));
    if (FAILED(hr))
    {
        return std::unexpected(shader_errors::make(
            shader_errors::Code::kDxcUnavailable,
            "DxcCreateInstance(CLSID_DxcCompiler) failed"));
    }

    // Source blob (UTF-8). DXC accepts UTF-8 + DXC_CP_UTF8 hint.
    DxcBuffer src {};
    src.Ptr = opts.source.data();
    src.Size = opts.source.size();
    src.Encoding = DXC_CP_UTF8;

    const std::wstring wentry = widen(opts.entry_point);
    const std::wstring wname  = widen(opts.source_name);

    std::vector<const wchar_t*> args;
    args.push_back(wname.c_str());
    args.push_back(L"-E");
    args.push_back(wentry.c_str());
    args.push_back(L"-T");
    args.push_back(target);
    args.push_back(L"-HV");
    args.push_back(L"2021");  // HLSL 2021 features
    if (opts.optimization_level == 0)
    {
        args.push_back(L"-Od");
        args.push_back(L"-Zi");
    }
    else
    {
        args.push_back(L"-O3");
    }

    ComPtr<IDxcResult> result;
    hr = compiler->Compile(&src, args.data(), static_cast<UINT32>(args.size()),
                           nullptr /*include handler*/, IID_PPV_ARGS(&result));
    if (FAILED(hr) || result == nullptr)
    {
        return std::unexpected(shader_errors::make(
            shader_errors::Code::kCompileFailed,
            "IDxcCompiler3::Compile returned a failed HRESULT"));
    }

    HRESULT status = S_OK;
    result->GetStatus(&status);
    if (FAILED(status))
    {
        ComPtr<IDxcBlobUtf8> errors;
        result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
        std::string msg = "DXC compile failed: ";
        if (errors && errors->GetStringLength() > 0)
            msg.append(errors->GetStringPointer(), errors->GetStringLength());
        return std::unexpected(shader_errors::make(
            shader_errors::Code::kCompileFailed, msg));
    }

    ComPtr<IDxcBlob> object;
    result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr);
    if (object == nullptr || object->GetBufferSize() == 0)
    {
        return std::unexpected(shader_errors::make(
            shader_errors::Code::kCompileFailed,
            "DXC produced no DXIL object"));
    }
    std::vector<std::uint8_t> out(object->GetBufferSize());
    std::memcpy(out.data(), object->GetBufferPointer(), out.size());
    return out;
}

cd::core::Result<std::vector<std::uint8_t>> compile_hlsl(const CompileOptions& opts)
{
    if (opts.model == ShaderModel::kSM5_1)
        return compile_sm5_d3dcompile(opts);
    return compile_sm6_dxc(opts);
}

#else  // _WIN32

cd::core::Result<std::vector<std::uint8_t>> compile_hlsl(const CompileOptions&)
{
    return std::unexpected(shader_errors::make(
        shader_errors::Code::kBackendUnavailable,
        "D3D12 shader compile only available on Windows"));
}

#endif  // _WIN32

}  // namespace cd::rhi::d3d12

#if defined(_WIN32) && defined(__clang__)
    #pragma clang diagnostic pop  // Wlanguage-extension-token (IID_PPV_ARGS)
#endif
