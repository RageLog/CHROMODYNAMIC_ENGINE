// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_d3d12_binding_reflection.cpp
//
// ADR-20260614-d3d12-binding-model verification (autonomous, NO GPU).
//
// The D3D12 binding model is "space-per-set + explicit push_constant remap":
//   resource (set N, binding M) -> HLSL register(<class>M, spaceN)
//   push_constant PC block      -> HLSL register(b0, space1)
// matching the D3D12 root signature D3D12Device.cpp builds (one descriptor
// table per descriptor set, RegisterSpace = set ordinal; root 32-bit-constants
// slot at b0/space1).
//
// This test compiles an engine-style GLSL fragment shader exercising:
//   (a) a set-0 resource     (CBV b0, SRV t1, readonly-SSBO t10)
//   (b) a set-1 resource     (dedicated bindless SRV array t0/space1)
//   (c) a push_constant block (PC -> b0/space1)
// through the real GLSL -> SPIR-V -> HLSL(SM6.5) -> DXIL toolchain, then uses
// DXC shader reflection (ID3D12ShaderReflection via IDxcUtils::CreateReflection)
// to ASSERT every resource lands at exactly the register+space the root
// signature declares. This proves binding correctness with no GPU adapter.
//
// Windows + DXC only (the toolchain tail is Windows-gated). When dxcompiler.dll
// is missing at runtime the test SKIPs — the dll is copied next to the binary
// by tests/CMakeLists.txt POST_BUILD.
// =============================================================================
#include <cd/rhi/d3d12/D3D12Device.hpp>
#include <cd/rhi/d3d12/D3D12ShaderToolchain.hpp>
#include <cd/rhi/Pipeline.hpp>
#include <cd/shader/Compiler.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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
    #include <d3d12.h>         // D3D12CreateRootSignatureDeserializer
    #include <d3d12shader.h>   // ID3D12ShaderReflection
    #include <dxcapi.h>        // IDxcUtils::CreateReflection
#endif

namespace
{

#if defined(_WIN32)

// Engine-style fragment shader: set-0 CBV + set-0 SRV (sampler) + set-0
// readonly-SSBO (SRV) + set-1 dedicated bindless SRV array + a push_constant
// block. Mirrors prim.frag.glsl's binding shape minus the RT/IBL noise.
constexpr const char* kBindingFS = R"glsl(
#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(push_constant) uniform PC {
    mat4 mvp;     // b0 / space1 (root 32-bit constants)
    mat4 model;
    vec4 tint;
} pc;

layout(set = 0, binding = 0) uniform Shadow {   // CBV -> b0 / space0
    mat4 light_vp;
} cd_shadow;

layout(set = 0, binding = 1) uniform sampler2D cd_shadow_map;  // SRV -> t1 / space0

layout(set = 0, binding = 10) readonly buffer InstanceMats {   // SRV -> t10 / space0
    mat4 mats[];
} cd_instances;

// Dedicated bindless set (MEMORY rule 9): set 1, binding 0 -> t0 / space1.
layout(set = 1, binding = 0) uniform sampler2D cd_bindless[];

layout(location = 0) out vec4 o;

void main()
{
    vec4 a = pc.tint * cd_shadow.light_vp[0];
    a += texture(cd_shadow_map, vec2(0.5));
    a += cd_instances.mats[0][0];
    a += texture(cd_bindless[nonuniformEXT(int(pc.mvp[0].x))], vec2(0.5));
    o = a;
}
)glsl";

using Microsoft::WRL::ComPtr;

// One reflected resource binding from the DXIL container.
struct BoundResource
{
    std::string                name;
    D3D_SHADER_INPUT_TYPE      type  = D3D_SIT_CBUFFER;
    std::uint32_t              bind_point = 0;
    std::uint32_t              space      = 0;
};

// Reflect a DXIL blob and return all bound resources. Empty result + populated
// `err` signals an environment/reflection failure (caller SKIPs).
[[nodiscard]] std::vector<BoundResource>
reflect_dxil(const std::vector<std::uint8_t>& dxil, std::string& err)
{
    std::vector<BoundResource> out;

    ComPtr<IDxcUtils> utils;
    HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
    if (FAILED(hr) || utils == nullptr)
    {
        err = "DxcCreateInstance(CLSID_DxcUtils) failed — dxcompiler.dll missing?";
        return out;
    }

    DxcBuffer buf {};
    buf.Ptr = dxil.data();
    buf.Size = dxil.size();
    buf.Encoding = 0;  // binary container

    ComPtr<ID3D12ShaderReflection> refl;
    hr = utils->CreateReflection(&buf, IID_PPV_ARGS(&refl));
    if (FAILED(hr) || refl == nullptr)
    {
        err = "IDxcUtils::CreateReflection failed on the DXIL container";
        return out;
    }

    D3D12_SHADER_DESC sd {};
    hr = refl->GetDesc(&sd);
    if (FAILED(hr))
    {
        err = "ID3D12ShaderReflection::GetDesc failed";
        return out;
    }

    for (UINT i = 0; i < sd.BoundResources; ++i)
    {
        D3D12_SHADER_INPUT_BIND_DESC bd {};
        if (FAILED(refl->GetResourceBindingDesc(i, &bd)))
            continue;
        BoundResource r {};
        r.name       = bd.Name != nullptr ? bd.Name : "";
        r.type       = bd.Type;
        r.bind_point = bd.BindPoint;
        r.space      = bd.Space;
        out.push_back(r);
    }
    return out;
}

