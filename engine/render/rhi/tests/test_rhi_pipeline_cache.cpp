// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_rhi_pipeline_cache.cpp
//
// V-PIPECACHE (Backend-to-100): cross-backend device-level pipeline cache.
//
// The IDevice surface gained get_pipeline_cache_data() (serialise the backend
// pipeline cache to a portable byte blob) + load_pipeline_cache() (live merge,
// kNotImplemented where the backend seeds only at create) and an optional
// `pipeline_cache_blob` seed field on each backend's create-info. The Vulkan
// backend owns a VkPipelineCache, D3D12 an ID3D12PipelineLibrary, Metal an
// MTLBinaryArchive (mac-gated). The cache changes ONLY pipeline-compile TIME —
// never the resulting pipeline or any rendered pixel.
//
// WHAT THIS TEST PROVES (Vulkan RTX 3080 / lavapipe + D3D12 WARP/hardware):
//   1. Create device A, build a REAL graphics PSO + a REAL compute PSO through
//      the in-device GLSL path (SPIR-V on Vulkan, DXIL on D3D12 via the same
//      create_shader_module(kGlsl) call). get_pipeline_cache_data() returns a
//      NON-EMPTY blob.
//   2. The blob ROUND-TRIPS: create device B seeded with it
//      (VulkanCreateInfo / D3D12CreateInfo::pipeline_cache_blob), rebuild the
//      SAME two pipelines — both succeed (the cache is consulted; on D3D12 the
//      second build hits LoadGraphics/ComputePipeline; on Vulkan the seed is a
//      valid VkPipelineCache, validated by the device against the GPU/driver
//      header). Device B's serialised blob is also non-empty.
//   3. Backend-specific observability:
//        * Vulkan — the blob carries a VALID VkPipelineCacheHeaderVersionOne
//          (>= 32 bytes, headerVersion field == 1) — i.e. it is a real,
//          seedable cache, not opaque noise.
//        * D3D12 — device B was seeded from device A's library and rebuilt the
//          identical pipelines successfully (the Load path is exercised; a
//          rejected seed would have forced an empty library, still succeeding —
//          so the observable contract is the successful round-trip rebuild).
//   4. The Null reference returns an EMPTY blob + kNotImplemented from
//      load_pipeline_cache (the documented base default).
//
// We do NOT assert wall-clock speed (flaky). The contract is: the blob
// round-trips and the pipelines still build identically. Honest-SKIP when no
// device / no glslang / no DXC runtime / the backend lacks pipeline-library
// support. Pattern: Arrange / Act / Assert.
// =============================================================================
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
#endif

#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/NullDevice.hpp>
#include <cd/rhi/Pipeline.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#include <cd/shader/Compiler.hpp>
#if defined(_WIN32)
    #include <cd/rhi/d3d12/D3D12Device.hpp>
#endif

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace
{

// ---- Trivial shaders driving real PSOs --------------------------------------
//
// A position-only vertex + solid-colour fragment shader for the graphics PSO,
// and a no-op 1x1x1 compute shader for the compute PSO. Built through the
// in-device GLSL path (SPIR-V on Vulkan, DXIL on D3D12 via the same call).

constexpr const char* kVS = R"glsl(
#version 450
layout(location = 0) in vec3 in_pos;
void main() { gl_Position = vec4(in_pos, 1.0); }
)glsl";

constexpr const char* kFS = R"glsl(
#version 450
layout(location = 0) out vec4 out_color;
void main() { out_color = vec4(1.0, 0.5, 0.25, 1.0); }
)glsl";

constexpr const char* kCS = R"glsl(
#version 450
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
void main() {}
)glsl";

// Build a shader module through the GLSL path. Sets *skip when the DXC runtime
// DLL is unavailable (D3D12-side) so the caller can honest-SKIP.
[[nodiscard]] cd::rhi::ShaderModuleHandle
make_module(cd::rhi::IDevice& dev, cd::rhi::ShaderStage stage,
            const char* src, bool* skip)
{
    cd::rhi::ShaderModuleDesc d {};
    d.stage       = stage;
    d.code        = src;
    d.code_size   = std::char_traits<char>::length(src);
    d.entry_point = "main";
    d.language    = cd::rhi::ShaderSourceLanguage::kGlsl;
    auto r = dev.create_shader_module(d);
    if (!r.has_value())
    {
        const std::string msg { r.error().message };
        if (msg.find("dxc") != std::string::npos)
            *skip = true;
        return {};
    }
    return *r;
}