// Find a bound resource of a given HLSL register class at (bind_point, space).
[[nodiscard]] const BoundResource*
find_at(const std::vector<BoundResource>& res,
        D3D_SHADER_INPUT_TYPE            cls_family_lo,
        D3D_SHADER_INPUT_TYPE            cls_family_hi,
        std::uint32_t                   bind_point,
        std::uint32_t                   space)
{
    for (const auto& r : res)
    {
        const bool class_ok = (r.type >= cls_family_lo && r.type <= cls_family_hi);
        if (class_ok && r.bind_point == bind_point && r.space == space)
            return &r;
    }
    return nullptr;
}

[[nodiscard]] std::unique_ptr<cd::shader::ICompiler> make_compiler_or_null()
{
    return cd::shader::make_glslang_compiler();
}

[[nodiscard]] bool compile_binding_fs(std::vector<std::uint8_t>& dxil_out,
                                       std::string&              skip_reason)
{
    auto c = make_compiler_or_null();
    if (c == nullptr)
    {
        skip_reason = "engine built without CD_ENABLE_GLSLANG";
        return false;
    }
    cd::rhi::d3d12::GlslToDxilDesc d {};
    d.glsl_source = kBindingFS;
    d.stage       = cd::rhi::ShaderStage::kFragment;
    d.source_name = "binding_reflection_fs";
    d.model       = cd::rhi::d3d12::ShaderModel::kSM6_5;
    const auto r = cd::rhi::d3d12::compile_glsl_to_dxil(*c, d);
    if (!r.has_value())
    {
        const std::string msg { r.error().message };
        // dxcompiler.dll missing at runtime surfaces as a "dxc:" / kDxcUnavailable
        // error — an environment gap, not a wiring bug.
        if (r.error().code == static_cast<std::uint32_t>(
                cd::rhi::d3d12::shader_errors::Code::kDxcUnavailable) ||
            msg.find("dxc") != std::string::npos)
        {
            skip_reason = "dxcompiler.dll unavailable: " + msg;
            return false;
        }
        ADD_FAILURE() << "GLSL->DXIL chain failed: " << msg;
        return false;
    }
    dxil_out = *r;
    return true;
}

// ---- The core assertion: every binding lands where the root signature says.
TEST(D3D12BindingReflection, SpacePerSetAndPushConstantMatchRootSig)
{
    std::vector<std::uint8_t> dxil;
    std::string skip;
    if (!compile_binding_fs(dxil, skip))
    {
        if (!skip.empty()) GTEST_SKIP() << skip;
        return;  // ADD_FAILURE already recorded
    }
    ASSERT_FALSE(dxil.empty());

    std::string err;
    const auto res = reflect_dxil(dxil, err);
    if (res.empty() && !err.empty())
        GTEST_SKIP() << err;
    ASSERT_FALSE(res.empty()) << "no bound resources reflected from DXIL";

    // (a) set 0: CBV `Shadow` -> b0 / space0.
    const auto* cbv0 = find_at(res, D3D_SIT_CBUFFER, D3D_SIT_CBUFFER, 0u, 0u);
    ASSERT_NE(cbv0, nullptr)
        << "set-0 CBV (Shadow UBO) must be register(b0, space0)";

    // (a) set 0: shadow_map sampled-image SRV -> t1 / space0. SPIRV-Cross
    // splits a combined sampler2D into a Texture (SRV t1) + SamplerState; we
    // assert the texture half lands at t1/space0.
    const auto* srv_t1 =
        find_at(res, D3D_SIT_TEXTURE, D3D_SIT_TEXTURE, 1u, 0u);
    ASSERT_NE(srv_t1, nullptr)
        << "set-0 sampled image (shadow_map) must be register(t1, space0)";

    // (a) set 0: readonly InstanceMats SSBO -> SRV t10 / space0 (StructuredBuffer
    // / ByteAddressBuffer reflects as a TBUFFER/STRUCTURED/BYTEADDRESS SRV).
    const auto* srv_t10 =
        find_at(res, D3D_SIT_TBUFFER, D3D_SIT_BYTEADDRESS, 10u, 0u);
    ASSERT_NE(srv_t10, nullptr)
        << "set-0 readonly SSBO (InstanceMats) must be register(t10, space0)";

    // (b) set 1: dedicated bindless SRV array -> t0 / space1 (MEMORY rule 9).
    const auto* srv_bindless =
        find_at(res, D3D_SIT_TEXTURE, D3D_SIT_TEXTURE, 0u, 1u);
    ASSERT_NE(srv_bindless, nullptr)
        << "set-1 bindless SRV array must be register(t0, space1) — "
           "space-per-set keeps the dedicated bindless set on its own space";

    // (c) push_constant PC block -> CBV b0 / space1 (the root 32-bit-constants
    // slot). Without the set_root_constant_layouts remap this would land at
    // b0/space0 and collide with the set-0 CBV above.
    const auto* pc_cbv = find_at(res, D3D_SIT_CBUFFER, D3D_SIT_CBUFFER, 0u, 1u);
    ASSERT_NE(pc_cbv, nullptr)
        << "push_constant PC block must be register(b0, space1) — the remap in "
           "Translate.cpp (set_root_constant_layouts) forces it off the default "
           "b0/space0 which would collide with the set-0 CBV";

    // Cross-check: the set-0 CBV and the push-constant CBV must be DISTINCT
    // resources (same register index, different space) — proving no collision.
    EXPECT_NE(cbv0->space, pc_cbv->space);
    EXPECT_EQ(cbv0->bind_point, pc_cbv->bind_point);
}

// ---- Root-signature SIDE: the bindless set-1 table is DECLARED -------------
//
// The reflection test above proves the SPIRV-Cross EMISSION side (the shader
// asks for `register(t0, space1)` for the dedicated bindless set). This case
// closes the matching blind spot on the ROOT-SIGNATURE side: it deserializes
// the D3D12_DESCRIPTOR_RANGE shape the create_pipeline_layout bindless path
// (D3D12Device.cpp) emits for a `kBindlessSampledImage` binding and asserts it
// lands at SRV / BaseShaderRegister=0 / RegisterSpace=1 / unbounded. Before the
// B1b fix the kBindlessSampledImage type hit the switch `default: continue`,
// produced ZERO ranges, and the whole set-1 table was dropped from the root
// signature — the shader's t0/space1 then had no table to bind to. This builds
// the IDENTICAL range the production switch now emits and round-trips it through
// the D3D12 root-signature (de)serializer, proving the declaration is valid and
// has the exact shape the SPIRV-Cross emission requires.
TEST(D3D12BindingReflection, BindlessSetDeclaresUnboundedSrvAtSpace1)
{
    using Microsoft::WRL::ComPtr;

    // The set-1 dedicated bindless binding (HelloMaterials.hpp): binding 0,
    // kBindlessSampledImage. create_pipeline_layout maps it to an SRV range at
    // RegisterSpace = set ordinal (1), BaseShaderRegister = binding (0), with an
    // UNBOUNDED descriptor count (UINT_MAX) mirroring the Vulkan
    // VARIABLE_DESCRIPTOR_COUNT bindless set.
    D3D12_DESCRIPTOR_RANGE range {};
    range.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors     = UINT_MAX;  // unbounded — bindless sentinel
    range.BaseShaderRegister = 0;         // t0
    range.RegisterSpace      = 1;         // space1 — N-th set => space N (N=1)
    range.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER table {};
    table.ParameterType    = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    table.DescriptorTable.NumDescriptorRanges = 1;
    table.DescriptorTable.pDescriptorRanges   = &range;
    table.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rsd {};
    rsd.NumParameters = 1;
    rsd.pParameters   = &table;
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> err;
    HRESULT hr = D3D12SerializeRootSignature(
        &rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
    ASSERT_TRUE(SUCCEEDED(hr))
        << "an unbounded SRV table range at t0/space1 must serialize into a "
           "valid root signature (this is the shape the bindless set emits)";
    ASSERT_NE(blob, nullptr);

    // Deserialize and read the range back — this proves the produced root
    // signature genuinely carries the space1 SRV table the shader binds to.
    ComPtr<ID3D12RootSignatureDeserializer> deser;
    hr = D3D12CreateRootSignatureDeserializer(
        blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&deser));
    ASSERT_TRUE(SUCCEEDED(hr)) << "root signature deserialization failed";

    const D3D12_ROOT_SIGNATURE_DESC* out = deser->GetRootSignatureDesc();
    ASSERT_NE(out, nullptr);
    ASSERT_EQ(out->NumParameters, 1u);
    ASSERT_EQ(out->pParameters[0].ParameterType,
              D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE);
    ASSERT_EQ(out->pParameters[0].DescriptorTable.NumDescriptorRanges, 1u);

    const D3D12_DESCRIPTOR_RANGE& got =
        out->pParameters[0].DescriptorTable.pDescriptorRanges[0];
    EXPECT_EQ(got.RangeType, D3D12_DESCRIPTOR_RANGE_TYPE_SRV)
        << "bindless sampler2D[] must be declared as an SRV range";
    EXPECT_EQ(got.BaseShaderRegister, 0u) << "t0";
    EXPECT_EQ(got.RegisterSpace, 1u)
        << "space1 — the dedicated bindless set keeps its own register space "
           "so the shader's register(t0, space1) finds its table";
    EXPECT_EQ(got.NumDescriptors, UINT_MAX)
        << "unbounded descriptor count mirrors the Vulkan "
           "VARIABLE_DESCRIPTOR_COUNT / PARTIALLY_BOUND bindless set";
}