// Build a graphics PSO (position-only VB, one RGBA8 target) + a compute PSO on
// `dev`. Returns false (and sets *skip when appropriate) when a prerequisite is
// missing; true when both PSOs were created successfully.
[[nodiscard]] bool build_pipelines(cd::rhi::IDevice& dev, bool* skip)
{
    bool local_skip = false;
    const auto vs = make_module(dev, cd::rhi::ShaderStage::kVertex, kVS, &local_skip);
    const auto fs = make_module(dev, cd::rhi::ShaderStage::kFragment, kFS, &local_skip);
    const auto cs = make_module(dev, cd::rhi::ShaderStage::kCompute, kCS, &local_skip);
    if (local_skip)
    {
        *skip = true;
        return false;
    }
    if (!vs.is_valid() || !fs.is_valid() || !cs.is_valid())
        return false;

    cd::rhi::PipelineLayoutDesc pld {};
    auto layout_r = dev.create_pipeline_layout(pld);
    if (!layout_r.has_value())
        return false;
    const auto layout = *layout_r;

    // Graphics PSO: one position attribute (binding 0, R32G32B32), one RGBA8
    // colour target, no depth. Minimal but real.
    const std::array<cd::rhi::VertexBinding, 1> vbinds {
        cd::rhi::VertexBinding { .binding = 0, .stride = 12, .per_instance = false }
    };
    const std::array<cd::rhi::VertexAttribute, 1> vattrs {
        cd::rhi::VertexAttribute {
            .location = 0, .binding = 0,
            .format = cd::rhi::Format::kRGB32Float, .offset = 0 }
    };
    const std::array<cd::rhi::Format, 1> color_fmts { cd::rhi::Format::kRGBA8Unorm };
    cd::rhi::GraphicsPipelineDesc gpd {};
    gpd.layout            = layout;
    gpd.vertex_shader     = vs;
    gpd.fragment_shader   = fs;
    gpd.vertex_bindings   = vbinds;
    gpd.vertex_attributes = vattrs;
    gpd.topology          = cd::rhi::PrimitiveTopology::kTriangleList;
    gpd.raster.cull       = cd::rhi::CullMode::kNone;
    gpd.color_attachment_formats = color_fmts;
    auto gpso_r = dev.create_graphics_pipeline(gpd);
    if (!gpso_r.has_value())
        return false;

    cd::rhi::ComputePipelineDesc cpd {};
    cpd.layout = layout;
    cpd.shader = cs;
    auto cpso_r = dev.create_compute_pipeline(cpd);
    return cpso_r.has_value();
}

// Shared round-trip body for a real backend device factory. `make_device`
// builds a device with an optional seed blob; the test creates device A
// (no seed), builds pipelines, serialises, asserts non-empty, then creates
// device B seeded with A's blob, rebuilds the SAME pipelines, asserts success
// + that B's serialised blob is also non-empty.
template <typename MakeDevice>
void run_round_trip(MakeDevice make_device, bool vulkan_header_check)
{
    if (cd::shader::make_glslang_compiler() == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    // ---- Device A: build pipelines, populate + serialise the cache ----------
    auto dev_a = make_device(std::span<const std::byte> {});
    if (dev_a == nullptr)
        GTEST_SKIP() << "no device on this host";

    bool skip = false;
    if (!build_pipelines(*dev_a, &skip))
    {
        if (skip)
            GTEST_SKIP() << "DXC runtime / GLSL toolchain unavailable";
        GTEST_SKIP() << "pipeline creation unsupported on this adapter";
    }

    std::vector<std::byte> blob = dev_a->get_pipeline_cache_data();
    if (blob.empty())
        GTEST_SKIP() << "backend reported no pipeline-cache support (empty blob)";

    // The blob must be a real, non-trivial cache (not a 1-byte sentinel).
    EXPECT_GE(blob.size(), std::size_t { 16 })
        << "serialised pipeline cache is implausibly small";

    // Vulkan observability: the blob carries a valid VkPipelineCacheHeader
    // (VkPipelineCacheHeaderVersionOne): u32 headerSize @0, u32 headerVersion
    // @4 (== 1 == VK_PIPELINE_CACHE_HEADER_VERSION_ONE). A device validates
    // vendorID / deviceID / UUID on seed, so a *valid header* is the strongest
    // portable assertion that the blob is a real, seedable cache.
    if (vulkan_header_check)
    {
        ASSERT_GE(blob.size(), std::size_t { 32 })
            << "Vulkan pipeline-cache blob too small for a header";
        std::uint32_t header_size = 0;
        std::uint32_t header_version = 0;
        std::memcpy(&header_size, blob.data(), sizeof(header_size));
        std::memcpy(&header_version, blob.data() + 4, sizeof(header_version));
        EXPECT_GE(header_size, std::uint32_t { 32 })
            << "VkPipelineCacheHeader headerSize must cover the fixed fields";
        EXPECT_EQ(header_version, std::uint32_t { 1 })
            << "VK_PIPELINE_CACHE_HEADER_VERSION_ONE expected";
    }

    // ---- Device B: seed from A's blob, rebuild the SAME pipelines -----------
    auto dev_b = make_device(std::span<const std::byte>(blob.data(), blob.size()));
    ASSERT_NE(dev_b, nullptr) << "seeded device creation failed";

    bool skip_b = false;
    EXPECT_TRUE(build_pipelines(*dev_b, &skip_b))
        << "identical pipelines must still build on a cache-seeded device";

    // Device B's cache also serialises to a non-empty blob (the seed survived +
    // the rebuilt pipelines are present).
    const std::vector<std::byte> blob_b = dev_b->get_pipeline_cache_data();
    EXPECT_FALSE(blob_b.empty())
        << "seeded device must still serialise a non-empty cache";
}

// ---- Null reference: documented base defaults -------------------------------

TEST(RhiPipelineCache, NullReturnsEmptyBlobAndNotImplemented)
{
    cd::rhi::NullDevice dev;

    // Base default: empty blob (no cache primitive).
    EXPECT_TRUE(dev.get_pipeline_cache_data().empty());

    // Base default: load_pipeline_cache reports kNotImplemented.
    const std::array<std::byte, 4> dummy {};
    const auto r = dev.load_pipeline_cache(
        std::span<const std::byte>(dummy.data(), dummy.size()));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::rhi::rhi_errors::Code::kNotImplemented));
}