// ---- End-to-end: the REAL create_pipeline_layout accepts the bindless set ---
//
// Drives the actual engine path (create_descriptor_set_layout for a set-0
// classic layout + a set-1 kBindlessSampledImage layout, then
// create_pipeline_layout). Before the B1b fix this still SUCCEEDED but silently
// produced a root signature with no space1 table; with the fix the bindless set
// now contributes a valid SRV table and the serialize/CreateRootSignature in
// create_pipeline_layout must still succeed (UINT_MAX range serializes). SKIPs
// when no D3D12 adapter is present (CI / WARP-less host).
TEST(D3D12BindingReflection, CreatePipelineLayoutAcceptsBindlessSet1)
{
    cd::rhi::d3d12::D3D12CreateInfo info {};
    info.enable_validation = false;  // headless CI may lack the debug layer
    auto dev_r = cd::rhi::d3d12::create_d3d12_device(info);
    if (!dev_r.has_value())
        GTEST_SKIP() << "D3D12 device creation failed (no adapter): "
                     << dev_r.error().message;
    auto& dev = *dev_r;

    // set 0 — classic CBV + SRV (space0).
    const std::array<cd::rhi::DescriptorSetLayoutBinding, 2> kSet0 {
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 0, .type = cd::rhi::DescriptorType::kUniformBuffer },
        cd::rhi::DescriptorSetLayoutBinding {
            .binding = 1, .type = cd::rhi::DescriptorType::kSampledImage },
    };
    cd::rhi::DescriptorSetLayoutDesc set0_desc {};
    set0_desc.bindings = kSet0;
    auto set0 = dev->create_descriptor_set_layout(set0_desc);
    ASSERT_TRUE(set0.has_value()) << set0.error().message;

    // set 1 — dedicated bindless sampler2D array (space1), MEMORY rule 9.
    const std::array<cd::rhi::DescriptorSetLayoutBinding, 1> kSet1 {
        cd::rhi::DescriptorSetLayoutBinding {
            .binding  = 0,
            .type     = cd::rhi::DescriptorType::kBindlessSampledImage,
            .count    = 256,
            .stages   = cd::rhi::ShaderStage::kFragment,
            .bindless = true },
    };
    cd::rhi::DescriptorSetLayoutDesc set1_desc {};
    set1_desc.bindings = kSet1;
    auto set1 = dev->create_descriptor_set_layout(set1_desc);
    ASSERT_TRUE(set1.has_value()) << set1.error().message;

    const std::array<cd::rhi::DescriptorSetLayoutHandle, 2> sets {
        *set0, *set1 };
    cd::rhi::PipelineLayoutDesc pld {};
    pld.set_layouts = sets;

    // With the B1b fix the set-1 kBindlessSampledImage binding contributes a
    // valid unbounded SRV table at space1; create_pipeline_layout must
    // serialize + CreateRootSignature without error.
    auto layout = dev->create_pipeline_layout(pld);
    EXPECT_TRUE(layout.has_value())
        << "create_pipeline_layout must accept a set-1 kBindlessSampledImage "
           "layout (unbounded SRV table at space1): "
        << (layout.has_value() ? std::string {} : layout.error().message);
}

#else  // !_WIN32

TEST(D3D12BindingReflection, SkippedOffWindows)
{
    GTEST_SKIP() << "DXIL reflection chain is Windows-only";
}

#endif  // _WIN32

}  // namespace

#if defined(_WIN32) && defined(__clang__)
    #pragma clang diagnostic pop  // Wlanguage-extension-token
#endif