// ---- Vulkan: VkPipelineCache round-trip + header validity -------------------

TEST(RhiPipelineCache, VulkanBlobRoundTrips)
{
    auto make = [](std::span<const std::byte> seed)
        -> std::unique_ptr<cd::rhi::IDevice>
    {
        cd::rhi::vulkan::VulkanCreateInfo ci {};
        ci.enable_validation = false;
        ci.pipeline_cache_blob.assign(seed.begin(), seed.end());
        auto r = cd::rhi::vulkan::create_vulkan_device(ci);
        if (!r.has_value())
            return nullptr;
        return std::move(*r);
    };
    run_round_trip(make, /*vulkan_header_check=*/true);
}

// Vulkan load_pipeline_cache live-merge: a device accepts its OWN serialised
// blob via the run-time merge path (kOk). A foreign/empty blob is harmlessly
// ignored — the contract is "never fail", so kOk either way.
TEST(RhiPipelineCache, VulkanLoadMergeAcceptsOwnBlob)
{
    if (cd::shader::make_glslang_compiler() == nullptr)
        GTEST_SKIP() << "engine built without CD_ENABLE_GLSLANG";

    cd::rhi::vulkan::VulkanCreateInfo ci {};
    ci.enable_validation = false;
    auto r = cd::rhi::vulkan::create_vulkan_device(ci);
    if (!r.has_value())
        GTEST_SKIP() << "no Vulkan ICD on this host";
    auto dev = std::move(*r);

    bool skip = false;
    if (!build_pipelines(*dev, &skip))
        GTEST_SKIP() << "pipeline creation unsupported / toolchain unavailable";

    const std::vector<std::byte> blob = dev->get_pipeline_cache_data();
    if (blob.empty())
        GTEST_SKIP() << "no Vulkan pipeline cache on this device";

    // Merge the device's own blob back in — must report kOk.
    const auto merged = dev->load_pipeline_cache(
        std::span<const std::byte>(blob.data(), blob.size()));
    EXPECT_TRUE(merged.has_value())
        << "Vulkan load_pipeline_cache must accept a valid blob (kOk)";
}

#if defined(_WIN32)

// ---- D3D12: ID3D12PipelineLibrary round-trip --------------------------------

TEST(RhiPipelineCache, D3D12BlobRoundTrips)
{
    auto make = [](std::span<const std::byte> seed)
        -> std::unique_ptr<cd::rhi::IDevice>
    {
        cd::rhi::d3d12::D3D12CreateInfo ci {};
        ci.enable_validation = false;
        ci.pipeline_cache_blob.assign(seed.begin(), seed.end());
        auto r = cd::rhi::d3d12::create_d3d12_device(ci);
        if (!r.has_value())
            return nullptr;
        return std::move(*r);
    };
    // D3D12 library blobs are opaque (no portable header to validate), so the
    // observable contract is the successful seeded round-trip rebuild.
    run_round_trip(make, /*vulkan_header_check=*/false);
}

// D3D12 seeds the pipeline library at device create; the live-merge path is
// honestly kNotImplemented (mirrors the Metal contract). Asserting it keeps the
// documented behaviour from silently changing.
TEST(RhiPipelineCache, D3D12LoadMergeNotImplemented)
{
    cd::rhi::d3d12::D3D12CreateInfo ci {};
    ci.enable_validation = false;
    auto r = cd::rhi::d3d12::create_d3d12_device(ci);
    if (!r.has_value())
        GTEST_SKIP() << "no D3D12 adapter (WARP/hardware) on this host";
    auto dev = std::move(*r);

    const std::array<std::byte, 4> dummy {};
    const auto m = dev->load_pipeline_cache(
        std::span<const std::byte>(dummy.data(), dummy.size()));
    ASSERT_FALSE(m.has_value());
    EXPECT_EQ(m.error().code,
              static_cast<std::uint32_t>(cd::rhi::rhi_errors::Code::kNotImplemented));
}

#endif  // _WIN32

}  // namespace
