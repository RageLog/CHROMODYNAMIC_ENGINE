// =============================================================================
// CHROMODYNAMIC — cd/rhi/vulkan/VulkanDevice.cpp
// =============================================================================
#include "VulkanInternal.hpp"

// VMA is included via the impl-in-one-TU pattern; here we want only the
// declarations so we mirror VulkanVma.cpp's macro state on the consumer side.
#define VMA_STATIC_VULKAN_FUNCTIONS  0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_VULKAN_VERSION           1003000
#if defined(__clang__) || defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#if defined(__clang__)
    #pragma clang diagnostic ignored "-Wnullability-completeness"
    #pragma clang diagnostic ignored "-Wnullability-extension"
#endif
#include <vk_mem_alloc.h>
#if defined(__clang__) || defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cd/rhi/ICommandBuffer.hpp>  // full type — VulkanDevice::create_command_buffer
#include "VulkanCommandBuffer.hpp"   // ResourceTables + AccelBuildView (Phase 132)
                                      // returns unique_ptr<ICommandBuffer>, the
                                      // dtor needs the complete type at the call
                                      // site (GCC enforces this earlier than Clang).
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>  // QueueFamilyInfo (multi-queue introspection bridge)
#include <cd/rhi/vulkan/VulkanFormat.hpp>  // vk_aspect_for_format public alias

// D16 (parity): in-device GLSL/HLSL→SPIR-V cross-compile in create_shader_module
// so a single create_shader_module(kGlsl, <source>) works on BOTH backends.
#include <cd/gluon/ModuleRegistry.hpp>     // cd::gluon::ModuleResolver (#include catalogue)
#include <cd/shader/Compiler.hpp>          // cd::shader::ICompiler / make_glslang_compiler

#include <array>
#include <cstddef>  // std::byte (V-PIPECACHE blob)
#include <cstdio>   // env-driven device selection diagnostic
#include <cstdlib>  // std::getenv / std::atoi for CD_VULKAN_DEVICE_INDEX
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cd::rhi::vulkan
{

// Forward declaration so the factory in VulkanDeviceFactory.cpp can reference
// this implementation. GCC -Wmissing-declarations requires every non-static
// definition to be preceded by a declaration with the same signature.
// NOLINTNEXTLINE(misc-use-internal-linkage) — consumed cross-TU by VulkanDeviceFactory.cpp via a local declaration.
[[nodiscard]] cd::core::Result<std::unique_ptr<cd::rhi::IDevice>> create_device(
    std::unique_ptr<VulkanInstance> inst,
    const std::vector<std::string>& device_extensions,
    bool prefer_discrete,
    std::vector<std::byte> pipeline_cache_blob = {}
);

namespace
{

[[nodiscard]] cd::core::ErrorCode make_err(cd::rhi::rhi_errors::Code c, std::string_view m)
{
    return cd::rhi::rhi_errors::make(c, m);
}

[[nodiscard]] VkBufferUsageFlags map_buffer_usage(cd::rhi::BufferUsage u)
{
    VkBufferUsageFlags out = 0;
    using BU = cd::rhi::BufferUsage;
    if (cd::rhi::has(u, BU::kTransferSrc))
        out |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (cd::rhi::has(u, BU::kTransferDst))
        out |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (cd::rhi::has(u, BU::kUniform))
        out |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (cd::rhi::has(u, BU::kStorage))
        out |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (cd::rhi::has(u, BU::kVertex))
        out |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (cd::rhi::has(u, BU::kIndex))
        out |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (cd::rhi::has(u, BU::kIndirect))
        out |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    if (cd::rhi::has(u, BU::kShaderBindingTable))
    {
        out |= VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR;
        out |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    }
    return out;
}

// Maps a RHI MemoryUsage onto VMA's "what does the user want?" model. We
// favor VMA_MEMORY_USAGE_AUTO (introduced in 3.0) and steer placement via
// VMA_ALLOCATION_CREATE_HOST_ACCESS_* flags — this lets VMA pick the right
// heap (BAR / non-coherent host / device-local) on every platform without
// us hard-coding VkMemoryPropertyFlags.
struct VmaUsageMapping
{
    VmaMemoryUsage usage { VMA_MEMORY_USAGE_AUTO };
    VmaAllocationCreateFlags flags { 0 };
    bool host_visible { false };
};

[[nodiscard]] VmaUsageMapping map_vma_usage(cd::rhi::MemoryUsage u)
{
    using MU = cd::rhi::MemoryUsage;
    switch (u)
    {
        case MU::kGpuOnly:
            return { VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0, false };
        case MU::kCpuToGpu:
            // Upload buffer: write sequentially from CPU, read on GPU. VMA may put
            // this in BAR memory if available.
            return { VMA_MEMORY_USAGE_AUTO,
                     VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                     true };
        case MU::kGpuToCpu:
            // Readback: GPU writes, CPU reads random-access.
            return { VMA_MEMORY_USAGE_AUTO,
                     VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                     true };
        case MU::kCpuRandomAccess:
            return { VMA_MEMORY_USAGE_AUTO,
                     VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                     true };
        case MU::kAuto:
            return { VMA_MEMORY_USAGE_AUTO, 0, false };
    }
    return { VMA_MEMORY_USAGE_AUTO, 0, false };
}

[[nodiscard]] VkImageUsageFlags map_texture_usage(cd::rhi::TextureUsage u)
{
    using TU = cd::rhi::TextureUsage;
    VkImageUsageFlags out = 0;
    if (cd::rhi::has(u, TU::kTransferSrc))
        out |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (cd::rhi::has(u, TU::kTransferDst))
        out |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (cd::rhi::has(u, TU::kSampled))
        out |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (cd::rhi::has(u, TU::kStorage))
        out |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (cd::rhi::has(u, TU::kColorAttachment))
        out |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (cd::rhi::has(u, TU::kDepthStencilAttachment))
        out |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (cd::rhi::has(u, TU::kInputAttachment))
        out |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    return out;
}

[[nodiscard]] VkImageType map_texture_type(cd::rhi::TextureType t)
{
    using TT = cd::rhi::TextureType;
    switch (t)
    {
        case TT::k1D:
        case TT::k1DArray:
            return VK_IMAGE_TYPE_1D;
        case TT::k3D:
            return VK_IMAGE_TYPE_3D;
        default:
            return VK_IMAGE_TYPE_2D;
    }
}

[[nodiscard]] VkImageViewType map_view_type(cd::rhi::TextureType t)
{
    using TT = cd::rhi::TextureType;
    switch (t)
    {
        case TT::k1D:
            return VK_IMAGE_VIEW_TYPE_1D;
        case TT::k1DArray:
            return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
        case TT::k2D:
            return VK_IMAGE_VIEW_TYPE_2D;
        case TT::k2DArray:
            return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        case TT::k3D:
            return VK_IMAGE_VIEW_TYPE_3D;
        case TT::kCube:
            return VK_IMAGE_VIEW_TYPE_CUBE;
        case TT::kCubeArray:
            return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    }
    return VK_IMAGE_VIEW_TYPE_2D;
}

[[nodiscard]] VkSampleCountFlagBits map_samples(cd::rhi::SampleCount s)
{
    switch (s)
    {
        case cd::rhi::SampleCount::k1:
            return VK_SAMPLE_COUNT_1_BIT;
        case cd::rhi::SampleCount::k2:
            return VK_SAMPLE_COUNT_2_BIT;
        case cd::rhi::SampleCount::k4:
            return VK_SAMPLE_COUNT_4_BIT;
        case cd::rhi::SampleCount::k8:
            return VK_SAMPLE_COUNT_8_BIT;
        case cd::rhi::SampleCount::k16:
            return VK_SAMPLE_COUNT_16_BIT;
    }
    return VK_SAMPLE_COUNT_1_BIT;
}

[[nodiscard]] VkFilter map_filter(cd::rhi::SamplerFilter f)
{
    return f == cd::rhi::SamplerFilter::kLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
}

[[nodiscard]] VkSamplerMipmapMode map_mipmap_mode(cd::rhi::SamplerMipmapMode m)
{
    return m == cd::rhi::SamplerMipmapMode::kLinear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
}

[[nodiscard]] VkSamplerAddressMode map_address_mode(cd::rhi::SamplerAddressMode a)
{
    switch (a)
    {
        case cd::rhi::SamplerAddressMode::kRepeat:
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case cd::rhi::SamplerAddressMode::kMirroredRepeat:
            return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case cd::rhi::SamplerAddressMode::kClampToEdge:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case cd::rhi::SamplerAddressMode::kClampToBorder:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        case cd::rhi::SamplerAddressMode::kMirrorClampToEdge:
            return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
    }
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

[[nodiscard]] VkCompareOp map_compare(cd::rhi::CompareOp c)
{
    switch (c)
    {
        case cd::rhi::CompareOp::kNever:
            return VK_COMPARE_OP_NEVER;
        case cd::rhi::CompareOp::kLess:
            return VK_COMPARE_OP_LESS;
        case cd::rhi::CompareOp::kEqual:
            return VK_COMPARE_OP_EQUAL;
        case cd::rhi::CompareOp::kLessEqual:
            return VK_COMPARE_OP_LESS_OR_EQUAL;
        case cd::rhi::CompareOp::kGreater:
            return VK_COMPARE_OP_GREATER;
        case cd::rhi::CompareOp::kNotEqual:
            return VK_COMPARE_OP_NOT_EQUAL;
        case cd::rhi::CompareOp::kGreaterEqual:
            return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case cd::rhi::CompareOp::kAlways:
            return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_ALWAYS;
}

[[nodiscard]] VkBorderColor map_border_color(cd::rhi::BorderColor b)
{
    switch (b)
    {
        case cd::rhi::BorderColor::kFloatTransparentBlack:
            return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        case cd::rhi::BorderColor::kFloatOpaqueBlack:
            return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        case cd::rhi::BorderColor::kFloatOpaqueWhite:
            return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        case cd::rhi::BorderColor::kIntTransparentBlack:
            return VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
        case cd::rhi::BorderColor::kIntOpaqueBlack:
            return VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        case cd::rhi::BorderColor::kIntOpaqueWhite:
            return VK_BORDER_COLOR_INT_OPAQUE_WHITE;
    }
    return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
}

[[nodiscard]] VkFormat map_format(cd::rhi::Format f)
{
    using F = cd::rhi::Format;
    switch (f)
    {
        case F::kUndefined:
            return VK_FORMAT_UNDEFINED;
        case F::kR8Unorm:
            return VK_FORMAT_R8_UNORM;
        case F::kR8Snorm:
            return VK_FORMAT_R8_SNORM;
        case F::kR8Uint:
            return VK_FORMAT_R8_UINT;
        case F::kR8Sint:
            return VK_FORMAT_R8_SINT;
        case F::kRG8Unorm:
            return VK_FORMAT_R8G8_UNORM;
        case F::kRG8Snorm:
            return VK_FORMAT_R8G8_SNORM;
        case F::kRG8Uint:
            return VK_FORMAT_R8G8_UINT;
        case F::kRG8Sint:
            return VK_FORMAT_R8G8_SINT;
        case F::kRGBA8Unorm:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case F::kRGBA8Snorm:
            return VK_FORMAT_R8G8B8A8_SNORM;
        case F::kRGBA8Uint:
            return VK_FORMAT_R8G8B8A8_UINT;
        case F::kRGBA8Sint:
            return VK_FORMAT_R8G8B8A8_SINT;
        case F::kRGBA8Srgb:
            return VK_FORMAT_R8G8B8A8_SRGB;
        case F::kBGRA8Unorm:
            return VK_FORMAT_B8G8R8A8_UNORM;
        case F::kBGRA8Srgb:
            return VK_FORMAT_B8G8R8A8_SRGB;
        case F::kR16Unorm:
            return VK_FORMAT_R16_UNORM;
        case F::kR16Snorm:
            return VK_FORMAT_R16_SNORM;
        case F::kR16Uint:
            return VK_FORMAT_R16_UINT;
        case F::kR16Sint:
            return VK_FORMAT_R16_SINT;
        case F::kR16Float:
            return VK_FORMAT_R16_SFLOAT;
        case F::kRG16Unorm:
            return VK_FORMAT_R16G16_UNORM;
        case F::kRG16Snorm:
            return VK_FORMAT_R16G16_SNORM;
        case F::kRG16Uint:
            return VK_FORMAT_R16G16_UINT;
        case F::kRG16Sint:
            return VK_FORMAT_R16G16_SINT;
        case F::kRG16Float:
            return VK_FORMAT_R16G16_SFLOAT;
        case F::kRGBA16Unorm:
            return VK_FORMAT_R16G16B16A16_UNORM;
        case F::kRGBA16Snorm:
            return VK_FORMAT_R16G16B16A16_SNORM;
        case F::kRGBA16Uint:
            return VK_FORMAT_R16G16B16A16_UINT;
        case F::kRGBA16Sint:
            return VK_FORMAT_R16G16B16A16_SINT;
        case F::kRGBA16Float:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case F::kR32Uint:
            return VK_FORMAT_R32_UINT;
        case F::kR32Sint:
            return VK_FORMAT_R32_SINT;
        case F::kR32Float:
            return VK_FORMAT_R32_SFLOAT;
        case F::kRG32Uint:
            return VK_FORMAT_R32G32_UINT;
        case F::kRG32Sint:
            return VK_FORMAT_R32G32_SINT;
        case F::kRG32Float:
            return VK_FORMAT_R32G32_SFLOAT;
        case F::kRGB32Uint:
            return VK_FORMAT_R32G32B32_UINT;
        case F::kRGB32Sint:
            return VK_FORMAT_R32G32B32_SINT;
        case F::kRGB32Float:
            return VK_FORMAT_R32G32B32_SFLOAT;
        case F::kRGBA32Uint:
            return VK_FORMAT_R32G32B32A32_UINT;
        case F::kRGBA32Sint:
            return VK_FORMAT_R32G32B32A32_SINT;
        case F::kRGBA32Float:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
        case F::kR11G11B10Float:
            return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
        case F::kRGB10A2Unorm:
            return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        case F::kRGB10A2Uint:
            return VK_FORMAT_A2B10G10R10_UINT_PACK32;
        case F::kRGB9E5Float:
            return VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;
        case F::kD16Unorm:
            return VK_FORMAT_D16_UNORM;
        case F::kD32Float:
            return VK_FORMAT_D32_SFLOAT;
        case F::kD24UnormS8Uint:
            return VK_FORMAT_D24_UNORM_S8_UINT;
        case F::kD32FloatS8Uint:
            return VK_FORMAT_D32_SFLOAT_S8_UINT;
        case F::kS8Uint:
            return VK_FORMAT_S8_UINT;
        case F::kBC1RGBUnorm:
            return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
        case F::kBC1RGBSrgb:
            return VK_FORMAT_BC1_RGB_SRGB_BLOCK;
        case F::kBC1RGBAUnorm:
            return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case F::kBC1RGBASrgb:
            return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
        case F::kBC2Unorm:
            return VK_FORMAT_BC2_UNORM_BLOCK;
        case F::kBC2Srgb:
            return VK_FORMAT_BC2_SRGB_BLOCK;
        case F::kBC3Unorm:
            return VK_FORMAT_BC3_UNORM_BLOCK;
        case F::kBC3Srgb:
            return VK_FORMAT_BC3_SRGB_BLOCK;
        case F::kBC4Unorm:
            return VK_FORMAT_BC4_UNORM_BLOCK;
        case F::kBC4Snorm:
            return VK_FORMAT_BC4_SNORM_BLOCK;
        case F::kBC5Unorm:
            return VK_FORMAT_BC5_UNORM_BLOCK;
        case F::kBC5Snorm:
            return VK_FORMAT_BC5_SNORM_BLOCK;
        case F::kBC6HUFloat:
            return VK_FORMAT_BC6H_UFLOAT_BLOCK;
        case F::kBC6HSFloat:
            return VK_FORMAT_BC6H_SFLOAT_BLOCK;
        case F::kBC7Unorm:
            return VK_FORMAT_BC7_UNORM_BLOCK;
        case F::kBC7Srgb:
            return VK_FORMAT_BC7_SRGB_BLOCK;
        case F::kCount:
            return VK_FORMAT_UNDEFINED;
    }
    return VK_FORMAT_UNDEFINED;
}

[[nodiscard]] VkPrimitiveTopology map_topology(cd::rhi::PrimitiveTopology t)
{
    using T = cd::rhi::PrimitiveTopology;
    switch (t)
    {
        case T::kPointList:
            return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case T::kLineList:
            return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case T::kLineStrip:
            return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case T::kTriangleList:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case T::kTriangleStrip:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case T::kTriangleFan:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

[[nodiscard]] VkPolygonMode map_polygon_mode(cd::rhi::PolygonMode m)
{
    switch (m)
    {
        case cd::rhi::PolygonMode::kFill:
            return VK_POLYGON_MODE_FILL;
        case cd::rhi::PolygonMode::kLine:
            return VK_POLYGON_MODE_LINE;
        case cd::rhi::PolygonMode::kPoint:
            return VK_POLYGON_MODE_POINT;
    }
    return VK_POLYGON_MODE_FILL;
}

[[nodiscard]] VkCullModeFlags map_cull_mode(cd::rhi::CullMode m)
{
    switch (m)
    {
        case cd::rhi::CullMode::kNone:
            return VK_CULL_MODE_NONE;
        case cd::rhi::CullMode::kFront:
            return VK_CULL_MODE_FRONT_BIT;
        case cd::rhi::CullMode::kBack:
            return VK_CULL_MODE_BACK_BIT;
        case cd::rhi::CullMode::kFrontAndBack:
            return VK_CULL_MODE_FRONT_AND_BACK;
    }
    return VK_CULL_MODE_BACK_BIT;
}

[[nodiscard]] VkFrontFace map_front_face(cd::rhi::FrontFace f)
{
    return f == cd::rhi::FrontFace::kClockwise ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
}

[[nodiscard]] VkBlendFactor map_blend_factor(cd::rhi::BlendFactor f)
{
    using BF = cd::rhi::BlendFactor;
    switch (f)
    {
        case BF::kZero:
            return VK_BLEND_FACTOR_ZERO;
        case BF::kOne:
            return VK_BLEND_FACTOR_ONE;
        case BF::kSrcColor:
            return VK_BLEND_FACTOR_SRC_COLOR;
        case BF::kOneMinusSrcColor:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case BF::kDstColor:
            return VK_BLEND_FACTOR_DST_COLOR;
        case BF::kOneMinusDstColor:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case BF::kSrcAlpha:
            return VK_BLEND_FACTOR_SRC_ALPHA;
        case BF::kOneMinusSrcAlpha:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case BF::kDstAlpha:
            return VK_BLEND_FACTOR_DST_ALPHA;
        case BF::kOneMinusDstAlpha:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case BF::kConstantColor:
            return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case BF::kOneMinusConstantColor:
            return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        case BF::kConstantAlpha:
            return VK_BLEND_FACTOR_CONSTANT_ALPHA;
        case BF::kOneMinusConstantAlpha:
            return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        case BF::kSrcAlphaSaturate:
            return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    }
    return VK_BLEND_FACTOR_ZERO;
}

[[nodiscard]] VkBlendOp map_blend_op(cd::rhi::BlendOp o)
{
    switch (o)
    {
        case cd::rhi::BlendOp::kAdd:
            return VK_BLEND_OP_ADD;
        case cd::rhi::BlendOp::kSubtract:
            return VK_BLEND_OP_SUBTRACT;
        case cd::rhi::BlendOp::kReverseSubtract:
            return VK_BLEND_OP_REVERSE_SUBTRACT;
        case cd::rhi::BlendOp::kMin:
            return VK_BLEND_OP_MIN;
        case cd::rhi::BlendOp::kMax:
            return VK_BLEND_OP_MAX;
    }
    return VK_BLEND_OP_ADD;
}

[[nodiscard]] VkShaderStageFlags map_shader_stages(cd::rhi::ShaderStage s)
{
    using SS = cd::rhi::ShaderStage;
    VkShaderStageFlags out = 0;
    if (cd::rhi::has(s, SS::kVertex))
        out |= VK_SHADER_STAGE_VERTEX_BIT;
    if (cd::rhi::has(s, SS::kFragment))
        out |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (cd::rhi::has(s, SS::kGeometry))
        out |= VK_SHADER_STAGE_GEOMETRY_BIT;
    if (cd::rhi::has(s, SS::kTessControl))
        out |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    if (cd::rhi::has(s, SS::kTessEval))
        out |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    if (cd::rhi::has(s, SS::kCompute))
        out |= VK_SHADER_STAGE_COMPUTE_BIT;
    return out;
}

// D16 (parity): cd::rhi::ShaderStage (single-stage) → cd::shader::ShaderStage,
// for the in-device GLSL/HLSL→SPIR-V cross-compile path in create_shader_module.
// Mirrors the D3D12 backend's to_shader_stage (Compiler.hpp keeps the shader
// library free of the RHI headers, so the mapping is an explicit copy). The
// argument carries exactly one stage bit (a shader module is single-stage).
[[nodiscard]] cd::shader::ShaderStage map_shader_stage(cd::rhi::ShaderStage s)
{
    using RS = cd::rhi::ShaderStage;
    using CS = cd::shader::ShaderStage;
    if (cd::rhi::has(s, RS::kFragment))     return CS::kFragment;
    if (cd::rhi::has(s, RS::kCompute))      return CS::kCompute;
    if (cd::rhi::has(s, RS::kGeometry))     return CS::kGeometry;
    if (cd::rhi::has(s, RS::kTessControl))  return CS::kTessControl;
    if (cd::rhi::has(s, RS::kTessEval))     return CS::kTessEval;
    if (cd::rhi::has(s, RS::kRayGen))       return CS::kRaygen;
    if (cd::rhi::has(s, RS::kAnyHit))       return CS::kAnyHit;
    if (cd::rhi::has(s, RS::kClosestHit))   return CS::kClosestHit;
    if (cd::rhi::has(s, RS::kMiss))         return CS::kMiss;
    if (cd::rhi::has(s, RS::kIntersection)) return CS::kIntersection;
    if (cd::rhi::has(s, RS::kCallable))     return CS::kCallable;
    if (cd::rhi::has(s, RS::kMesh))         return CS::kMesh;
    if (cd::rhi::has(s, RS::kTask))         return CS::kTask;
    return CS::kVertex;  // kVertex or kNone default
}

[[nodiscard]] VkDescriptorType map_descriptor_type(cd::rhi::DescriptorType d)
{
    using DT = cd::rhi::DescriptorType;
    switch (d)
    {
        case DT::kSampler:
            return VK_DESCRIPTOR_TYPE_SAMPLER;
        case DT::kCombinedImageSampler:
            return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        case DT::kSampledImage:
            return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case DT::kStorageImage:
            return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case DT::kUniformBuffer:
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case DT::kStorageBuffer:
            return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case DT::kUniformBufferDynamic:
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        case DT::kStorageBufferDynamic:
            return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        case DT::kInputAttachment:
            return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        case DT::kAccelerationStructure:
            return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        case DT::kBindlessSampledImage:
            // phase837-W8-BE: bindless sampler2D array is still
            // represented at the Vulkan layer as COMBINED_IMAGE_SAMPLER
            // — the binding's `descriptorCount` >1 and the flags
            // (UPDATE_AFTER_BIND | PARTIALLY_BOUND | VARIABLE_DESCRIPTOR_COUNT)
            // make it bindless. See phase838 for the layout-side flags.
            return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    }
    return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
}

[[nodiscard]] VkImageAspectFlags aspect_for_format(cd::rhi::Format f)
{
    if (cd::rhi::is_depth_format(f) && cd::rhi::is_stencil_format(f))
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    if (cd::rhi::is_depth_format(f))
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    if (cd::rhi::is_stencil_format(f))
        return VK_IMAGE_ASPECT_STENCIL_BIT;
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

}  // namespace

// Vulkan V1 (depth-aware barrier fix): public, testable alias of the
// anon-namespace aspect_for_format. Returns the raw VkImageAspectFlags value
// (alias of uint32_t) so the declaring header stays Vulkan-free.
std::uint32_t vk_aspect_for_format(cd::rhi::Format f) noexcept
{
    return static_cast<std::uint32_t>(aspect_for_format(f));
}

class VulkanDevice final : public cd::rhi::IDevice
{
public:
    /// Vulkan V2 (multi-queue model) — the additive async-compute / dedicated-
    /// transfer / present queue handles + their family indices. The graphics
    /// family/queue stay separate constructor args so the unchanged render path
    /// is unmistakable. When a QueueType aliases graphics, the corresponding
    /// family equals graphics_family and the VkQueue handle equals graphics_queue.
    struct QueueSet
    {
        std::uint32_t compute_family { 0 };
        std::uint32_t transfer_family { 0 };
        std::uint32_t present_family { 0 };
        VkQueue compute_queue { VK_NULL_HANDLE };
        VkQueue transfer_queue { VK_NULL_HANDLE };
        VkQueue present_queue { VK_NULL_HANDLE };
        bool compute_dedicated { false };
        bool transfer_dedicated { false };
    };

    VulkanDevice(
        std::unique_ptr<VulkanInstance> inst,
        VkPhysicalDevice pd,
        VkDevice dev,
        std::uint32_t gfx_family,
        VkQueue gfx_queue,
        VmaAllocator allocator,
        std::string name,
        QueueSet queues,
        std::vector<std::byte> pipeline_cache_seed = {}
    ) noexcept
        : inst_ { std::move(inst) }
        , physical_ { pd }
        , device_ { dev }
        , graphics_family_ { gfx_family }
        , graphics_queue_ { gfx_queue }
        , compute_family_ { queues.compute_family }
        , transfer_family_ { queues.transfer_family }
        , present_family_ { queues.present_family }
        , compute_queue_ { queues.compute_queue }
        , transfer_queue_ { queues.transfer_queue }
        , present_queue_ { queues.present_queue }
        , compute_dedicated_ { queues.compute_dedicated }
        , transfer_dedicated_ { queues.transfer_dedicated }
        , vma_allocator_ { allocator }
        , adapter_name_ { std::move(name) }
        , pipeline_cache_seed_ { std::move(pipeline_cache_seed) }
    {
        populate_limits();
        init_pipeline_cache_();
    }

    /// Create the VkPipelineCache. The seed is, in priority order:
    ///   1. The in-memory blob from VulkanCreateInfo::pipeline_cache_blob
    ///      (V-PIPECACHE — a previous device's get_pipeline_cache_data()).
    ///   2. The legacy on-disk `.shader_cache/pipeline_cache.bin`.
    ///   3. Empty.
    /// Best-effort: if the seed is corrupt or comes from a different
    /// driver/GPU, Vulkan rejects it silently and we fall back to an empty
    /// cache. Either way we end up with a valid VkPipelineCache so the rest of
    /// the device init can pass it to vkCreate*Pipelines.
    void init_pipeline_cache_()
    {
        std::vector<std::uint8_t> seed;
        // 1. Prefer the explicitly-supplied in-memory blob (V-PIPECACHE).
        if (!pipeline_cache_seed_.empty())
        {
            seed.resize(pipeline_cache_seed_.size());
            std::memcpy(seed.data(), pipeline_cache_seed_.data(), pipeline_cache_seed_.size());
        }
        // 2. Otherwise fall back to the legacy on-disk cache.
        if (seed.empty())
        {
            std::error_code ec;
            const auto path = pipeline_cache_path_();
            if (std::filesystem::exists(path, ec) && !ec)
            {
                std::ifstream in(path, std::ios::binary | std::ios::ate);
                if (in.is_open())
                {
                    const auto sz = in.tellg();
                    if (sz > 0)
                    {
                        seed.resize(static_cast<std::size_t>(sz));
                        in.seekg(0);
                        in.read(reinterpret_cast<char*>(seed.data()), static_cast<std::streamsize>(sz));
                        if (!in.good() && !in.eof())
                            seed.clear();
                    }
                }
            }
        }

        VkPipelineCacheCreateInfo ci {};
        ci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        ci.initialDataSize = seed.size();
        ci.pInitialData = seed.empty() ? nullptr : seed.data();
        // vkCreatePipelineCache may fail with VK_INCOMPATIBLE_DRIVER_VERSION on
        // a stale seed; try once more with an empty cache before giving up.
        if (vkCreatePipelineCache(device_, &ci, nullptr, &pipeline_cache_) != VK_SUCCESS)
        {
            ci.initialDataSize = 0;
            ci.pInitialData = nullptr;
            vkCreatePipelineCache(device_, &ci, nullptr, &pipeline_cache_);
        }
    }

    /// Write the current pipeline-cache contents to disk so the next
    /// process invocation can seed from it. Failure (no-write directory,
    /// disk full) is silent — a missing cache is correct behaviour.
    void save_pipeline_cache_()
    {
        if (pipeline_cache_ == VK_NULL_HANDLE)
            return;
        std::size_t sz = 0;
        if (vkGetPipelineCacheData(device_, pipeline_cache_, &sz, nullptr) != VK_SUCCESS || sz == 0)
            return;
        std::vector<std::uint8_t> data(sz);
        if (vkGetPipelineCacheData(device_, pipeline_cache_, &sz, data.data()) != VK_SUCCESS)
            return;

        const auto path = pipeline_cache_path_();
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        // Write to a tmp file and rename so a concurrent reader never sees a
        // half-written cache (driver would reject it on next load).
        const auto& tmp = path;
        const auto tmp_path = std::filesystem::path { path.string() + ".tmp" };
        {
            std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
            if (!out.is_open())
                return;
            out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
            if (!out.good())
            {
                out.close();
                std::filesystem::remove(tmp_path, ec);
                return;
            }
        }
        std::filesystem::rename(tmp_path, tmp, ec);
        if (ec)
            std::filesystem::remove(tmp_path, ec);
    }

    [[nodiscard]] static std::filesystem::path pipeline_cache_path_()
    {
        // Co-located with the engine's other on-disk caches; the
        // .shader_cache/ directory is already in .gitignore.
        return std::filesystem::path { ".shader_cache" } / "pipeline_cache.bin";
    }

    [[nodiscard]] std::uint32_t graphics_family() const noexcept
    {
        return graphics_family_;
    }

    [[nodiscard]] VkQueue graphics_queue() const noexcept
    {
        return graphics_queue_;
    }

    // Vulkan V2 (multi-queue model) — read-only introspection used by the
    // multi-queue smoke test to assert the family-selection result on the
    // current GPU (graphics valid; compute/transfer either dedicated or aliased
    // to graphics). These are rhi_vulkan-internal, exposed via the
    // try_fill_queue_families bridge — the IDevice interface stays queue-agnostic.
    [[nodiscard]] std::uint32_t compute_family() const noexcept { return compute_family_; }
    [[nodiscard]] std::uint32_t transfer_family() const noexcept { return transfer_family_; }
    [[nodiscard]] std::uint32_t present_family() const noexcept { return present_family_; }
    [[nodiscard]] bool compute_family_dedicated() const noexcept { return compute_dedicated_; }
    [[nodiscard]] bool transfer_family_dedicated() const noexcept { return transfer_dedicated_; }

    // Public read-only accessors used by `try_fill_native_handles()` so
    // NativeHandles.cpp can hand raw Vulkan handles to opt-in callers
    // (ImGui backend, RenderDoc capture script, etc.) without making the
    // bridge a `friend`. The IDevice abstract interface still does NOT
    // leak these — they're rhi_vulkan-only.
    [[nodiscard]] VkInstance native_instance() const noexcept
    {
        return inst_ != nullptr ? inst_->instance : VK_NULL_HANDLE;
    }

    [[nodiscard]] VkPhysicalDevice native_physical_device() const noexcept
    {
        return physical_;
    }

    [[nodiscard]] VkDevice native_device() const noexcept
    {
        return device_;
    }

    ~VulkanDevice() override
    {
        wait_idle();
        // Swapchains first: they hold image-views that the device must destroy
        // before the surface and device themselves go away.
        for (auto& [_, sc] : swapchains_)
        {
            // Drop our synthetic engine-side image handles before destroying the
            // swapchain — the images_ vmaDestroyImage loop below must never iterate
            // over swapchain-owned VkImages. Same treatment for the views_ map
            // entries so vkDestroyImageView below doesn't double-free.
            for (auto img_id : sc.image_ids)
            {
                images_.erase(img_id);
            }
            for (auto view_id : sc.view_ids)
            {
                views_.erase(view_id);
                view_formats_.erase(view_id);  // phase1119 (audit A4)
            }
            for (auto v : sc.views)
            {
                if (v != VK_NULL_HANDLE)
                    vkDestroyImageView(device_, v, nullptr);
            }
            if (sc.swapchain != VK_NULL_HANDLE)
            {
                vkDestroySwapchainKHR(device_, sc.swapchain, nullptr);
            }
            if (sc.surface != VK_NULL_HANDLE && inst_ != nullptr)
            {
                vkDestroySurfaceKHR(inst_->instance, sc.surface, nullptr);
            }
        }
        swapchains_.clear();
        // Sync primitives: must outlive every submit that referenced them; we
        // already wait_idle'd above so destroying now is safe.
        for (auto& [_, s] : semaphores_)
        {
            vkDestroySemaphore(device_, s, nullptr);
        }
        for (auto& [_, s] : timeline_semaphores_)
        {
            vkDestroySemaphore(device_, s, nullptr);
        }
        for (auto& [_, f] : fences_)
        {
            vkDestroyFence(device_, f, nullptr);
        }
        // A-QUERY (Backend-to-100 Wave 3a): query pools outlive every submit
        // that referenced them; we already wait_idle'd above.
        for (auto& [_, q] : query_pools_)
        {
            vkDestroyQueryPool(device_, q.pool, nullptr);
        }
        query_pools_.clear();
        query_pool_handles_.clear();
        // Order: pipelines/layouts before shaders/set-layouts (depend on them);
        //        views before images (views reference images);
        //        samplers and pipeline_layouts are independent.
        if (graphics_pool_ != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(device_, graphics_pool_, nullptr);
            graphics_pool_ = VK_NULL_HANDLE;
        }
        // Vulkan V2 (multi-queue model): tear down the additive compute/transfer
        // pools. They are distinct VkCommandPool objects even when their family
        // aliases graphics, so each needs its own destroy.
        if (compute_pool_ != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(device_, compute_pool_, nullptr);
            compute_pool_ = VK_NULL_HANDLE;
        }
        if (transfer_pool_ != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(device_, transfer_pool_, nullptr);
            transfer_pool_ = VK_NULL_HANDLE;
        }
        if (descriptor_pool_ != VK_NULL_HANDLE)
        {
            // Destroying the pool frees every set allocated from it; no per-set
            // cleanup needed.
            vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
            descriptor_pool_ = VK_NULL_HANDLE;
        }
        // Persist the pipeline cache BEFORE destroying any pipelines — the
        // cache lookup is keyed on full pipeline state hashes and saving
        // after pipelines are destroyed is still valid, but doing it here
        // keeps the order simple.
        save_pipeline_cache_();
        if (pipeline_cache_ != VK_NULL_HANDLE)
        {
            vkDestroyPipelineCache(device_, pipeline_cache_, nullptr);
            pipeline_cache_ = VK_NULL_HANDLE;
        }
        for (auto& [_, p] : compute_pipelines_)
        {
            vkDestroyPipeline(device_, p, nullptr);
        }
        for (auto& [_, p] : graphics_pipelines_)
        {
            vkDestroyPipeline(device_, p, nullptr);
        }
        for (auto& [_, p] : pipeline_layouts_)
        {
            vkDestroyPipelineLayout(device_, p, nullptr);
        }
        for (auto& [_, l] : set_layouts_)
        {
            vkDestroyDescriptorSetLayout(device_, l, nullptr);
        }
        for (auto& [_, v] : views_)
        {
            vkDestroyImageView(device_, v, nullptr);
        }
        // Images: VkImage + backing VmaAllocation freed together. We iterate
        // images_ (the authoritative map) and look up the allocation; both maps
        // were inserted in lockstep by create_texture.
        for (auto& [id, img] : images_)
        {
            auto ait = image_alloc_.find(id);
            VmaAllocation alloc = ait != image_alloc_.end() ? ait->second : VK_NULL_HANDLE;
            vmaDestroyImage(vma_allocator_, img, alloc);
        }
        images_.clear();
        image_alloc_.clear();
        for (auto& [_, s] : samplers_)
        {
            vkDestroySampler(device_, s, nullptr);
        }
        for (auto& [id, buf] : buffers_)
        {
            auto ait = buffer_alloc_.find(id);
            VmaAllocation alloc = ait != buffer_alloc_.end() ? ait->second : VK_NULL_HANDLE;
            vmaDestroyBuffer(vma_allocator_, buf, alloc);
        }
        buffers_.clear();
        buffer_alloc_.clear();
        for (auto& [_, s] : shaders_)
        {
            vkDestroyShaderModule(device_, s, nullptr);
        }
        // Allocator must outlive every resource it owns and die *before* the
        // device whose memory it brokered.
        if (vma_allocator_ != VK_NULL_HANDLE)
        {
            vmaDestroyAllocator(vma_allocator_);
            vma_allocator_ = VK_NULL_HANDLE;
        }
        if (device_ != VK_NULL_HANDLE)
            vkDestroyDevice(device_, nullptr);
    }

    // --- Introspection ----------------------------------------------------
    [[nodiscard]] cd::rhi::Backend backend() const noexcept override
    {
        return cd::rhi::Backend::kVulkan;
    }

    [[nodiscard]] std::string_view adapter_name() const noexcept override
    {
        return adapter_name_;
    }

    [[nodiscard]] const cd::rhi::DeviceLimits& limits() const noexcept override
    {
        return limits_;
    }

    [[nodiscard]] const cd::rhi::DeviceFeatures& features() const noexcept override
    {
        return features_;
    }

    // --- Buffer (VMA-backed) ---------------------------------------------
    [[nodiscard]] cd::core::Result<cd::rhi::BufferHandle> create_buffer(const cd::rhi::BufferDesc& desc) override
    {
        if (desc.size == 0)
        {
            return std::unexpected(make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "buffer size == 0"));
        }
        const VkBufferCreateInfo bi {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .size = desc.size,
            .usage = map_buffer_usage(desc.usage),
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
        };
        const auto mapping = map_vma_usage(desc.memory);
        const VmaAllocationCreateInfo aci {
            .flags = mapping.flags,
            .usage = mapping.usage,
            .requiredFlags = 0,
            .preferredFlags = 0,
            .memoryTypeBits = 0,
            .pool = VK_NULL_HANDLE,
            .pUserData = nullptr,
            .priority = 0.0F,
        };
        VkBuffer buf { VK_NULL_HANDLE };
        VmaAllocation alloc { VK_NULL_HANDLE };
        VmaAllocationInfo alloc_info {};
        if (vmaCreateBuffer(vma_allocator_, &bi, &aci, &buf, &alloc, &alloc_info) != VK_SUCCESS)
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kResourceCreationFailed, "vmaCreateBuffer failed")
            );
        }
        // VMA already bound the memory inside vmaCreateBuffer.

        const auto id = next_id_++;
        buffers_.emplace(id, buf);
        buffer_alloc_.emplace(id, alloc);
        BufferMeta meta;
        meta.size = desc.size;
        meta.memory_usage = desc.memory;
        // Ask VMA what heap it landed in instead of guessing. This is the source
        // of truth for upload_buffer's host-visible precondition.
        VkMemoryPropertyFlags mem_props { 0 };
        vmaGetAllocationMemoryProperties(vma_allocator_, alloc, &mem_props);
        meta.host_visible = (mem_props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
        buffer_meta_.emplace(id, meta);
        return cd::rhi::BufferHandle { id, 1u };
    }

    void destroy_buffer(cd::rhi::BufferHandle h) override
    {
        auto bit = buffers_.find(h.index());
        auto ait = buffer_alloc_.find(h.index());
        if (bit != buffers_.end())
        {
            VmaAllocation alloc = (ait != buffer_alloc_.end()) ? ait->second : VK_NULL_HANDLE;
            vmaDestroyBuffer(vma_allocator_, bit->second, alloc);
            buffers_.erase(bit);
        }
        if (ait != buffer_alloc_.end())
            buffer_alloc_.erase(ait);
        buffer_meta_.erase(h.index());
    }

    [[nodiscard]] cd::core::Result<void>
    upload_buffer(cd::rhi::BufferHandle h, std::uint64_t offset, std::span<const std::byte> data) override
    {
        auto bit = buffers_.find(h.index());
        auto ait = buffer_alloc_.find(h.index());
        auto met = buffer_meta_.find(h.index());
        if (bit == buffers_.end() || ait == buffer_alloc_.end() || met == buffer_meta_.end())
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "upload_buffer: unknown buffer")
            );
        }
        if (!met->second.host_visible)
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "upload_buffer: buffer is not host-visible")
            );
        }
        if (offset + data.size() > met->second.size)
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "upload_buffer: out of bounds")
            );
        }
        if (data.empty())
            return {};
        // vmaCopyMemoryToAllocation handles map+memcpy+flush+unmap atomically,
        // including the flush for non-coherent heaps that vkMapMemory does not.
        if (vmaCopyMemoryToAllocation(vma_allocator_, data.data(), ait->second, offset, data.size()) != VK_SUCCESS)
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kResourceCreationFailed, "vmaCopyMemoryToAllocation failed")
            );
        }
        return {};
    }

    [[nodiscard]] cd::core::Result<void>
    download_buffer(cd::rhi::BufferHandle h, std::uint64_t offset, std::span<std::byte> dst) override
    {
        auto bit = buffers_.find(h.index());
        auto ait = buffer_alloc_.find(h.index());
        auto met = buffer_meta_.find(h.index());
        if (bit == buffers_.end() || ait == buffer_alloc_.end() || met == buffer_meta_.end())
            return std::unexpected(make_err(cd::rhi::rhi_errors::Code::kInvalidArgument,
                                            "download_buffer: unknown buffer"));
        if (!met->second.host_visible)
            return std::unexpected(make_err(cd::rhi::rhi_errors::Code::kInvalidArgument,
                                            "download_buffer: buffer is not host-visible"));
        if (offset + dst.size() > met->second.size)
            return std::unexpected(make_err(cd::rhi::rhi_errors::Code::kInvalidArgument,
                                            "download_buffer: out of bounds"));
        if (dst.empty())
            return {};
        // vmaCopyAllocationToMemory mirrors vmaCopyMemoryToAllocation:
        // map+invalidate+memcpy+unmap atomically, handling the non-
        // coherent-heap invalidate vkMapMemory doesn't.
        if (vmaCopyAllocationToMemory(vma_allocator_, ait->second, offset, dst.data(), dst.size()) != VK_SUCCESS)
            return std::unexpected(make_err(cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                                            "vmaCopyAllocationToMemory failed"));
        return {};
    }

    // --- Shader module (real) --------------------------------------------
    [[nodiscard]] cd::core::Result<cd::rhi::ShaderModuleHandle>
    create_shader_module(const cd::rhi::ShaderModuleDesc& desc) override
    {
        if (desc.code == nullptr || desc.code_size == 0)
        {
            return std::unexpected(make_err(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "shader_module: code must be non-empty"
            ));
        }

        // D16 (parity): honour desc.language symmetrically with the D3D12
        // backend so a SINGLE create_shader_module(kGlsl, <source>) call works
        // on BOTH backends. kBytecode/kSpirv consume the 32-bit SPIR-V words
        // verbatim (the legacy contract); kGlsl cross-compiles GLSL→SPIR-V via
        // the glslang front-end (mirroring D3D12's GLSL→SPIR-V→DXIL chain — on
        // Vulkan the SPIR-V IS the native bytecode so we stop after stage 1).
        std::vector<std::uint32_t> compiled;  // backing store for the kGlsl path
        const std::uint32_t* words = nullptr;
        std::size_t          word_bytes = 0;

        switch (desc.language)
        {
            case cd::rhi::ShaderSourceLanguage::kBytecode:
            case cd::rhi::ShaderSourceLanguage::kSpirv:
            {
                if ((desc.code_size % 4) != 0)
                {
                    return std::unexpected(make_err(
                        cd::rhi::rhi_errors::Code::kInvalidArgument,
                        "shader_module: SPIR-V code must be 32-bit aligned"));
                }
                words      = static_cast<const std::uint32_t*>(desc.code);
                word_bytes = static_cast<std::size_t>(desc.code_size);
                break;
            }
            case cd::rhi::ShaderSourceLanguage::kGlsl:
            {
                auto compiler = cd::shader::make_glslang_compiler();
                if (compiler == nullptr)
                {
                    return std::unexpected(make_err(
                        cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                        "shader module: GLSL requested but the glslang front-end "
                        "is unavailable (CD_ENABLE_GLSLANG=OFF)"));
                }
                // ADR-20260614 consumer-resolver pattern: a null include_resolver
                // bridges to the embedded cd::gluon catalogue via a FUNCTION-LOCAL
                // resolver; an injected resolver is respected. It must outlive the
                // compile() call so it lives for the whole branch.
                cd::gluon::ModuleResolver default_resolver {};
                auto* injected = static_cast<cd::shader::IIncludeResolver*>(
                    desc.include_resolver);
                cd::shader::CompileDesc sd {};
                sd.source = std::string_view {
                    static_cast<const char*>(desc.code),
                    static_cast<std::size_t>(desc.code_size) };
                sd.stage = map_shader_stage(desc.stage);
                sd.lang  = cd::shader::ShaderLanguage::kGlsl;
                sd.target = cd::shader::TargetEnv::kVulkan13;
                sd.entry_point = desc.entry_point;
                sd.source_name = desc.debug_name.empty()
                                     ? std::string_view { "<inline>" }
                                     : desc.debug_name;
                sd.include_resolver =
                    injected != nullptr ? injected : &default_resolver;
                auto spirv_r = compiler->compile(sd);
                if (!spirv_r.has_value())
                {
                    return std::unexpected(make_err(
                        cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                        std::string { "shader module (GLSL→SPIR-V): " } +
                            std::string { spirv_r.error().message }));
                }
                compiled   = std::move(spirv_r->spirv);
                words      = compiled.data();
                word_bytes = compiled.size() * sizeof(std::uint32_t);
                break;
            }
            case cd::rhi::ShaderSourceLanguage::kHlsl:
            {
                // glslang has an HLSL front-end; route it the same way as GLSL.
                auto compiler = cd::shader::make_glslang_compiler();
                if (compiler == nullptr)
                {
                    return std::unexpected(make_err(
                        cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                        "shader module: HLSL requested but the glslang front-end "
                        "is unavailable (CD_ENABLE_GLSLANG=OFF)"));
                }
                cd::shader::CompileDesc sd {};
                sd.source = std::string_view {
                    static_cast<const char*>(desc.code),
                    static_cast<std::size_t>(desc.code_size) };
                sd.stage = map_shader_stage(desc.stage);
                sd.lang  = cd::shader::ShaderLanguage::kHlsl;
                sd.target = cd::shader::TargetEnv::kVulkan13;
                sd.entry_point = desc.entry_point;
                sd.source_name = desc.debug_name.empty()
                                     ? std::string_view { "<inline>" }
                                     : desc.debug_name;
                auto spirv_r = compiler->compile(sd);
                if (!spirv_r.has_value())
                {
                    return std::unexpected(make_err(
                        cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                        std::string { "shader module (HLSL→SPIR-V): " } +
                            std::string { spirv_r.error().message }));
                }
                compiled   = std::move(spirv_r->spirv);
                words      = compiled.data();
                word_bytes = compiled.size() * sizeof(std::uint32_t);
                break;
            }
        }

        if (words == nullptr || word_bytes == 0 || (word_bytes % 4) != 0)
        {
            return std::unexpected(make_err(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "shader_module: empty/misaligned SPIR-V after compile"));
        }
        const VkShaderModuleCreateInfo ci {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .codeSize = word_bytes,
            .pCode = words,
        };
        VkShaderModule m {};
        if (vkCreateShaderModule(device_, &ci, nullptr, &m) != VK_SUCCESS)
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kResourceCreationFailed, "vkCreateShaderModule failed")
            );
        }
        const auto id = next_id_++;
        shaders_.emplace(id, m);
        return cd::rhi::ShaderModuleHandle { id, 1u };
    }

    void destroy_shader_module(cd::rhi::ShaderModuleHandle h) override
    {
        if (auto it = shaders_.find(h.index()); it != shaders_.end())
        {
            vkDestroyShaderModule(device_, it->second, nullptr);
            shaders_.erase(it);
        }
    }

    // --- Texture / view / sampler (S3.4) ---------------------------------
    [[nodiscard]] cd::core::Result<cd::rhi::TextureHandle> create_texture(const cd::rhi::TextureDesc& desc) override
    {
        if (desc.extent.width == 0 || desc.extent.height == 0)
        {
            return std::unexpected(make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "texture: zero extent"));
        }
        if (desc.format == cd::rhi::Format::kUndefined)
        {
            return std::unexpected(make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "texture: kUndefined format"));
        }
        const VkImageCreateInfo ici {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = (desc.type == cd::rhi::TextureType::kCube || desc.type == cd::rhi::TextureType::kCubeArray)
                         ? VkImageCreateFlags { VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT }
                         : VkImageCreateFlags { 0 },
            .imageType = map_texture_type(desc.type),
            .format = map_format(desc.format),
            .extent = { desc.extent.width, desc.extent.height, desc.extent.depth },
            .mipLevels = desc.mip_levels,
            .arrayLayers = desc.array_layers,
            .samples = map_samples(desc.samples),
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = map_texture_usage(desc.usage),
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        const auto mapping = map_vma_usage(desc.memory);
        const VmaAllocationCreateInfo aci {
            .flags = mapping.flags,
            .usage = mapping.usage,
            .requiredFlags = 0,
            .preferredFlags = 0,
            .memoryTypeBits = 0,
            .pool = VK_NULL_HANDLE,
            .pUserData = nullptr,
            .priority = 0.0F,
        };
        VkImage img { VK_NULL_HANDLE };
        VmaAllocation alloc { VK_NULL_HANDLE };
        if (vmaCreateImage(vma_allocator_, &ici, &aci, &img, &alloc, nullptr) != VK_SUCCESS)
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kResourceCreationFailed, "vmaCreateImage failed")
            );
        }
        const auto id = next_id_++;
        images_.emplace(id, img);
        image_alloc_.emplace(id, alloc);
        TextureMeta meta;
        meta.format = desc.format;
        meta.type = desc.type;
        meta.mip_levels = desc.mip_levels;
        meta.array_layers = desc.array_layers;
        image_meta_.emplace(id, meta);
        image_formats_.emplace(id, desc.format);
        return cd::rhi::TextureHandle { id, 1u };
    }

    void destroy_texture(cd::rhi::TextureHandle h) override
    {
        auto iit = images_.find(h.index());
        auto ait = image_alloc_.find(h.index());
        if (iit != images_.end())
        {
            VmaAllocation alloc = (ait != image_alloc_.end()) ? ait->second : VK_NULL_HANDLE;
            vmaDestroyImage(vma_allocator_, iit->second, alloc);
            images_.erase(iit);
        }
        if (ait != image_alloc_.end())
            image_alloc_.erase(ait);
        image_meta_.erase(h.index());
        image_formats_.erase(h.index());
    }

    [[nodiscard]] cd::core::Result<cd::rhi::TextureViewHandle>
    create_texture_view(const cd::rhi::TextureViewDesc& desc) override
    {
        auto img_it = images_.find(desc.texture.index());
        auto meta_it = image_meta_.find(desc.texture.index());
        if (img_it == images_.end() || meta_it == image_meta_.end())
        {
            return std::unexpected(make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "view: unknown texture"));
        }
        const auto fmt = desc.format == cd::rhi::Format::kUndefined ? meta_it->second.format : desc.format;
        const VkImageViewCreateInfo vci {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = img_it->second,
            .viewType = map_view_type(desc.type),
            .format = map_format(fmt),
            .components = { VK_COMPONENT_SWIZZLE_IDENTITY,
                           VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                           VK_COMPONENT_SWIZZLE_IDENTITY },
            .subresourceRange = { .aspectMask = aspect_for_format(fmt),
                           .baseMipLevel = desc.base_mip,
                           .levelCount = desc.mip_count,
                           .baseArrayLayer = desc.base_layer,
                           .layerCount = desc.layer_count },
        };
        VkImageView view {};
        if (vkCreateImageView(device_, &vci, nullptr, &view) != VK_SUCCESS)
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kResourceCreationFailed, "vkCreateImageView failed")
            );
        }
        const auto id = next_id_++;
        views_.emplace(id, view);
        view_formats_.emplace(id, map_format(fmt));
        return cd::rhi::TextureViewHandle { id, 1u };
    }

    void destroy_texture_view(cd::rhi::TextureViewHandle h) override
    {
        if (auto it = views_.find(h.index()); it != views_.end())
        {
            vkDestroyImageView(device_, it->second, nullptr);
            views_.erase(it);
        }
        view_formats_.erase(h.index());
    }

    [[nodiscard]] cd::core::Result<cd::rhi::SamplerHandle> create_sampler(const cd::rhi::SamplerDesc& desc) override
    {
        const VkSamplerCreateInfo sci {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .magFilter = map_filter(desc.mag_filter),
            .minFilter = map_filter(desc.min_filter),
            .mipmapMode = map_mipmap_mode(desc.mipmap_mode),
            .addressModeU = map_address_mode(desc.address_u),
            .addressModeV = map_address_mode(desc.address_v),
            .addressModeW = map_address_mode(desc.address_w),
            .mipLodBias = desc.mip_lod_bias,
            .anisotropyEnable = desc.anisotropy_enable ? VK_TRUE : VK_FALSE,
            .maxAnisotropy = desc.max_anisotropy,
            .compareEnable = desc.compare_enable ? VK_TRUE : VK_FALSE,
            .compareOp = map_compare(desc.compare_op),
            .minLod = desc.min_lod,
            .maxLod = desc.max_lod,
            .borderColor = map_border_color(desc.border_color),
            .unnormalizedCoordinates = VK_FALSE,
        };
        VkSampler s {};
        if (vkCreateSampler(device_, &sci, nullptr, &s) != VK_SUCCESS)
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kResourceCreationFailed, "vkCreateSampler failed")
            );
        }
        const auto id = next_id_++;
        samplers_.emplace(id, s);
        return cd::rhi::SamplerHandle { id, 1u };
    }

    void destroy_sampler(cd::rhi::SamplerHandle h) override
    {
        if (auto it = samplers_.find(h.index()); it != samplers_.end())
        {
            vkDestroySampler(device_, it->second, nullptr);
            samplers_.erase(it);
        }
    }

    // --- Descriptor / pipeline layout (S3.4) ----------------------------
    [[nodiscard]] cd::core::Result<cd::rhi::DescriptorSetLayoutHandle>
    create_descriptor_set_layout(const cd::rhi::DescriptorSetLayoutDesc& desc) override
    {
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        std::vector<VkDescriptorBindingFlags>     binding_flags;
        bindings.reserve(desc.bindings.size());
        binding_flags.reserve(desc.bindings.size());
        bool any_bindless = false;
        for (std::size_t i = 0; i < desc.bindings.size(); ++i)
        {
            const auto& b = desc.bindings[i];
            bindings.push_back(
                VkDescriptorSetLayoutBinding {
                    .binding = b.binding,
                    .descriptorType = map_descriptor_type(b.type),
                    .descriptorCount = b.count,
                    .stageFlags = map_shader_stages(b.stages),
                    .pImmutableSamplers = nullptr,
                }
            );
            // phase842-W8-BE-rt-bindless-texture-sampling: per-binding
            // flag chain. Non-bindless bindings get 0; bindless ones get
            // UPDATE_AFTER_BIND | PARTIALLY_BOUND. If the bindless
            // binding is the LAST one in the set, also flag it with
            // VARIABLE_DESCRIPTOR_COUNT so the descriptor set can be
            // allocated with a smaller actual count when needed.
            VkDescriptorBindingFlags flags = 0;
            if (b.bindless)
            {
                any_bindless = true;
                flags |= VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
                       | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
                if (i + 1 == desc.bindings.size())
                    flags |= VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
            }
            binding_flags.push_back(flags);
        }
        const VkDescriptorSetLayoutBindingFlagsCreateInfo flags_ci {
            .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
            .pNext         = nullptr,
            .bindingCount  = static_cast<std::uint32_t>(binding_flags.size()),
            .pBindingFlags = binding_flags.empty() ? nullptr : binding_flags.data(),
        };
        const VkDescriptorSetLayoutCreateInfo ci {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = any_bindless ? static_cast<const void*>(&flags_ci) : nullptr,
            .flags = any_bindless
                       ? VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT
                       : 0u,
            .bindingCount = static_cast<std::uint32_t>(bindings.size()),
            .pBindings = bindings.empty() ? nullptr : bindings.data(),
        };
        VkDescriptorSetLayout layout {};
        if (vkCreateDescriptorSetLayout(device_, &ci, nullptr, &layout) != VK_SUCCESS)
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kResourceCreationFailed, "vkCreateDescriptorSetLayout failed")
            );
        }
        const auto id = next_id_++;
        set_layouts_.emplace(id, layout);
        // phase843-W8-BE: record whether the layout has a trailing
        // variable-count binding so allocate_descriptor_set knows to
        // chain VkDescriptorSetVariableDescriptorCountAllocateInfo.
        std::uint32_t var_count = 0u;
        if (!desc.bindings.empty()
            && desc.bindings.back().bindless)
        {
            var_count = desc.bindings.back().count;
        }
        set_layout_meta_.emplace(id, DescriptorSetLayoutMeta {
            .layout             = layout,
            .variable_count_max = var_count,
        });
        return cd::rhi::DescriptorSetLayoutHandle { id, 1u };
    }

    void destroy_descriptor_set_layout(cd::rhi::DescriptorSetLayoutHandle h) override
    {
        if (auto it = set_layouts_.find(h.index()); it != set_layouts_.end())
        {
            vkDestroyDescriptorSetLayout(device_, it->second, nullptr);
            set_layouts_.erase(it);
        }
    }

    [[nodiscard]] cd::core::Result<cd::rhi::PipelineLayoutHandle>
    create_pipeline_layout(const cd::rhi::PipelineLayoutDesc& desc) override
    {
        std::vector<VkDescriptorSetLayout> set_layouts;
        set_layouts.reserve(desc.set_layouts.size());
        for (const auto& h : desc.set_layouts)
        {
            auto it = set_layouts_.find(h.index());
            if (it == set_layouts_.end())
            {
                return std::unexpected(
                    make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "pipeline_layout: unknown set layout")
                );
            }
            set_layouts.push_back(it->second);
        }
        std::vector<VkPushConstantRange> push_ranges;
        push_ranges.reserve(desc.push_constants.size());
        for (const auto& pc : desc.push_constants)
        {
            push_ranges.push_back(
                VkPushConstantRange {
                    .stageFlags = map_shader_stages(pc.stages),
                    .offset = pc.offset,
                    .size = pc.size,
                }
            );
        }
        const VkPipelineLayoutCreateInfo ci {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .setLayoutCount = static_cast<std::uint32_t>(set_layouts.size()),
            .pSetLayouts = set_layouts.empty() ? nullptr : set_layouts.data(),
            .pushConstantRangeCount = static_cast<std::uint32_t>(push_ranges.size()),
            .pPushConstantRanges = push_ranges.empty() ? nullptr : push_ranges.data(),
        };
        VkPipelineLayout layout {};
        if (vkCreatePipelineLayout(device_, &ci, nullptr, &layout) != VK_SUCCESS)
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kResourceCreationFailed, "vkCreatePipelineLayout failed")
            );
        }
        const auto id = next_id_++;
        pipeline_layouts_.emplace(id, layout);
        return cd::rhi::PipelineLayoutHandle { id, 1u };
    }

    void destroy_pipeline_layout(cd::rhi::PipelineLayoutHandle h) override
    {
        if (auto it = pipeline_layouts_.find(h.index()); it != pipeline_layouts_.end())
        {
            vkDestroyPipelineLayout(device_, it->second, nullptr);
            pipeline_layouts_.erase(it);
        }
    }

    // --- Graphics pipeline (S3.4 follow-up) ------------------------------
    [[nodiscard]] cd::core::Result<cd::rhi::GraphicsPipelineHandle>
    create_graphics_pipeline(const cd::rhi::GraphicsPipelineDesc& desc) override
    {
        if (!desc.vertex_shader.is_valid())
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "graphics pipeline: vertex shader required")
            );
        }
        if (!desc.layout.is_valid())
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "graphics pipeline: pipeline layout required")
            );
        }
        auto vs_it = shaders_.find(desc.vertex_shader.index());
        auto layout_it = pipeline_layouts_.find(desc.layout.index());
        if (vs_it == shaders_.end() || layout_it == pipeline_layouts_.end())
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "graphics pipeline: unknown shader/layout handle")
            );
        }

        // --- Shader stages --------------------------------------------------
        std::vector<VkPipelineShaderStageCreateInfo> stages;
        stages.reserve(5);
        stages.push_back(
            VkPipelineShaderStageCreateInfo {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .module = vs_it->second,
                .pName = "main",
                .pSpecializationInfo = nullptr,
            }
        );
        if (desc.fragment_shader.is_valid())
        {
            auto fs_it = shaders_.find(desc.fragment_shader.index());
            if (fs_it == shaders_.end())
            {
                return std::unexpected(make_err(
                    cd::rhi::rhi_errors::Code::kInvalidArgument,
                    "graphics pipeline: unknown fragment shader handle"
                ));
            }
            stages.push_back(
                VkPipelineShaderStageCreateInfo {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = 0,
                    .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .module = fs_it->second,
                    .pName = "main",
                    .pSpecializationInfo = nullptr,
                }
            );
        }

        // --- Vertex input ---------------------------------------------------
        std::vector<VkVertexInputBindingDescription> vk_bindings;
        vk_bindings.reserve(desc.vertex_bindings.size());
        for (const auto& b : desc.vertex_bindings)
        {
            vk_bindings.push_back(
                VkVertexInputBindingDescription {
                    .binding = b.binding,
                    .stride = b.stride,
                    .inputRate = b.per_instance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX,
                }
            );
        }
        std::vector<VkVertexInputAttributeDescription> vk_attrs;
        vk_attrs.reserve(desc.vertex_attributes.size());
        for (const auto& a : desc.vertex_attributes)
        {
            vk_attrs.push_back(
                VkVertexInputAttributeDescription {
                    .location = a.location,
                    .binding = a.binding,
                    .format = map_format(a.format),
                    .offset = a.offset,
                }
            );
        }
        const VkPipelineVertexInputStateCreateInfo vi {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .vertexBindingDescriptionCount = static_cast<std::uint32_t>(vk_bindings.size()),
            .pVertexBindingDescriptions = vk_bindings.empty() ? nullptr : vk_bindings.data(),
            .vertexAttributeDescriptionCount = static_cast<std::uint32_t>(vk_attrs.size()),
            .pVertexAttributeDescriptions = vk_attrs.empty() ? nullptr : vk_attrs.data(),
        };

        // --- Input assembly -------------------------------------------------
        const VkPipelineInputAssemblyStateCreateInfo ia {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .topology = map_topology(desc.topology),
            .primitiveRestartEnable = VK_FALSE,
        };

        // --- Viewport / scissor are dynamic (set per command buffer) --------
        const VkPipelineViewportStateCreateInfo vp {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .viewportCount = 1,
            .pViewports = nullptr,
            .scissorCount = 1,
            .pScissors = nullptr,
        };

        // --- Rasterization --------------------------------------------------
        const VkPipelineRasterizationStateCreateInfo rs {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthClampEnable = desc.raster.depth_clamp ? VK_TRUE : VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = map_polygon_mode(desc.raster.polygon_mode),
            .cullMode = map_cull_mode(desc.raster.cull),
            .frontFace = map_front_face(desc.raster.front_face),
            .depthBiasEnable = desc.raster.depth_bias_enable ? VK_TRUE : VK_FALSE,
            .depthBiasConstantFactor = desc.raster.depth_bias_constant,
            .depthBiasClamp = 0.0F,
            .depthBiasSlopeFactor = desc.raster.depth_bias_slope,
            .lineWidth = desc.raster.line_width,
        };

        // --- Multisample ----------------------------------------------------
        const VkPipelineMultisampleStateCreateInfo ms {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .rasterizationSamples = map_samples(desc.samples),
            .sampleShadingEnable = VK_FALSE,
            .minSampleShading = 0.0F,
            .pSampleMask = nullptr,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE,
        };

        // --- Depth/stencil --------------------------------------------------
        // Force-off depth/stencil tests when the pipeline has no matching
        // attachment under dynamic rendering. Without this gate, drivers
        // run the depth test against a non-existent buffer and discard every
        // fragment — the user sees a black screen with no validation hint.
        // Engines that DO want depth/stencil declare the attachment format
        // explicitly via `depth_attachment_format` / `stencil_attachment_format`.
        const bool has_depth_attach = desc.depth_attachment_format != cd::rhi::Format::kUndefined;
        const bool has_stencil_attach = desc.stencil_attachment_format != cd::rhi::Format::kUndefined;
        const VkPipelineDepthStencilStateCreateInfo ds {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthTestEnable = (desc.depth_stencil.depth_test && has_depth_attach) ? VK_TRUE : VK_FALSE,
            .depthWriteEnable = (desc.depth_stencil.depth_write && has_depth_attach) ? VK_TRUE : VK_FALSE,
            .depthCompareOp = map_compare(desc.depth_stencil.depth_compare),
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = (desc.depth_stencil.stencil_test && has_stencil_attach) ? VK_TRUE : VK_FALSE,
            .front = {},
            .back = {},
            .minDepthBounds = 0.0F,
            .maxDepthBounds = 1.0F,
        };

        // --- Blend per attachment ------------------------------------------
        // Vulkan spec requires the blend-attachment count to match the number
        // of color attachments declared by the pipeline (here: dynamic-
        // rendering's `color_attachment_formats`). If the caller didn't
        // supply any blend states we synthesize "opaque, write-all" entries
        // so fragment-shader output actually reaches the framebuffer.
        std::vector<VkPipelineColorBlendAttachmentState> blends;
        if (!desc.blend_attachments.empty())
        {
            blends.reserve(desc.blend_attachments.size());
            for (const auto& b : desc.blend_attachments)
            {
                blends.push_back(
                    VkPipelineColorBlendAttachmentState {
                        .blendEnable = b.blend_enable ? VK_TRUE : VK_FALSE,
                        .srcColorBlendFactor = map_blend_factor(b.src_color),
                        .dstColorBlendFactor = map_blend_factor(b.dst_color),
                        .colorBlendOp = map_blend_op(b.color_op),
                        .srcAlphaBlendFactor = map_blend_factor(b.src_alpha),
                        .dstAlphaBlendFactor = map_blend_factor(b.dst_alpha),
                        .alphaBlendOp = map_blend_op(b.alpha_op),
                        .colorWriteMask = b.color_write_mask,
                    }
                );
            }
        }
        else
        {
            blends.reserve(desc.color_attachment_formats.size());
            constexpr VkColorComponentFlags kRgbaWrite = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            for (std::size_t i = 0; i < desc.color_attachment_formats.size(); ++i)
            {
                blends.push_back(
                    VkPipelineColorBlendAttachmentState {
                        .blendEnable = VK_FALSE,
                        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
                        .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
                        .colorBlendOp = VK_BLEND_OP_ADD,
                        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
                        .alphaBlendOp = VK_BLEND_OP_ADD,
                        .colorWriteMask = kRgbaWrite,
                    }
                );
            }
        }
        const VkPipelineColorBlendStateCreateInfo cb {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .logicOpEnable = VK_FALSE,
            .logicOp = VK_LOGIC_OP_COPY,
            .attachmentCount = static_cast<std::uint32_t>(blends.size()),
            .pAttachments = blends.empty() ? nullptr : blends.data(),
            .blendConstants = { 0.0F, 0.0F, 0.0F, 0.0F },
        };

        // --- Dynamic state -------------------------------------------------
        constexpr std::array<VkDynamicState, 2> kDynamic { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        const VkPipelineDynamicStateCreateInfo dyn {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .dynamicStateCount = static_cast<std::uint32_t>(kDynamic.size()),
            .pDynamicStates = kDynamic.data(),
        };

        // --- Dynamic rendering (Vulkan 1.3 core; no VkRenderPass needed) ---
        std::vector<VkFormat> color_formats;
        color_formats.reserve(desc.color_attachment_formats.size());
        for (auto f : desc.color_attachment_formats)
            color_formats.push_back(map_format(f));
        const VkPipelineRenderingCreateInfo rendering {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .pNext = nullptr,
            .viewMask = 0,
            .colorAttachmentCount = static_cast<std::uint32_t>(color_formats.size()),
            .pColorAttachmentFormats = color_formats.empty() ? nullptr : color_formats.data(),
            .depthAttachmentFormat = map_format(desc.depth_attachment_format),
            .stencilAttachmentFormat = map_format(desc.stencil_attachment_format),
        };

        const VkGraphicsPipelineCreateInfo pci {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &rendering,
            .flags = 0,
            .stageCount = static_cast<std::uint32_t>(stages.size()),
            .pStages = stages.data(),
            .pVertexInputState = &vi,
            .pInputAssemblyState = &ia,
            .pTessellationState = nullptr,
            .pViewportState = &vp,
            .pRasterizationState = &rs,
            .pMultisampleState = &ms,
            .pDepthStencilState = &ds,
            .pColorBlendState = &cb,
            .pDynamicState = &dyn,
            .layout = layout_it->second,
            .renderPass = VK_NULL_HANDLE,  // dynamic rendering — no render pass
            .subpass = 0,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = -1,
        };

        VkPipeline pipeline {};
        if (vkCreateGraphicsPipelines(device_, pipeline_cache_, 1, &pci, nullptr, &pipeline) != VK_SUCCESS)
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kResourceCreationFailed, "vkCreateGraphicsPipelines failed")
            );
        }
        const auto id = next_id_++;
        graphics_pipelines_.emplace(id, pipeline);
        pipeline_to_layout_.emplace(id, layout_it->second);
        return cd::rhi::GraphicsPipelineHandle { id, 1u };
    }

    void destroy_graphics_pipeline(cd::rhi::GraphicsPipelineHandle h) override
    {
        if (auto it = graphics_pipelines_.find(h.index()); it != graphics_pipelines_.end())
        {
            vkDestroyPipeline(device_, it->second, nullptr);
            graphics_pipelines_.erase(it);
        }
        pipeline_to_layout_.erase(h.index());
    }

    // --- Compute pipeline (S3.4) ----------------------------------------
    [[nodiscard]] cd::core::Result<cd::rhi::ComputePipelineHandle>
    create_compute_pipeline(const cd::rhi::ComputePipelineDesc& desc) override
    {
        if (!desc.shader.is_valid())
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "compute pipeline: shader required")
            );
        }
        if (!desc.layout.is_valid())
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "compute pipeline: pipeline layout required")
            );
        }
        auto shader_it = shaders_.find(desc.shader.index());
        auto layout_it = pipeline_layouts_.find(desc.layout.index());
        if (shader_it == shaders_.end() || layout_it == pipeline_layouts_.end())
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "compute pipeline: unknown shader/layout handle")
            );
        }
        const VkPipelineShaderStageCreateInfo stage {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = shader_it->second,
            .pName = "main",
            .pSpecializationInfo = nullptr,
        };
        const VkComputePipelineCreateInfo ci {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = stage,
            .layout = layout_it->second,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = -1,
        };
        VkPipeline pipeline {};
        if (vkCreateComputePipelines(device_, pipeline_cache_, 1, &ci, nullptr, &pipeline) != VK_SUCCESS)
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kResourceCreationFailed, "vkCreateComputePipelines failed")
            );
        }
        const auto id = next_id_++;
        compute_pipelines_.emplace(id, pipeline);
        pipeline_to_layout_.emplace(id, layout_it->second);
        return cd::rhi::ComputePipelineHandle { id, 1u };
    }

    void destroy_compute_pipeline(cd::rhi::ComputePipelineHandle h) override
    {
        if (auto it = compute_pipelines_.find(h.index()); it != compute_pipelines_.end())
        {
            vkDestroyPipeline(device_, it->second, nullptr);
            compute_pipelines_.erase(it);
        }
        pipeline_to_layout_.erase(h.index());
    }

    // ---- Pipeline cache (V-PIPECACHE) -----------------------------------
    //
    // Serialise the live VkPipelineCache via vkGetPipelineCacheData. The blob
    // is the Vulkan-defined pipeline-cache format: an 8-byte+ header (length /
    // version / vendorID / deviceID / UUID) followed by driver payload. A
    // matching device validates the header on seed and rejects a mismatch,
    // falling back to an empty cache — so the blob is safe to hand to any device
    // (a stale one is just ignored, never a crash).
    [[nodiscard]] std::vector<std::byte> get_pipeline_cache_data() const override
    {
        if (pipeline_cache_ == VK_NULL_HANDLE)
            return {};
        std::size_t sz = 0;
        if (vkGetPipelineCacheData(device_, pipeline_cache_, &sz, nullptr) != VK_SUCCESS || sz == 0)
            return {};
        std::vector<std::byte> blob(sz);
        if (vkGetPipelineCacheData(device_, pipeline_cache_, &sz, blob.data()) != VK_SUCCESS)
            return {};
        blob.resize(sz);  // driver may report a smaller final size
        return blob;
    }

    // Merge an additional blob into the live cache via vkMergePipelineCaches
    // (route through a temporary source cache built from the blob). A blob from
    // a different driver/GPU fails vkCreatePipelineCache and is harmlessly
    // skipped — the live cache is untouched and we still return kOk.
    [[nodiscard]] cd::core::Result<void>
    load_pipeline_cache(std::span<const std::byte> blob) override
    {
        if (pipeline_cache_ == VK_NULL_HANDLE || blob.empty())
            return {};
        VkPipelineCacheCreateInfo ci {};
        ci.sType           = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        ci.initialDataSize = blob.size();
        ci.pInitialData    = blob.data();
        VkPipelineCache src { VK_NULL_HANDLE };
        if (vkCreatePipelineCache(device_, &ci, nullptr, &src) != VK_SUCCESS || src == VK_NULL_HANDLE)
            return {};  // stale/foreign blob — ignore, never fail
        (void)vkMergePipelineCaches(device_, pipeline_cache_, 1, &src);
        vkDestroyPipelineCache(device_, src, nullptr);
        return {};
    }

    // --- Mesh-shader pipeline (Phase 765 W2A — F5) -----------------------
    //
    // Builds a Vulkan VkPipeline whose stage list contains an optional task
    // shader, a mesh shader, and a fragment shader -- no vertex-input or
    // input-assembly state (VK_EXT_mesh_shader replaces both with the
    // task/mesh dispatch). Stored in `graphics_pipelines_` so the existing
    // bind_graphics_pipeline + pipeline_to_layout machinery transparently
    // handles mesh PSOs at command-buffer record time.
    [[nodiscard]] cd::core::Result<cd::rhi::GraphicsPipelineHandle>
    create_mesh_pipeline(const cd::rhi::MeshPipelineDesc& desc) override
    {
        if (!features_.mesh_shader)
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kNotImplemented,
                         "mesh pipeline: device has no VK_EXT_mesh_shader support")
            );
        }
        if (!desc.mesh_shader.is_valid())
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "mesh pipeline: mesh shader required")
            );
        }
        if (!desc.layout.is_valid())
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "mesh pipeline: pipeline layout required")
            );
        }
        auto ms_it = shaders_.find(desc.mesh_shader.index());
        auto layout_it = pipeline_layouts_.find(desc.layout.index());
        if (ms_it == shaders_.end() || layout_it == pipeline_layouts_.end())
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "mesh pipeline: unknown shader/layout handle")
            );
        }

        // --- Shader stages --------------------------------------------------
        std::vector<VkPipelineShaderStageCreateInfo> stages;
        stages.reserve(3);
        if (desc.task_shader.is_valid())
        {
            auto ts_it = shaders_.find(desc.task_shader.index());
            if (ts_it == shaders_.end())
            {
                return std::unexpected(make_err(
                    cd::rhi::rhi_errors::Code::kInvalidArgument,
                    "mesh pipeline: unknown task shader handle"
                ));
            }
            stages.push_back(
                VkPipelineShaderStageCreateInfo {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = 0,
                    .stage = VK_SHADER_STAGE_TASK_BIT_EXT,
                    .module = ts_it->second,
                    .pName = "main",
                    .pSpecializationInfo = nullptr,
                }
            );
        }
        stages.push_back(
            VkPipelineShaderStageCreateInfo {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .stage = VK_SHADER_STAGE_MESH_BIT_EXT,
                .module = ms_it->second,
                .pName = "main",
                .pSpecializationInfo = nullptr,
            }
        );
        if (desc.fragment_shader.is_valid())
        {
            auto fs_it = shaders_.find(desc.fragment_shader.index());
            if (fs_it == shaders_.end())
            {
                return std::unexpected(make_err(
                    cd::rhi::rhi_errors::Code::kInvalidArgument,
                    "mesh pipeline: unknown fragment shader handle"
                ));
            }
            stages.push_back(
                VkPipelineShaderStageCreateInfo {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = 0,
                    .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .module = fs_it->second,
                    .pName = "main",
                    .pSpecializationInfo = nullptr,
                }
            );
        }

        // --- Viewport / scissor: dynamic (set per command buffer) -----------
        const VkPipelineViewportStateCreateInfo vp {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .viewportCount = 1,
            .pViewports = nullptr,
            .scissorCount = 1,
            .pScissors = nullptr,
        };

        // --- Rasterization (same mapping as graphics pipeline) --------------
        const VkPipelineRasterizationStateCreateInfo rs {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthClampEnable = desc.raster.depth_clamp ? VK_TRUE : VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = map_polygon_mode(desc.raster.polygon_mode),
            .cullMode = map_cull_mode(desc.raster.cull),
            .frontFace = map_front_face(desc.raster.front_face),
            .depthBiasEnable = desc.raster.depth_bias_enable ? VK_TRUE : VK_FALSE,
            .depthBiasConstantFactor = desc.raster.depth_bias_constant,
            .depthBiasClamp = 0.0F,
            .depthBiasSlopeFactor = desc.raster.depth_bias_slope,
            .lineWidth = desc.raster.line_width,
        };

        // --- Multisample ----------------------------------------------------
        const VkPipelineMultisampleStateCreateInfo ms {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .rasterizationSamples = map_samples(desc.samples),
            .sampleShadingEnable = VK_FALSE,
            .minSampleShading = 0.0F,
            .pSampleMask = nullptr,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE,
        };

        // --- Depth/stencil --------------------------------------------------
        const bool has_depth_attach = desc.depth_attachment_format != cd::rhi::Format::kUndefined;
        const bool has_stencil_attach = desc.stencil_attachment_format != cd::rhi::Format::kUndefined;
        const VkPipelineDepthStencilStateCreateInfo ds {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthTestEnable = (desc.depth_stencil.depth_test && has_depth_attach) ? VK_TRUE : VK_FALSE,
            .depthWriteEnable = (desc.depth_stencil.depth_write && has_depth_attach) ? VK_TRUE : VK_FALSE,
            .depthCompareOp = map_compare(desc.depth_stencil.depth_compare),
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = (desc.depth_stencil.stencil_test && has_stencil_attach) ? VK_TRUE : VK_FALSE,
            .front = {},
            .back = {},
            .minDepthBounds = 0.0F,
            .maxDepthBounds = 1.0F,
        };

        // --- Blend per attachment (same opaque fallback as graphics) --------
        std::vector<VkPipelineColorBlendAttachmentState> blends;
        if (!desc.blend_attachments.empty())
        {
            blends.reserve(desc.blend_attachments.size());
            for (const auto& b : desc.blend_attachments)
            {
                blends.push_back(
                    VkPipelineColorBlendAttachmentState {
                        .blendEnable = b.blend_enable ? VK_TRUE : VK_FALSE,
                        .srcColorBlendFactor = map_blend_factor(b.src_color),
                        .dstColorBlendFactor = map_blend_factor(b.dst_color),
                        .colorBlendOp = map_blend_op(b.color_op),
                        .srcAlphaBlendFactor = map_blend_factor(b.src_alpha),
                        .dstAlphaBlendFactor = map_blend_factor(b.dst_alpha),
                        .alphaBlendOp = map_blend_op(b.alpha_op),
                        .colorWriteMask = b.color_write_mask,
                    }
                );
            }
        }
        else
        {
            blends.reserve(desc.color_attachment_formats.size());
            constexpr VkColorComponentFlags kRgbaWrite = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            for (std::size_t i = 0; i < desc.color_attachment_formats.size(); ++i)
            {
                blends.push_back(
                    VkPipelineColorBlendAttachmentState {
                        .blendEnable = VK_FALSE,
                        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
                        .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
                        .colorBlendOp = VK_BLEND_OP_ADD,
                        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
                        .alphaBlendOp = VK_BLEND_OP_ADD,
                        .colorWriteMask = kRgbaWrite,
                    }
                );
            }
        }
        const VkPipelineColorBlendStateCreateInfo cb {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .logicOpEnable = VK_FALSE,
            .logicOp = VK_LOGIC_OP_COPY,
            .attachmentCount = static_cast<std::uint32_t>(blends.size()),
            .pAttachments = blends.empty() ? nullptr : blends.data(),
            .blendConstants = { 0.0F, 0.0F, 0.0F, 0.0F },
        };

        // --- Dynamic state -------------------------------------------------
        constexpr std::array<VkDynamicState, 2> kDynamic { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        const VkPipelineDynamicStateCreateInfo dyn {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .dynamicStateCount = static_cast<std::uint32_t>(kDynamic.size()),
            .pDynamicStates = kDynamic.data(),
        };

        // --- Dynamic rendering (no VkRenderPass) ---------------------------
        std::vector<VkFormat> color_formats;
        color_formats.reserve(desc.color_attachment_formats.size());
        for (auto f : desc.color_attachment_formats)
            color_formats.push_back(map_format(f));
        const VkPipelineRenderingCreateInfo rendering {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .pNext = nullptr,
            .viewMask = 0,
            .colorAttachmentCount = static_cast<std::uint32_t>(color_formats.size()),
            .pColorAttachmentFormats = color_formats.empty() ? nullptr : color_formats.data(),
            .depthAttachmentFormat = map_format(desc.depth_attachment_format),
            .stencilAttachmentFormat = map_format(desc.stencil_attachment_format),
        };

        // Mesh pipelines must NOT supply pVertexInputState / pInputAssemblyState.
        // The Vulkan spec explicitly forbids vertex-input state when the stage
        // list contains MESH_BIT_EXT (VUID-VkGraphicsPipelineCreateInfo-pStages-02095).
        const VkGraphicsPipelineCreateInfo pci {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &rendering,
            .flags = 0,
            .stageCount = static_cast<std::uint32_t>(stages.size()),
            .pStages = stages.data(),
            .pVertexInputState = nullptr,
            .pInputAssemblyState = nullptr,
            .pTessellationState = nullptr,
            .pViewportState = &vp,
            .pRasterizationState = &rs,
            .pMultisampleState = &ms,
            .pDepthStencilState = &ds,
            .pColorBlendState = &cb,
            .pDynamicState = &dyn,
            .layout = layout_it->second,
            .renderPass = VK_NULL_HANDLE,
            .subpass = 0,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = -1,
        };

        VkPipeline pipeline {};
        if (vkCreateGraphicsPipelines(device_, pipeline_cache_, 1, &pci, nullptr, &pipeline) != VK_SUCCESS)
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kResourceCreationFailed, "vkCreateGraphicsPipelines (mesh) failed")
            );
        }
        const auto id = next_id_++;
        // Share the graphics_pipelines_ + pipeline_to_layout_ maps with the
        // classic graphics path: mesh pipelines bind via vkCmdBindPipeline at
        // VK_PIPELINE_BIND_POINT_GRAPHICS exactly like a vertex/fragment PSO,
        // so reusing bind_graphics_pipeline + bind_descriptor_set keeps the
        // command-buffer hot path uniform.
        graphics_pipelines_.emplace(id, pipeline);
        pipeline_to_layout_.emplace(id, layout_it->second);
        return cd::rhi::GraphicsPipelineHandle { id, 1u };
    }

    // --- Descriptor set allocation (S3.4) -------------------------------
    [[nodiscard]] cd::core::Result<cd::rhi::DescriptorSetHandle>
    allocate_descriptor_set(cd::rhi::DescriptorSetLayoutHandle layout) override
    {
        auto layout_it = set_layouts_.find(layout.index());
        if (layout_it == set_layouts_.end())
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "allocate_descriptor_set: unknown layout")
            );
        }
        if (descriptor_pool_ == VK_NULL_HANDLE)
        {
            // v1 bindful: one persistent pool with generous per-type capacities.
            // A2 A6: per-frame ring + auto-grow is the S3.5 evolution; v1 is
            // sized to cover a "typical" small engine scene without overflow.
            // phase851-W8-BE-pool-bump: COMBINED_IMAGE_SAMPLER bumped from
            // 1024 to 65536 because the bindless texture-array binding
            // (binding 13 on the prim material layout) allocates a full
            // 256-slot array PER descriptor set. With ~103 Sponza per-
            // prim sets, that alone needs 103 * 256 = 26368 descriptors
            // of this type. The previous 1024 ceiling was silently
            // exceeding pool capacity for sets allocated after the
            // first ~4, manifesting as device-lost a few frames later
            // when the shader read garbage descriptors. UPDATE_AFTER_BIND
            // pools allocate eagerly per spec, so the pool size IS the
            // hard ceiling. Other sizes bumped proportionally for
            // headroom.
            const std::array<VkDescriptorPoolSize, 7> sizes {
                VkDescriptorPoolSize { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         2048U  },
                VkDescriptorPoolSize { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         2048U  },
                VkDescriptorPoolSize { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          4096U  },
                VkDescriptorPoolSize { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1024U  },
                VkDescriptorPoolSize { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 65536U },
                VkDescriptorPoolSize { VK_DESCRIPTOR_TYPE_SAMPLER,                256U   },
                VkDescriptorPoolSize { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,       128U   },
            };
            const VkDescriptorPoolCreateInfo pci {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .pNext = nullptr,
                // FREE_DESCRIPTOR_SET_BIT lets callers release individual sets via
                // destroy_descriptor_set; A6 warned this can hurt ARM mobile drivers.
                // We accept the cost in exchange for the simpler lifecycle in v1.
                //
                // phase842-W8-BE-rt-bindless-texture-sampling: add the
                // UPDATE_AFTER_BIND_POOL flag so this shared pool can
                // allocate descriptor sets whose layout was created
                // with the UPDATE_AFTER_BIND_POOL_BIT layout flag (the
                // bindless texture-array case). Non-bindless sets are
                // unaffected; UPDATE_AFTER_BIND is opt-in per layout
                // and per binding.
                .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT
                       | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
                .maxSets = 4096U,
                .poolSizeCount = static_cast<std::uint32_t>(sizes.size()),
                .pPoolSizes = sizes.data(),
            };
            if (vkCreateDescriptorPool(device_, &pci, nullptr, &descriptor_pool_) != VK_SUCCESS)
            {
                return std::unexpected(
                    make_err(cd::rhi::rhi_errors::Code::kResourceCreationFailed, "vkCreateDescriptorPool failed")
                );
            }
        }
        // phase843-W8-BE: chain VariableDescriptorCountAllocateInfo
        // when the layout has a variable-count trailing binding.
        VkDescriptorSetVariableDescriptorCountAllocateInfo var_count_ci {};
        const void* alloc_pNext = nullptr;
        if (auto meta_it = set_layout_meta_.find(layout.index());
            meta_it != set_layout_meta_.end() &&
            meta_it->second.variable_count_max > 0u)
        {
            var_count_ci.sType =
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
            var_count_ci.descriptorSetCount = 1u;
            var_count_ci.pDescriptorCounts  = &meta_it->second.variable_count_max;
            alloc_pNext = &var_count_ci;
        }
        const VkDescriptorSetAllocateInfo ai {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext = alloc_pNext,
            .descriptorPool = descriptor_pool_,
            .descriptorSetCount = 1,
            .pSetLayouts = &layout_it->second,
        };
        VkDescriptorSet set {};
        if (vkAllocateDescriptorSets(device_, &ai, &set) != VK_SUCCESS)
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kResourceCreationFailed, "vkAllocateDescriptorSets failed")
            );
        }
        const auto id = next_id_++;
        descriptor_sets_.emplace(id, set);
        return cd::rhi::DescriptorSetHandle { id, 1u };
    }

    void destroy_descriptor_set(cd::rhi::DescriptorSetHandle h) override
    {
        if (auto it = descriptor_sets_.find(h.index()); it != descriptor_sets_.end())
        {
            vkFreeDescriptorSets(device_, descriptor_pool_, 1, &it->second);
            descriptor_sets_.erase(it);
        }
    }

    [[nodiscard]] cd::core::Result<void>
    update_descriptor_set(cd::rhi::DescriptorSetHandle set, std::span<const cd::rhi::DescriptorWrite> writes) override
    {
        auto set_it = descriptor_sets_.find(set.index());
        if (set_it == descriptor_sets_.end())
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "update_descriptor_set: unknown set")
            );
        }
        if (writes.empty())
            return {};

        // Backing arrays survive the vkUpdateDescriptorSets call: descriptors
        // hold pointers into them so we cannot let them go out of scope mid-call.
        std::vector<VkDescriptorBufferInfo> buffer_infos;
        std::vector<VkDescriptorImageInfo> image_infos;
        buffer_infos.reserve(writes.size());
        image_infos.reserve(writes.size());

        std::vector<VkWriteDescriptorSet> vk_writes;
        vk_writes.reserve(writes.size());

        // Acceleration-structure writes need a chained
        // VkWriteDescriptorSetAccelerationStructureKHR plus storage for
        // the VkAccelerationStructureKHR pointer that struct points at.
        // The pNext payload addresses must stay live until
        // vkUpdateDescriptorSets returns, so we own both vectors here.
        std::vector<VkWriteDescriptorSetAccelerationStructureKHR> accel_writes;
        accel_writes.reserve(writes.size());
        std::vector<VkAccelerationStructureKHR>                   accel_handles;
        accel_handles.reserve(writes.size());

        for (const auto& w : writes)
        {
            const VkDescriptorType vk_type = map_descriptor_type(w.type);
            VkWriteDescriptorSet entry {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext = nullptr,
                .dstSet = set_it->second,
                .dstBinding = w.binding,
                .dstArrayElement = w.array_element,
                .descriptorCount = 1,
                .descriptorType = vk_type,
                .pImageInfo = nullptr,
                .pBufferInfo = nullptr,
                .pTexelBufferView = nullptr,
            };

            switch (w.type)
            {
                case cd::rhi::DescriptorType::kUniformBuffer:
                case cd::rhi::DescriptorType::kStorageBuffer:
                case cd::rhi::DescriptorType::kUniformBufferDynamic:
                case cd::rhi::DescriptorType::kStorageBufferDynamic:
                {
                    auto buf_it = buffers_.find(w.buffer.index());
                    if (buf_it == buffers_.end())
                    {
                        return std::unexpected(make_err(
                            cd::rhi::rhi_errors::Code::kInvalidArgument,
                            "update_descriptor_set: unknown buffer"
                        ));
                    }
                    buffer_infos.push_back(
                        VkDescriptorBufferInfo {
                            .buffer = buf_it->second,
                            .offset = w.buffer_offset,
                            .range = w.buffer_range == 0 ? VK_WHOLE_SIZE : w.buffer_range,
                        }
                    );
                    entry.pBufferInfo = &buffer_infos.back();
                    break;
                }
                case cd::rhi::DescriptorType::kSampledImage:
                case cd::rhi::DescriptorType::kStorageImage:
                case cd::rhi::DescriptorType::kInputAttachment:
                {
                    auto view_it = views_.find(w.view.index());
                    if (view_it == views_.end())
                    {
                        return std::unexpected(
                            make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "update_descriptor_set: unknown view")
                        );
                    }
                    const VkImageLayout layout = w.type == cd::rhi::DescriptorType::kStorageImage
                                                     ? VK_IMAGE_LAYOUT_GENERAL
                                                     : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    image_infos.push_back(
                        VkDescriptorImageInfo {
                            .sampler = VK_NULL_HANDLE,
                            .imageView = view_it->second,
                            .imageLayout = layout,
                        }
                    );
                    entry.pImageInfo = &image_infos.back();
                    break;
                }
                case cd::rhi::DescriptorType::kCombinedImageSampler:
                {
                    auto view_it = views_.find(w.view.index());
                    auto smp_it = samplers_.find(w.sampler.index());
                    if (view_it == views_.end() || smp_it == samplers_.end())
                    {
                        return std::unexpected(make_err(
                            cd::rhi::rhi_errors::Code::kInvalidArgument,
                            "update_descriptor_set: unknown view or sampler"
                        ));
                    }
                    image_infos.push_back(
                        VkDescriptorImageInfo {
                            .sampler = smp_it->second,
                            .imageView = view_it->second,
                            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        }
                    );
                    entry.pImageInfo = &image_infos.back();
                    break;
                }
                case cd::rhi::DescriptorType::kSampler:
                {
                    auto smp_it = samplers_.find(w.sampler.index());
                    if (smp_it == samplers_.end())
                    {
                        return std::unexpected(make_err(
                            cd::rhi::rhi_errors::Code::kInvalidArgument,
                            "update_descriptor_set: unknown sampler"
                        ));
                    }
                    image_infos.push_back(
                        VkDescriptorImageInfo {
                            .sampler = smp_it->second,
                            .imageView = VK_NULL_HANDLE,
                            .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                        }
                    );
                    entry.pImageInfo = &image_infos.back();
                    break;
                }
                case cd::rhi::DescriptorType::kAccelerationStructure:
                {
                    auto as_it = accels_.find(w.accel.index());
                    if (as_it == accels_.end())
                    {
                        return std::unexpected(make_err(
                            cd::rhi::rhi_errors::Code::kInvalidArgument,
                            "update_descriptor_set: unknown acceleration structure"
                        ));
                    }
                    accel_handles.push_back(as_it->second.as);
                    accel_writes.push_back(VkWriteDescriptorSetAccelerationStructureKHR {
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
                        .pNext = nullptr,
                        .accelerationStructureCount = 1,
                        .pAccelerationStructures = &accel_handles.back(),
                    });
                    entry.pNext = &accel_writes.back();
                    break;
                }
                case cd::rhi::DescriptorType::kBindlessSampledImage:
                {
                    // phase843-W8-BE: write one slot of the bindless
                    // sampler2D array. `array_element` is the slot
                    // index in the array; `view` + `sampler` are the
                    // texture-side handles. Falls back to LOUD failure
                    // when either is missing (a misconfigured caller
                    // would otherwise silently write a black sampled
                    // image into the chrome reflection branch).
                    auto view_it = views_.find(w.view.index());
                    auto smp_it  = samplers_.find(w.sampler.index());
                    if (view_it == views_.end() || smp_it == samplers_.end())
                    {
                        return std::unexpected(make_err(
                            cd::rhi::rhi_errors::Code::kInvalidArgument,
                            "update_descriptor_set: bindless write needs both view + sampler"
                        ));
                    }
                    image_infos.push_back(
                        VkDescriptorImageInfo {
                            .sampler     = smp_it->second,
                            .imageView   = view_it->second,
                            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        }
                    );
                    entry.pImageInfo    = &image_infos.back();
                    entry.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    break;
                }
            }
            vk_writes.push_back(entry);
        }

        vkUpdateDescriptorSets(device_, static_cast<std::uint32_t>(vk_writes.size()), vk_writes.data(), 0, nullptr);
        return {};
    }

    // --- Synchronization primitives (S3.5) ------------------------------
    [[nodiscard]] cd::core::Result<cd::rhi::SemaphoreHandle> create_semaphore() override
    {
        const VkSemaphoreCreateInfo ci {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
        };
        VkSemaphore s {};
        if (vkCreateSemaphore(device_, &ci, nullptr, &s) != VK_SUCCESS)
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kResourceCreationFailed, "vkCreateSemaphore failed")
            );
        }
        const auto id = next_id_++;
        semaphores_.emplace(id, s);
        return cd::rhi::SemaphoreHandle { id, 1u };
    }

    void destroy_semaphore(cd::rhi::SemaphoreHandle h) override
    {
        if (auto it = semaphores_.find(h.index()); it != semaphores_.end())
        {
            vkDestroySemaphore(device_, it->second, nullptr);
            semaphores_.erase(it);
        }
    }

    [[nodiscard]] cd::core::Result<cd::rhi::FenceHandle> create_fence(bool signaled) override
    {
        const VkFenceCreateInfo ci {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = signaled ? VkFenceCreateFlags { VK_FENCE_CREATE_SIGNALED_BIT } : VkFenceCreateFlags { 0 },
        };
        VkFence f {};
        if (vkCreateFence(device_, &ci, nullptr, &f) != VK_SUCCESS)
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kResourceCreationFailed, "vkCreateFence failed")
            );
        }
        const auto id = next_id_++;
        fences_.emplace(id, f);
        return cd::rhi::FenceHandle { id, 1u };
    }

    void destroy_fence(cd::rhi::FenceHandle h) override
    {
        if (auto it = fences_.find(h.index()); it != fences_.end())
        {
            vkDestroyFence(device_, it->second, nullptr);
            fences_.erase(it);
        }
    }

    [[nodiscard]] cd::core::Result<void> wait_for_fence(cd::rhi::FenceHandle h, std::uint64_t timeout_ns) override
    {
        auto it = fences_.find(h.index());
        if (it == fences_.end())
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "wait_for_fence: unknown fence")
            );
        }
        const VkResult r = vkWaitForFences(device_, 1, &it->second, VK_TRUE, timeout_ns);
        if (r == VK_TIMEOUT)
        {
            return std::unexpected(make_err(cd::rhi::rhi_errors::Code::kTimeout, "wait_for_fence: deadline exceeded"));
        }
        if (r != VK_SUCCESS)
        {
            return std::unexpected(make_err(cd::rhi::rhi_errors::Code::kDeviceLost, "vkWaitForFences failed"));
        }
        return {};
    }

    void reset_fence(cd::rhi::FenceHandle h) override
    {
        if (auto it = fences_.find(h.index()); it != fences_.end())
        {
            vkResetFences(device_, 1, &it->second);
        }
    }

    [[nodiscard]] bool is_fence_signaled(cd::rhi::FenceHandle h) override
    {
        auto it = fences_.find(h.index());
        if (it == fences_.end())
            return false;
        return vkGetFenceStatus(device_, it->second) == VK_SUCCESS;
    }

    // --- GPU query pool (A-QUERY, Backend-to-100 Wave 3a) ----------------
    [[nodiscard]] cd::core::Result<cd::rhi::QueryPoolHandle>
    create_query_pool(const cd::rhi::QueryPoolDesc& desc) override
    {
        if (desc.count == 0)
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "create_query_pool: count == 0"));
        }
        VkQueryType vk_type { VK_QUERY_TYPE_TIMESTAMP };
        VkQueryPipelineStatisticFlags stat_flags { 0 };
        switch (desc.type)
        {
            case cd::rhi::QueryType::kTimestamp:
                vk_type = VK_QUERY_TYPE_TIMESTAMP;
                break;
            case cd::rhi::QueryType::kOcclusion:
                vk_type = VK_QUERY_TYPE_OCCLUSION;
                break;
            case cd::rhi::QueryType::kPipelineStatistics:
                vk_type = VK_QUERY_TYPE_PIPELINE_STATISTICS;
                // One counter (input-assembly vertices) is enough to make the
                // pool valid; callers reading pipeline-statistics interpret the
                // single u64 result accordingly.
                stat_flags = VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT;
                break;
        }
        const VkQueryPoolCreateInfo ci {
            .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queryType = vk_type,
            .queryCount = desc.count,
            .pipelineStatistics = stat_flags,
        };
        VkQueryPool pool { VK_NULL_HANDLE };
        if (vkCreateQueryPool(device_, &ci, nullptr, &pool) != VK_SUCCESS)
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kResourceCreationFailed, "vkCreateQueryPool failed"));
        }
        const auto id = next_id_++;
        query_pools_.emplace(id, QueryPoolRecord { pool, desc.type, desc.count });
        query_pool_handles_.emplace(id, pool);
        return cd::rhi::QueryPoolHandle { id, 1u };
    }

    void destroy_query_pool(cd::rhi::QueryPoolHandle h) override
    {
        if (auto it = query_pools_.find(h.index()); it != query_pools_.end())
        {
            vkDestroyQueryPool(device_, it->second.pool, nullptr);
            query_pools_.erase(it);
            query_pool_handles_.erase(h.index());
        }
    }

    [[nodiscard]] cd::core::Result<void>
    get_query_results(cd::rhi::QueryPoolHandle pool,
                      std::uint32_t first,
                      std::uint32_t count,
                      std::span<std::uint64_t> out) override
    {
        auto it = query_pools_.find(pool.index());
        if (it == query_pools_.end())
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "get_query_results: unknown pool"));
        }
        const auto& rec = it->second;
        if (count == 0 || first + count > rec.count || out.size() < count)
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "get_query_results: out-of-range / undersized"));
        }
        std::vector<std::uint64_t> raw(count, 0);
        const VkResult r = vkGetQueryPoolResults(
            device_, rec.pool, first, count,
            static_cast<std::size_t>(count) * sizeof(std::uint64_t),
            raw.data(), sizeof(std::uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        if (r != VK_SUCCESS)
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kDeviceLost, "vkGetQueryPoolResults failed"));
        }
        // Timestamps are reported in GPU ticks; scale to nanoseconds via the
        // device's timestamp period. Occlusion / pipeline-statistics counters
        // are passed through verbatim.
        for (std::uint32_t i = 0; i < count; ++i)
        {
            out[i] = (rec.type == cd::rhi::QueryType::kTimestamp)
                         ? static_cast<std::uint64_t>(
                               static_cast<double>(raw[i]) * static_cast<double>(timestamp_period_ns_))
                         : raw[i];
        }
        return {};
    }

    // --- Timeline semaphores (Vulkan 1.2+ core) -------------------------
    [[nodiscard]] cd::core::Result<cd::rhi::TimelineSemaphoreHandle>
    create_timeline_semaphore(std::uint64_t initial_value) override
    {
        VkSemaphoreTypeCreateInfo type_ci {};
        type_ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        type_ci.pNext = nullptr;
        type_ci.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        type_ci.initialValue = initial_value;

        VkSemaphoreCreateInfo ci {};
        ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        ci.pNext = &type_ci;
        ci.flags = 0;

        VkSemaphore sem { VK_NULL_HANDLE };
        if (vkCreateSemaphore(device_, &ci, nullptr, &sem) != VK_SUCCESS)
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kResourceCreationFailed, "vkCreateSemaphore (timeline) failed")
            );
        }
        const auto id = next_id_++;
        timeline_semaphores_.emplace(id, sem);
        return cd::rhi::TimelineSemaphoreHandle { id, 1u };
    }

    void destroy_timeline_semaphore(cd::rhi::TimelineSemaphoreHandle h) override
    {
        if (auto it = timeline_semaphores_.find(h.index()); it != timeline_semaphores_.end())
        {
            vkDestroySemaphore(device_, it->second, nullptr);
            timeline_semaphores_.erase(it);
        }
    }

    [[nodiscard]] cd::core::Result<void>
    wait_timeline_semaphore(cd::rhi::TimelineSemaphoreHandle h, std::uint64_t value, std::uint64_t timeout_ns) override
    {
        auto it = timeline_semaphores_.find(h.index());
        if (it == timeline_semaphores_.end())
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "wait_timeline_semaphore: unknown handle")
            );
        }
        VkSemaphoreWaitInfo wi {};
        wi.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        wi.pNext = nullptr;
        wi.flags = 0;
        wi.semaphoreCount = 1;
        wi.pSemaphores = &it->second;
        wi.pValues = &value;
        const VkResult r = vkWaitSemaphores(device_, &wi, timeout_ns);
        if (r == VK_TIMEOUT)
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kTimeout, "wait_timeline_semaphore: deadline exceeded")
            );
        }
        if (r != VK_SUCCESS)
        {
            return std::unexpected(make_err(cd::rhi::rhi_errors::Code::kDeviceLost, "vkWaitSemaphores failed"));
        }
        return {};
    }

    [[nodiscard]] cd::core::Result<void>
    signal_timeline_semaphore(cd::rhi::TimelineSemaphoreHandle h, std::uint64_t value) override
    {
        auto it = timeline_semaphores_.find(h.index());
        if (it == timeline_semaphores_.end())
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "signal_timeline_semaphore: unknown handle")
            );
        }
        // Vulkan enforces strict monotonic increase at the driver level, but we
        // pre-check so callers get the typed kInvalidArgument rather than a
        // VK_ERROR_UNKNOWN.
        std::uint64_t current { 0 };
        vkGetSemaphoreCounterValue(device_, it->second, &current);
        if (value <= current)
        {
            return std::unexpected(make_err(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "signal_timeline_semaphore: value must strictly increase"
            ));
        }
        VkSemaphoreSignalInfo si {};
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
        si.pNext = nullptr;
        si.semaphore = it->second;
        si.value = value;
        if (vkSignalSemaphore(device_, &si) != VK_SUCCESS)
        {
            return std::unexpected(make_err(cd::rhi::rhi_errors::Code::kDeviceLost, "vkSignalSemaphore failed"));
        }
        return {};
    }

    [[nodiscard]] std::uint64_t timeline_semaphore_value(cd::rhi::TimelineSemaphoreHandle h) const override
    {
        auto it = timeline_semaphores_.find(h.index());
        if (it == timeline_semaphores_.end())
            return 0U;
        std::uint64_t value { 0 };
        vkGetSemaphoreCounterValue(device_, it->second, &value);
        return value;
    }

    // --- Swapchain acquire / present (S3.5) -----------------------------
    [[nodiscard]] cd::core::Result<std::uint32_t> acquire_next_image(
        cd::rhi::SwapchainHandle sc,
        cd::rhi::SemaphoreHandle signal,
        cd::rhi::FenceHandle fence,
        std::uint64_t timeout_ns
    ) override
    {
        auto sc_it = swapchains_.find(sc.index());
        if (sc_it == swapchains_.end())
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "acquire_next_image: unknown swapchain")
            );
        }
        VkSemaphore sem = VK_NULL_HANDLE;
        if (signal.is_valid())
        {
            auto sem_it = semaphores_.find(signal.index());
            if (sem_it == semaphores_.end())
            {
                return std::unexpected(
                    make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "acquire_next_image: unknown semaphore")
                );
            }
            sem = sem_it->second;
        }
        VkFence fnc = VK_NULL_HANDLE;
        if (fence.is_valid())
        {
            auto fnc_it = fences_.find(fence.index());
            if (fnc_it == fences_.end())
            {
                return std::unexpected(
                    make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "acquire_next_image: unknown fence")
                );
            }
            fnc = fnc_it->second;
        }
        std::uint32_t image_index = 0;
        const VkResult r = vkAcquireNextImageKHR(device_, sc_it->second.swapchain, timeout_ns, sem, fnc, &image_index);
        if (r == VK_TIMEOUT || r == VK_NOT_READY)
        {
            return std::unexpected(make_err(cd::rhi::rhi_errors::Code::kTimeout, "vkAcquireNextImageKHR: timeout"));
        }
        if (r == VK_ERROR_OUT_OF_DATE_KHR)
        {
            return std::unexpected(make_err(cd::rhi::rhi_errors::Code::kSwapchainOutOfDate, "swapchain out of date"));
        }
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
        {
            return std::unexpected(make_err(cd::rhi::rhi_errors::Code::kDeviceLost, "vkAcquireNextImageKHR failed"));
        }
        return image_index;
    }

    [[nodiscard]] cd::core::Result<void> present(
        cd::rhi::SwapchainHandle sc,
        std::uint32_t image_index,
        std::span<const cd::rhi::SemaphoreHandle> wait
    ) override
    {
        auto sc_it = swapchains_.find(sc.index());
        if (sc_it == swapchains_.end())
        {
            return std::unexpected(make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "present: unknown swapchain"));
        }
        if (image_index >= sc_it->second.images.size())
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "present: image index out of range")
            );
        }
        std::vector<VkSemaphore> wait_sems;
        wait_sems.reserve(wait.size());
        for (auto w : wait)
        {
            auto it = semaphores_.find(w.index());
            if (it == semaphores_.end())
            {
                return std::unexpected(
                    make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "present: unknown wait semaphore")
                );
            }
            wait_sems.push_back(it->second);
        }
        const VkPresentInfoKHR pi {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = nullptr,
            .waitSemaphoreCount = static_cast<std::uint32_t>(wait_sems.size()),
            .pWaitSemaphores = wait_sems.empty() ? nullptr : wait_sems.data(),
            .swapchainCount = 1,
            .pSwapchains = &sc_it->second.swapchain,
            .pImageIndices = &image_index,
            .pResults = nullptr,
        };
        // Vulkan V2 (multi-queue model): present on the present queue. When no
        // separate present family was selected (the common case — graphics
        // supports the surface, or no surface was probed at device creation),
        // present_queue_ aliases graphics_queue_ and this is the unchanged path.
        VkQueue present_q = present_queue_ != VK_NULL_HANDLE ? present_queue_ : graphics_queue_;
        const VkResult r = vkQueuePresentKHR(present_q, &pi);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR)
        {
            return std::unexpected(make_err(cd::rhi::rhi_errors::Code::kSwapchainOutOfDate, "swapchain out of date"));
        }
        if (r != VK_SUCCESS)
        {
            return std::unexpected(make_err(cd::rhi::rhi_errors::Code::kDeviceLost, "vkQueuePresentKHR failed"));
        }
        return {};
    }

    [[nodiscard]] cd::rhi::TextureViewHandle
    swapchain_image_view(cd::rhi::SwapchainHandle sc, std::uint32_t image_index) const override
    {
        auto it = swapchains_.find(sc.index());
        if (it == swapchains_.end())
            return cd::rhi::TextureViewHandle {};
        if (image_index >= it->second.view_ids.size())
            return cd::rhi::TextureViewHandle {};
        // Engine-side view ids are emplaced into views_ at swapchain creation
        // (see create_swapchain). begin_render_pass() resolves the handle via
        // the command-buffer ResourceTables.views map → VkImageView.
        return cd::rhi::TextureViewHandle { it->second.view_ids[image_index], 1u };
    }

    [[nodiscard]] std::uint32_t swapchain_image_count(cd::rhi::SwapchainHandle sc) const override
    {
        auto it = swapchains_.find(sc.index());
        return it == swapchains_.end() ? 0U : static_cast<std::uint32_t>(it->second.images.size());
    }

    // --- Swapchain (S3.5) -----------------------------------------------
    [[nodiscard]] cd::core::Result<cd::rhi::SwapchainHandle>
    create_swapchain(const cd::rhi::SwapchainDesc& desc) override
    {
        if (desc.window_handle == nullptr)
        {
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "swapchain: null window handle")
            );
        }
        if (desc.extent.width == 0 || desc.extent.height == 0)
        {
            return std::unexpected(make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "swapchain: zero extent"));
        }

        // 1) Create platform surface. Each path is gated on the matching
        //    VK_USE_PLATFORM_*_KHR define, which the build system sets per OS.
        //    Wayland is preferred over Xlib when both are compiled in (it's the
        //    modern Linux default); the `display_handle` carries the matching
        //    native display pointer.
        VkSurfaceKHR surface = VK_NULL_HANDLE;
#if defined(VK_USE_PLATFORM_WIN32_KHR)
        {
            const VkWin32SurfaceCreateInfoKHR si {
                .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
                .pNext = nullptr,
                .flags = 0,
                .hinstance = static_cast<HINSTANCE>(desc.display_handle),
                .hwnd = static_cast<HWND>(desc.window_handle),
            };
            if (vkCreateWin32SurfaceKHR(inst_->instance, &si, nullptr, &surface) != VK_SUCCESS)
            {
                return std::unexpected(
                    make_err(cd::rhi::rhi_errors::Code::kBackendInitFailed, "vkCreateWin32SurfaceKHR failed")
                );
            }
        }
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
        {
            VkWaylandSurfaceCreateInfoKHR si {};
            si.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
            si.pNext = nullptr;
            si.flags = 0;
            si.display = static_cast<wl_display*>(desc.display_handle);
            si.surface = static_cast<wl_surface*>(desc.window_handle);
            if (vkCreateWaylandSurfaceKHR(inst_->instance, &si, nullptr, &surface) != VK_SUCCESS)
            {
                return std::unexpected(
                    make_err(cd::rhi::rhi_errors::Code::kBackendInitFailed, "vkCreateWaylandSurfaceKHR failed")
                );
            }
        }
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
        {
            VkXlibSurfaceCreateInfoKHR si {};
            si.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
            si.pNext = nullptr;
            si.flags = 0;
            si.dpy = static_cast<Display*>(desc.display_handle);
            // Xlib's `Window` is an XID (unsigned long); SwapchainDesc carries it
            // through void* so we cast through uintptr_t to satisfy strict-aliasing.
            si.window = static_cast<Window>(reinterpret_cast<std::uintptr_t>(desc.window_handle));
            if (vkCreateXlibSurfaceKHR(inst_->instance, &si, nullptr, &surface) != VK_SUCCESS)
            {
                return std::unexpected(
                    make_err(cd::rhi::rhi_errors::Code::kBackendInitFailed, "vkCreateXlibSurfaceKHR failed")
                );
            }
        }
#elif defined(VK_USE_PLATFORM_METAL_EXT)
        {
            VkMetalSurfaceCreateInfoEXT si {};
            si.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
            si.pNext = nullptr;
            si.flags = 0;
            // macOS callers pass a `CAMetalLayer*` through `window_handle`.
            si.pLayer = static_cast<const CAMetalLayer*>(desc.window_handle);
            if (vkCreateMetalSurfaceEXT(inst_->instance, &si, nullptr, &surface) != VK_SUCCESS)
            {
                return std::unexpected(
                    make_err(cd::rhi::rhi_errors::Code::kBackendInitFailed, "vkCreateMetalSurfaceEXT failed")
                );
            }
        }
#else
        // B-PLATFORM-GUARD: correct-by-design compile-time platform guard (the
        // active path here is VK_USE_PLATFORM_WIN32_KHR); not a runtime hole.
        // See ADR-20260616 backend-wontfix.
        return std::unexpected(
            make_err(cd::rhi::rhi_errors::Code::kNotImplemented, "swapchain: no platform surface extension compiled in")
        );
#endif

        // 2) Verify queue supports presentation to this surface.
        VkBool32 present_supported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physical_, graphics_family_, surface, &present_supported);
        if (present_supported == VK_FALSE)
        {
            vkDestroySurfaceKHR(inst_->instance, surface, nullptr);
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kNoSuitableAdapter, "swapchain: graphics queue cannot present")
            );
        }

        // 3) Pick surface format (caller hint, otherwise BGRA8 sRGB if available).
        std::uint32_t fmt_count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface, &fmt_count, nullptr);
        std::vector<VkSurfaceFormatKHR> available_formats(fmt_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface, &fmt_count, available_formats.data());
        const VkFormat hinted = map_format(desc.format);
        // Translate the rhi::ColorSpace enum to a VkColorSpaceKHR. The
        // surface enumeration may not advertise the requested space —
        // we fall back to sRGB nonlinear which every surface supports.
        VkColorSpaceKHR hinted_cs = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        switch (desc.colour_space)
        {
            case cd::rhi::ColorSpace::kHdr10St2084:
                hinted_cs = VK_COLOR_SPACE_HDR10_ST2084_EXT;
                break;
            case cd::rhi::ColorSpace::kScrgbLinear:
                hinted_cs = VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT;
                break;
            case cd::rhi::ColorSpace::kSrgbNonlinear:
            default:
                hinted_cs = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
                break;
        }
        VkSurfaceFormatKHR chosen_format =
            available_formats.empty()
                ? VkSurfaceFormatKHR { VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR }
                : available_formats.front();
        // First pass: exact format + requested colour space.
        bool matched_hdr = false;
        for (const auto& f : available_formats)
        {
            if (f.format == hinted && f.colorSpace == hinted_cs)
            {
                chosen_format = f;
                matched_hdr = (hinted_cs != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
                break;
            }
        }
        // Second pass: any format with the requested colour space.
        if (!matched_hdr && hinted_cs != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            for (const auto& f : available_formats)
            {
                if (f.colorSpace == hinted_cs)
                {
                    chosen_format = f;
                    break;
                }
            }
        }
        // Third pass (SDR fallback) — exact format + sRGB nonlinear.
        if (chosen_format.colorSpace != hinted_cs)
        {
            for (const auto& f : available_formats)
            {
                if (f.format == hinted && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                {
                    chosen_format = f;
                    break;
                }
            }
        }

        // 4) Pick present mode. FIFO is always supported. When vsync is
        //    requested off we prefer IMMEDIATE for a truly uncapped frame
        //    rate (tearing accepted) and fall back to MAILBOX when
        //    IMMEDIATE isn't supported. With vsync on we stay on FIFO.
        //    W5-C: previous build picked MAILBOX exclusively; on 60 Hz
        //    monitors the application loop still capped at ~60 fps because
        //    vkAcquireNextImageKHR blocks once MAILBOX's 3-image queue
        //    fills. IMMEDIATE lifts that ceiling — the FPS-counter readout
        //    is then driven by actual GPU + CPU cost, not display sync.
        std::uint32_t pm_count = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface, &pm_count, nullptr);
        std::vector<VkPresentModeKHR> present_modes(pm_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface, &pm_count, present_modes.data());
        VkPresentModeKHR chosen_pm = VK_PRESENT_MODE_FIFO_KHR;
        if (!desc.vsync)
        {
            bool got_immediate = false;
            for (auto pm : present_modes)
            {
                if (pm == VK_PRESENT_MODE_IMMEDIATE_KHR)
                {
                    chosen_pm = pm;
                    got_immediate = true;
                    break;
                }
            }
            if (!got_immediate)
            {
                for (auto pm : present_modes)
                {
                    if (pm == VK_PRESENT_MODE_MAILBOX_KHR)
                    {
                        chosen_pm = pm;
                        break;
                    }
                }
            }
        }

        // 5) Capabilities + extent.
        VkSurfaceCapabilitiesKHR caps {};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface, &caps);
        VkExtent2D extent = caps.currentExtent;
        if (extent.width == 0xFFFFFFFFu)
        {
            // Driver allows us to choose: clamp the requested extent to caps.
            extent = { desc.extent.width, desc.extent.height };
            extent.width = std::max(extent.width, caps.minImageExtent.width);
            extent.height = std::max(extent.height, caps.minImageExtent.height);
            extent.width = std::min(extent.width, caps.maxImageExtent.width);
            extent.height = std::min(extent.height, caps.maxImageExtent.height);
        }
        std::uint32_t image_count = desc.image_count;
        image_count = std::max(image_count, caps.minImageCount);
        if (caps.maxImageCount != 0 && image_count > caps.maxImageCount)
        {
            image_count = caps.maxImageCount;
        }

        // 6) Create the swapchain.
        const VkSwapchainCreateInfoKHR sci {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .pNext = nullptr,
            .flags = 0,
            .surface = surface,
            .minImageCount = image_count,
            .imageFormat = chosen_format.format,
            .imageColorSpace = chosen_format.colorSpace,
            .imageExtent = extent,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .preTransform = caps.currentTransform,
            .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode = chosen_pm,
            .clipped = VK_TRUE,
            .oldSwapchain = VK_NULL_HANDLE,
        };
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        if (vkCreateSwapchainKHR(device_, &sci, nullptr, &swapchain) != VK_SUCCESS)
        {
            vkDestroySurfaceKHR(inst_->instance, surface, nullptr);
            return std::unexpected(
                make_err(cd::rhi::rhi_errors::Code::kResourceCreationFailed, "vkCreateSwapchainKHR failed")
            );
        }

        // 7) Collect images + create one VkImageView per image.
        std::uint32_t actual_count = 0;
        vkGetSwapchainImagesKHR(device_, swapchain, &actual_count, nullptr);
        std::vector<VkImage> sc_images(actual_count);
        vkGetSwapchainImagesKHR(device_, swapchain, &actual_count, sc_images.data());

        std::vector<VkImageView> sc_views;
        sc_views.reserve(actual_count);
        for (auto img : sc_images)
        {
            const VkImageViewCreateInfo vci {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .image = img,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = chosen_format.format,
                .components = { VK_COMPONENT_SWIZZLE_IDENTITY,
                               VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                               VK_COMPONENT_SWIZZLE_IDENTITY },
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
            };
            VkImageView view = VK_NULL_HANDLE;
            vkCreateImageView(device_, &vci, nullptr, &view);
            sc_views.push_back(view);
        }

        // Allocate engine-side TextureHandle ids for each swapchain image and
        // emplace into images_ so barrier()/begin_render_pass can resolve them.
        // These ids are excluded from the vmaDestroyImage cleanup loop because
        // the swapchain owns the underlying VkImage.
        std::vector<std::uint32_t> image_ids;
        image_ids.reserve(sc_images.size());
        for (auto img : sc_images)
        {
            const auto img_id = next_id_++;
            images_.emplace(img_id, img);
            image_ids.push_back(img_id);
        }

        // Same treatment for the views: every view needs an engine-side
        // TextureViewHandle id so begin_render_pass() can resolve it via the
        // command-buffer ResourceTables.views map. The views themselves are
        // owned by the swapchain (created above, destroyed in destroy_swap-
        // chain); the views_ entries are non-owning aliases excluded from
        // any view-destruction loop in the device dtor.
        std::vector<std::uint32_t> view_ids;
        view_ids.reserve(sc_views.size());
        for (auto v : sc_views)
        {
            const auto view_id = next_id_++;
            views_.emplace(view_id, v);
            view_formats_.emplace(view_id, chosen_format.format);  // phase1116
            view_ids.push_back(view_id);
        }

        SwapchainRecord rec;
        rec.surface = surface;
        rec.swapchain = swapchain;
        rec.images = std::move(sc_images);
        rec.image_ids = std::move(image_ids);
        rec.views = std::move(sc_views);
        rec.view_ids = std::move(view_ids);
        rec.format = chosen_format.format;
        rec.extent = extent;

        const auto id = next_id_++;
        swapchains_.emplace(id, std::move(rec));
        return cd::rhi::SwapchainHandle { id, 1u };
    }

    [[nodiscard]] cd::rhi::TextureHandle
    swapchain_image(cd::rhi::SwapchainHandle s, std::uint32_t image_index) const override
    {
        auto it = swapchains_.find(s.index());
        if (it == swapchains_.end())
            return cd::rhi::TextureHandle {};
        if (image_index >= it->second.image_ids.size())
            return cd::rhi::TextureHandle {};
        return cd::rhi::TextureHandle { it->second.image_ids[image_index], 1u };
    }

    void destroy_swapchain(cd::rhi::SwapchainHandle h) override
    {
        auto it = swapchains_.find(h.index());
        if (it == swapchains_.end())
            return;
        // Remove synthetic image + view entries from images_/views_ *before*
        // destroying the swapchain — the dtor loops must never see swapchain-
        // owned VkImages/VkImageViews or they will double-free.
        for (auto img_id : it->second.image_ids)
        {
            images_.erase(img_id);
        }
        for (auto view_id : it->second.view_ids)
        {
            views_.erase(view_id);
            view_formats_.erase(view_id);  // phase1119 (audit A4)
        }
        for (auto v : it->second.views)
        {
            if (v != VK_NULL_HANDLE)
                vkDestroyImageView(device_, v, nullptr);
        }
        if (it->second.swapchain != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(device_, it->second.swapchain, nullptr);
        }
        if (it->second.surface != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(inst_->instance, it->second.surface, nullptr);
        }
        swapchains_.erase(it);
    }

    // --- Command pool + command buffer (S3.4) ----------------------------
    // Vulkan V2 (multi-queue model): resolve the (pool, family) pair for a
    // QueueType, lazily creating the pool on that QueueType's family. The
    // graphics case returns graphics_pool_/graphics_family_ UNCHANGED so the
    // render path is byte-identical; compute/transfer get their own pool on the
    // selected (possibly aliased-to-graphics) family. Returns false on pool
    // creation failure.
    [[nodiscard]] bool resolve_pool_for_queue_(cd::rhi::QueueType queue,
                                               VkCommandPool& out_pool,
                                               std::uint32_t& out_family) noexcept
    {
        VkCommandPool* slot = &graphics_pool_;
        std::uint32_t family = graphics_family_;
        switch (queue)
        {
            case cd::rhi::QueueType::kGraphics:
                slot = &graphics_pool_;
                family = graphics_family_;
                break;
            case cd::rhi::QueueType::kCompute:
                slot = &compute_pool_;
                family = compute_family_;
                break;
            case cd::rhi::QueueType::kTransfer:
                slot = &transfer_pool_;
                family = transfer_family_;
                break;
        }
        if (*slot == VK_NULL_HANDLE)
        {
            const VkCommandPoolCreateInfo pi {
                .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                .pNext = nullptr,
                // No FREE_DESCRIPTOR_SET_BIT equivalent: callers free via vkFree-
                // CommandBuffers in VulkanCommandBuffer's dtor (single-pool v1).
                // S3.5 will switch to a per-frame pool + vkResetCommandPool.
                .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                .queueFamilyIndex = family,
            };
            if (vkCreateCommandPool(device_, &pi, nullptr, slot) != VK_SUCCESS)
                return false;
        }
        out_pool = *slot;
        out_family = family;
        return true;
    }

    [[nodiscard]] std::unique_ptr<cd::rhi::ICommandBuffer> do_create_command_buffer(cd::rhi::QueueType queue) override
    {
        VkCommandPool pool { VK_NULL_HANDLE };
        std::uint32_t family { graphics_family_ };
        if (!resolve_pool_for_queue_(queue, pool, family))
            return nullptr;

        const VkCommandBufferAllocateInfo ai {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        VkCommandBuffer cmd {};
        if (vkAllocateCommandBuffers(device_, &ai, &cmd) != VK_SUCCESS)
        {
            return nullptr;
        }
        auto vcb = std::make_unique<VulkanCommandBuffer>(
            device_,
            pool,
            cmd,
            ResourceTables {
                .buffers = &buffers_,
                .images = &images_,
                .views = &views_,
                .graphics_pipelines = &graphics_pipelines_,
                .compute_pipelines = &compute_pipelines_,
                .pipeline_layouts = &pipeline_layouts_,
                .pipeline_to_layout = &pipeline_to_layout_,
                .descriptor_sets = &descriptor_sets_,
                .query_pools = &query_pool_handles_,
                .view_formats = &view_formats_,
                .image_formats = &image_formats_,
                // Per-lane pools (parallel render pass) always live on the
                // graphics family — secondaries only execute graphics work.
                .graphics_queue_family = graphics_family_,
                .accel_lookup = &VulkanDevice::accel_lookup_static_,
                .accel_lookup_user = this,
                .rt_pipeline_lookup = &VulkanDevice::rt_pipeline_lookup_static_,
            }
        );
        // Vulkan V2 (multi-queue model): tag the buffer so submit() routes it to
        // the matching VkQueue. kGraphics tag => unchanged graphics submit path.
        vcb->set_queue_type(queue);
        return vcb;
    }

    // Phase 135 — static callback for cmd-buffer bind_rt_pipeline.
    static VkPipeline rt_pipeline_lookup_static_(void* user, std::uint32_t idx) noexcept
    {
        auto* self = static_cast<VulkanDevice*>(user);
        auto it = self->rt_pipelines_.find(idx);
        return (it == self->rt_pipelines_.end()) ? VK_NULL_HANDLE : it->second.pipeline;
    }

    // Phase 132 — static callback the VulkanCommandBuffer invokes to
    // resolve an AS handle into an AccelBuildView. Static so it can
    // sit in a function-pointer slot; `user` is the VulkanDevice*.
    static bool accel_lookup_static_(void* user, std::uint32_t idx, AccelBuildView& out) noexcept
    {
        auto* self = static_cast<VulkanDevice*>(user);
        auto it = self->accels_.find(idx);
        if (it == self->accels_.end()) return false;
        const auto& rec = it->second;
        out.as = rec.as;
        out.scratch_device_address = rec.scratch_device_address;
        out.is_tlas = (rec.kind == cd::rhi::AccelStructureKind::kTopLevel);
        out.instance_device_address = rec.instance_device_address;
        out.instance_count = static_cast<std::uint32_t>(rec.instances.size());
        out.triangle_geos = rec.vk_triangle_geos.data();
        out.triangle_primitive_counts = rec.vk_primitive_counts.data();
        out.triangle_count = static_cast<std::uint32_t>(rec.vk_triangle_geos.size());
        // A-AS-FLAGS / A-REFIT — re-apply the create-time build flags + carry
        // the update-eligibility bit to the cmd-buffer build/refit path.
        out.build_flags = vk_build_flags_from(rec.build_flags);
        out.allows_update =
            cd::rhi::has(rec.build_flags, cd::rhi::AccelBuildFlags::kAllowUpdate);
        return true;
    }

    // ---- Image readback (phase377-B-infra2) ---------------------------------
    //
    // One-shot command buffer: transition src_image to TRANSFER_SRC_OPTIMAL,
    // issue vkCmdCopyImageToBuffer, transition back to SHADER_READ_ONLY_OPTIMAL,
    // submit, fence-wait. Caller does not need to pre-transition the image.
    //
    // Scope-down: no per-image layout tracking exists in VulkanDevice; the
    // transition from UNDEFINED is conservative and safe for textures that have
    // been written to at least once. Textures still in UNDEFINED state on entry
    // (i.e. freshly created and never written) will trigger a validation warning
    // but will not crash — the driver treats UNDEFINED as "I don't care".
    [[nodiscard]] cd::core::Result<void> copy_image_to_buffer(
        cd::rhi::TextureHandle      src_image,
        cd::rhi::BufferHandle       dst_buffer,
        std::uint64_t               dst_offset,
        const cd::rhi::IDevice::ImageRegion& region
    ) override
    {
        const auto img_it  = images_.find(src_image.index());
        const auto meta_it = image_meta_.find(src_image.index());
        const auto buf_it  = buffers_.find(dst_buffer.index());
        const auto met_it  = buffer_meta_.find(dst_buffer.index());

        if (img_it == images_.end() || meta_it == image_meta_.end())
        {
            return std::unexpected(make_err(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "copy_image_to_buffer: unknown src_image handle"));
        }
        if (buf_it == buffers_.end() || met_it == buffer_meta_.end())
        {
            return std::unexpected(make_err(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "copy_image_to_buffer: unknown dst_buffer handle"));
        }
        if (!met_it->second.host_visible)
        {
            return std::unexpected(make_err(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "copy_image_to_buffer: dst_buffer is not host-visible (use kGpuToCpu)"));
        }

        // Ensure the command pool exists (create_command_buffer may not have
        // been called yet in this device's lifetime).
        if (graphics_pool_ == VK_NULL_HANDLE)
        {
            const VkCommandPoolCreateInfo pi {
                .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                .pNext = nullptr,
                .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                .queueFamilyIndex = graphics_family_,
            };
            if (vkCreateCommandPool(device_, &pi, nullptr, &graphics_pool_) != VK_SUCCESS)
            {
                return std::unexpected(make_err(
                    cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                    "copy_image_to_buffer: vkCreateCommandPool failed"));
            }
        }

        // Allocate a one-shot command buffer.
        VkCommandBuffer cmd { VK_NULL_HANDLE };
        const VkCommandBufferAllocateInfo ai {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = graphics_pool_,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        if (vkAllocateCommandBuffers(device_, &ai, &cmd) != VK_SUCCESS)
        {
            return std::unexpected(make_err(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "copy_image_to_buffer: vkAllocateCommandBuffers failed"));
        }

        const VkCommandBufferBeginInfo bi {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr,
        };
        vkBeginCommandBuffer(cmd, &bi);

        // Transition: caller-declared src_state → TRANSFER_SRC_OPTIMAL.
        // phase1127 (X4-B): the legacy default (kUndefined) maps to
        // VK_IMAGE_LAYOUT_UNDEFINED, which per spec may DISCARD the
        // image contents — fine for the original phase377 smoke use,
        // WRONG for reading back rendered results. Callers that render
        // first must set region.src_state to the true current state.
        const VkImageLayout caller_layout = layout_for_state(region.src_state);
        const VkImageMemoryBarrier2 to_src {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
            .oldLayout = caller_layout,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = img_it->second,
            .subresourceRange = {
                .aspectMask = aspect_for_format(meta_it->second.format),
                .baseMipLevel = region.mip_level,
                .levelCount = 1,
                .baseArrayLayer = region.base_layer,
                .layerCount = 1,
            },
        };
        const VkDependencyInfo dep_to_src {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = nullptr,
            .dependencyFlags = 0,
            .memoryBarrierCount = 0,
            .pMemoryBarriers = nullptr,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers = nullptr,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &to_src,
        };
        vkCmdPipelineBarrier2(cmd, &dep_to_src);

        const VkBufferImageCopy copy_region {
            .bufferOffset = dst_offset,
            .bufferRowLength = 0,    // tightly packed
            .bufferImageHeight = 0,  // tightly packed
            .imageSubresource = {
                .aspectMask = aspect_for_format(meta_it->second.format),
                .mipLevel = region.mip_level,
                .baseArrayLayer = region.base_layer,
                .layerCount = 1,
            },
            .imageOffset = { static_cast<std::int32_t>(region.x),
                             static_cast<std::int32_t>(region.y), 0 },
            .imageExtent = { region.width, region.height, 1 },
        };
        vkCmdCopyImageToBuffer(
            cmd,
            img_it->second,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            buf_it->second,
            1,
            &copy_region
        );

        // Transition back: TRANSFER_SRC_OPTIMAL → the caller's declared
        // state (legacy kUndefined keeps the historical SHADER_READ_ONLY
        // restore so existing callers observe no behaviour change).
        const VkImageLayout restore_layout =
            (region.src_state == cd::rhi::ResourceState::kUndefined)
                ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                : caller_layout;
        const VkImageMemoryBarrier2 to_read {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout = restore_layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = img_it->second,
            .subresourceRange = {
                .aspectMask = aspect_for_format(meta_it->second.format),
                .baseMipLevel = region.mip_level,
                .levelCount = 1,
                .baseArrayLayer = region.base_layer,
                .layerCount = 1,
            },
        };
        const VkDependencyInfo dep_to_read {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = nullptr,
            .dependencyFlags = 0,
            .memoryBarrierCount = 0,
            .pMemoryBarriers = nullptr,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers = nullptr,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &to_read,
        };
        vkCmdPipelineBarrier2(cmd, &dep_to_read);

        vkEndCommandBuffer(cmd);

        // Create a fence, submit, wait, clean up.
        VkFenceCreateInfo fci {};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fci.pNext = nullptr;
        fci.flags = 0;
        VkFence fence { VK_NULL_HANDLE };
        if (vkCreateFence(device_, &fci, nullptr, &fence) != VK_SUCCESS)
        {
            vkFreeCommandBuffers(device_, graphics_pool_, 1, &cmd);
            return std::unexpected(make_err(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "copy_image_to_buffer: vkCreateFence failed"));
        }

        const VkCommandBufferSubmitInfo cb_info {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .pNext = nullptr,
            .commandBuffer = cmd,
            .deviceMask = 0,
        };
        const VkSubmitInfo2 si {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .pNext = nullptr,
            .flags = 0,
            .waitSemaphoreInfoCount = 0,
            .pWaitSemaphoreInfos = nullptr,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &cb_info,
            .signalSemaphoreInfoCount = 0,
            .pSignalSemaphoreInfos = nullptr,
        };
        const VkResult submit_res = vkQueueSubmit2(graphics_queue_, 1, &si, fence);
        if (submit_res != VK_SUCCESS)
        {
            vkDestroyFence(device_, fence, nullptr);
            vkFreeCommandBuffers(device_, graphics_pool_, 1, &cmd);
            return std::unexpected(make_err(
                cd::rhi::rhi_errors::Code::kDeviceLost,
                "copy_image_to_buffer: vkQueueSubmit2 failed"));
        }
        vkWaitForFences(device_, 1, &fence, VK_TRUE, ~std::uint64_t { 0 });
        vkDestroyFence(device_, fence, nullptr);
        vkFreeCommandBuffers(device_, graphics_pool_, 1, &cmd);
        return {};
    }

    // ---- One-shot command-buffer helpers (A-COMPACTION reuse) --------------
    //
    // Factor out the lazy-pool + allocate + begin + submit-and-wait pattern
    // copy_image_to_buffer already uses, so compact_acceleration_structure can
    // run its two one-shot submissions (size-query, copy-compact) without
    // duplicating the boilerplate. These mirror that path exactly.

    [[nodiscard]] VkResult ensure_graphics_pool_internal() noexcept
    {
        if (graphics_pool_ != VK_NULL_HANDLE)
            return VK_SUCCESS;
        const VkCommandPoolCreateInfo pi {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = graphics_family_,
        };
        return vkCreateCommandPool(device_, &pi, nullptr, &graphics_pool_);
    }

    [[nodiscard]] VkResult alloc_oneshot_cmd_internal(VkCommandBuffer& out) noexcept
    {
        const VkCommandBufferAllocateInfo ai {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = graphics_pool_,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        return vkAllocateCommandBuffers(device_, &ai, &out);
    }

    void begin_oneshot_cmd_internal(VkCommandBuffer cmd) noexcept
    {
        const VkCommandBufferBeginInfo bi {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr,
        };
        vkBeginCommandBuffer(cmd, &bi);
    }

    [[nodiscard]] VkResult submit_oneshot_wait_internal(VkCommandBuffer cmd) noexcept
    {
        VkFenceCreateInfo fci {};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence fence { VK_NULL_HANDLE };
        if (vkCreateFence(device_, &fci, nullptr, &fence) != VK_SUCCESS)
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        const VkCommandBufferSubmitInfo cb_info {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .pNext = nullptr,
            .commandBuffer = cmd,
            .deviceMask = 0,
        };
        const VkSubmitInfo2 si {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .pNext = nullptr,
            .flags = 0,
            .waitSemaphoreInfoCount = 0,
            .pWaitSemaphoreInfos = nullptr,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &cb_info,
            .signalSemaphoreInfoCount = 0,
            .pSignalSemaphoreInfos = nullptr,
        };
        const VkResult sr = vkQueueSubmit2(graphics_queue_, 1, &si, fence);
        if (sr == VK_SUCCESS)
            vkWaitForFences(device_, 1, &fence, VK_TRUE, ~std::uint64_t { 0 });
        vkDestroyFence(device_, fence, nullptr);
        return sr;
    }

    void submit(cd::rhi::ICommandBuffer& cmd) override
    {
        // Single-command shorthand. Forward to the full submit() so the queue
        // submission path is implemented exactly once.
        cd::rhi::ICommandBuffer* cb_array[] = { &cmd };
        std::span<cd::rhi::ICommandBuffer* const> cb_span { cb_array, 1 };
        cd::rhi::SubmitDesc d {};
        d.command_buffers = cb_span;
        (void)submit(d);
    }

    [[nodiscard]] cd::core::Result<void> submit(const cd::rhi::SubmitDesc& desc) override
    {
        // Resolve command buffers.
        // Vulkan V2 (multi-queue model): a VkSubmitInfo2 targets exactly one
        // VkQueue, so the submit's destination queue is the QueueType of its
        // command buffers. We take it from the first command buffer; the legacy
        // default of kGraphics is preserved when none is present (empty submit)
        // or when the buffer was created with the default kGraphics tag, so the
        // flagship render path routes to graphics_queue_ byte-identically.
        cd::rhi::QueueType target_queue = cd::rhi::QueueType::kGraphics;
        bool queue_resolved = false;
        std::vector<VkCommandBufferSubmitInfo> cb_infos;
        cb_infos.reserve(desc.command_buffers.size());
        for (auto* icb : desc.command_buffers)
        {
            if (icb == nullptr)
                continue;
            auto* vk_cb = dynamic_cast<VulkanCommandBuffer*>(icb);
            if (vk_cb == nullptr || vk_cb->native() == VK_NULL_HANDLE)
            {
                return std::unexpected(
                    make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "submit: non-Vulkan command buffer")
                );
            }
            if (!queue_resolved)
            {
                target_queue = vk_cb->queue_type();
                queue_resolved = true;
            }
            VkCommandBufferSubmitInfo ci {};
            ci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
            ci.pNext = nullptr;
            ci.commandBuffer = vk_cb->native();
            ci.deviceMask = 0;
            cb_infos.push_back(ci);
        }

        // Resolve wait/signal semaphores into VkSemaphoreSubmitInfo entries.
        // Binary semaphores have value=0; timeline semaphores carry their value.
        std::vector<VkSemaphoreSubmitInfo> waits;
        waits.reserve(desc.wait_semaphores.size() + desc.wait_timeline_semaphores.size());
        for (const auto& w : desc.wait_semaphores)
        {
            auto it = semaphores_.find(w.semaphore.index());
            if (it == semaphores_.end())
            {
                return std::unexpected(
                    make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "submit: unknown binary wait semaphore")
                );
            }
            VkSemaphoreSubmitInfo si {};
            si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            si.semaphore = it->second;
            si.value = 0;
            si.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            si.deviceIndex = 0;
            waits.push_back(si);
        }
        for (const auto& w : desc.wait_timeline_semaphores)
        {
            auto it = timeline_semaphores_.find(w.semaphore.index());
            if (it == timeline_semaphores_.end())
            {
                return std::unexpected(
                    make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "submit: unknown timeline wait semaphore")
                );
            }
            VkSemaphoreSubmitInfo si {};
            si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            si.semaphore = it->second;
            si.value = w.value;
            si.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            si.deviceIndex = 0;
            waits.push_back(si);
        }

        std::vector<VkSemaphoreSubmitInfo> signals;
        signals.reserve(desc.signal_semaphores.size() + desc.signal_timeline_semaphores.size());
        for (const auto& s : desc.signal_semaphores)
        {
            auto it = semaphores_.find(s.semaphore.index());
            if (it == semaphores_.end())
            {
                return std::unexpected(
                    make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "submit: unknown binary signal semaphore")
                );
            }
            VkSemaphoreSubmitInfo si {};
            si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            si.semaphore = it->second;
            si.value = 0;
            si.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            si.deviceIndex = 0;
            signals.push_back(si);
        }
        for (const auto& s : desc.signal_timeline_semaphores)
        {
            auto it = timeline_semaphores_.find(s.semaphore.index());
            if (it == timeline_semaphores_.end())
            {
                return std::unexpected(
                    make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "submit: unknown timeline signal semaphore")
                );
            }
            VkSemaphoreSubmitInfo si {};
            si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            si.semaphore = it->second;
            si.value = s.value;
            si.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            si.deviceIndex = 0;
            signals.push_back(si);
        }

        VkFence vk_fence { VK_NULL_HANDLE };
        if (desc.signal_fence.is_valid())
        {
            auto it = fences_.find(desc.signal_fence.index());
            if (it == fences_.end())
            {
                return std::unexpected(
                    make_err(cd::rhi::rhi_errors::Code::kInvalidArgument, "submit: unknown signal fence")
                );
            }
            vk_fence = it->second;
        }

        const VkSubmitInfo2 si {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .pNext = nullptr,
            .flags = 0,
            .waitSemaphoreInfoCount = static_cast<std::uint32_t>(waits.size()),
            .pWaitSemaphoreInfos = waits.empty() ? nullptr : waits.data(),
            .commandBufferInfoCount = static_cast<std::uint32_t>(cb_infos.size()),
            .pCommandBufferInfos = cb_infos.empty() ? nullptr : cb_infos.data(),
            .signalSemaphoreInfoCount = static_cast<std::uint32_t>(signals.size()),
            .pSignalSemaphoreInfos = signals.empty() ? nullptr : signals.data(),
        };
        // Vulkan V2 (multi-queue model): route to the queue for this submit's
        // QueueType. kGraphics (the default + the flagship path) resolves to
        // graphics_queue_, so this is the exact same call as before for the
        // render loop. Compute/transfer queues alias graphics_queue_ on GPUs
        // with a single universal family — still correct.
        if (vkQueueSubmit2(queue_for_type_(target_queue), 1, &si, vk_fence) != VK_SUCCESS)
        {
            return std::unexpected(make_err(cd::rhi::rhi_errors::Code::kDeviceLost, "vkQueueSubmit2 failed"));
        }
        return {};
    }

    // Vulkan V2 (multi-queue model): map a QueueType to its VkQueue. Aliased
    // QueueTypes return graphics_queue_ (same handle the factory retrieved for
    // their shared family), so routing degenerates to the legacy single queue.
    [[nodiscard]] VkQueue queue_for_type_(cd::rhi::QueueType q) const noexcept
    {
        switch (q)
        {
            case cd::rhi::QueueType::kGraphics:
                return graphics_queue_;
            case cd::rhi::QueueType::kCompute:
                return compute_queue_ != VK_NULL_HANDLE ? compute_queue_ : graphics_queue_;
            case cd::rhi::QueueType::kTransfer:
                return transfer_queue_ != VK_NULL_HANDLE ? transfer_queue_ : graphics_queue_;
        }
        return graphics_queue_;
    }

    void wait_idle() override
    {
        if (device_ != VK_NULL_HANDLE)
            vkDeviceWaitIdle(device_);
    }

    // A-AS-FLAGS (Backend-to-100 Wave 3b) — map the engine's AccelBuildFlags
    // onto VkBuildAccelerationStructureFlagsKHR. kPreferFastTrace is the
    // default on AccelStructureDesc, so an untouched descriptor reproduces the
    // historical hardcoded PREFER_FAST_TRACE flag exactly. The flags are stored
    // in the AccelRecord and re-applied by the command-buffer build path (so a
    // refit's MODE_UPDATE build carries the SAME flags as the create-time size
    // query — Vulkan requires the build flags to match the size-query flags).
    [[nodiscard]] static VkBuildAccelerationStructureFlagsKHR
    vk_build_flags_from(cd::rhi::AccelBuildFlags f) noexcept
    {
        VkBuildAccelerationStructureFlagsKHR out = 0;
        if (cd::rhi::has(f, cd::rhi::AccelBuildFlags::kAllowUpdate))
            out |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
        if (cd::rhi::has(f, cd::rhi::AccelBuildFlags::kAllowCompaction))
            out |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
        if (cd::rhi::has(f, cd::rhi::AccelBuildFlags::kPreferFastTrace))
            out |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        if (cd::rhi::has(f, cd::rhi::AccelBuildFlags::kPreferFastBuild))
            out |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
        if (cd::rhi::has(f, cd::rhi::AccelBuildFlags::kLowMemory))
            out |= VK_BUILD_ACCELERATION_STRUCTURE_LOW_MEMORY_BIT_KHR;
        return out;
    }

    // ---- Ray tracing (Phase 17.A — BLAS creation) -------------------------
    //
    // Creates the AS object + its backing buffer. The BUILD is deferred
    // to the command buffer (ICommandBuffer::build_acceleration_structure).
    // BLAS only at v0.48.0 — TLAS + RT pipeline + dispatch_rays land in
    // 17.B.
    [[nodiscard]] cd::core::Result<cd::rhi::AccelStructureHandle>
    create_acceleration_structure(const cd::rhi::AccelStructureDesc& desc) override
    {
        if (!features_.ray_tracing)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kNotImplemented,
                "create_acceleration_structure: device lacks RT extensions"));
        }
        // Phase 127 — TLAS create path (build deferred to a future
        // command-buffer wave). Mirror BLAS but feed the size query
        // an INSTANCES geometry with a placeholder instance buffer
        // address (sizes only depend on instance count).
        if (desc.kind == cd::rhi::AccelStructureKind::kTopLevel)
        {
            if (desc.instances.empty())
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kInvalidArgument,
                    "create_acceleration_structure: TLAS must have >=1 instance"));
            }
            // Reject every instance whose BLAS handle isn't backed by an
            // existing AccelRecord. Without this guard the build path
            // would later call vkGetBufferDeviceAddress on a missing
            // entry and segfault.
            for (const auto& inst : desc.instances)
            {
                if (!inst.blas.is_valid() ||
                    !accels_.contains(inst.blas.index()))
                {
                    return std::unexpected(cd::rhi::rhi_errors::make(
                        cd::rhi::rhi_errors::Code::kInvalidArgument,
                        "create_acceleration_structure: TLAS instance "
                        "references invalid or unknown BLAS handle"));
                }
            }

            VkAccelerationStructureGeometryKHR g {};
            g.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
            g.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
            g.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
            g.geometry.instances.sType =
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
            g.geometry.instances.arrayOfPointers = VK_FALSE;
            g.geometry.instances.data.deviceAddress = 0;  // placeholder

            // A-AS-FLAGS — thread the descriptor's build flags into the size
            // query (the default kPreferFastTrace reproduces the old hardcode).
            const VkBuildAccelerationStructureFlagsKHR tlas_flags =
                vk_build_flags_from(desc.build_flags);
            VkAccelerationStructureBuildGeometryInfoKHR bgi {};
            bgi.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
            bgi.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
            bgi.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
            bgi.flags = tlas_flags;
            bgi.geometryCount = 1;
            bgi.pGeometries = &g;

            auto prim_count = static_cast<std::uint32_t>(desc.instances.size());
            VkAccelerationStructureBuildSizesInfoKHR sizes {};
            sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
            vkGetAccelerationStructureBuildSizesKHR(
                device_,
                VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                &bgi, &prim_count, &sizes);

            VkBufferCreateInfo bci {};
            bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bci.size = sizes.accelerationStructureSize;
            bci.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            VmaAllocationCreateInfo aci {};
            aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            VkBuffer storage_buf { VK_NULL_HANDLE };
            VmaAllocation storage_alloc { VK_NULL_HANDLE };
            if (vmaCreateBuffer(vma_allocator_, &bci, &aci, &storage_buf, &storage_alloc, nullptr) != VK_SUCCESS)
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                    "TLAS storage buffer allocation failed"));
            }

            VkAccelerationStructureCreateInfoKHR aci_as {};
            aci_as.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
            aci_as.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
            aci_as.buffer = storage_buf;
            aci_as.offset = 0;
            aci_as.size = sizes.accelerationStructureSize;
            VkAccelerationStructureKHR as { VK_NULL_HANDLE };
            if (vkCreateAccelerationStructureKHR(device_, &aci_as, nullptr, &as) != VK_SUCCESS)
            {
                vmaDestroyBuffer(vma_allocator_, storage_buf, storage_alloc);
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                    "vkCreateAccelerationStructureKHR(TLAS) failed"));
            }

            AccelRecord rec;
            rec.as = as;
            rec.storage_buf = storage_buf;
            rec.storage_alloc = storage_alloc;
            // A-REFIT — refit (MODE_UPDATE) needs at least updateScratchSize
            // scratch. Size the single scratch buffer to the larger of the two
            // so the SAME buffer backs both the initial build and every refit.
            rec.scratch_size = std::max(sizes.buildScratchSize, sizes.updateScratchSize);
            rec.update_scratch_size = sizes.updateScratchSize;
            rec.as_size = sizes.accelerationStructureSize;
            rec.build_flags = desc.build_flags;
            rec.kind = desc.kind;
            rec.instances.assign(desc.instances.begin(), desc.instances.end());

            // Phase 132 — allocate per-AS scratch buffer.
            {
                VkBufferCreateInfo sbci {};
                sbci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                sbci.size = rec.scratch_size;
                sbci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
                sbci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                VmaAllocationCreateInfo saci {};
                saci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
                if (vmaCreateBuffer(vma_allocator_, &sbci, &saci,
                                    &rec.scratch_buf, &rec.scratch_alloc, nullptr) != VK_SUCCESS)
                {
                    vkDestroyAccelerationStructureKHR(device_, as, nullptr);
                    vmaDestroyBuffer(vma_allocator_, storage_buf, storage_alloc);
                    return std::unexpected(cd::rhi::rhi_errors::make(
                        cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                        "TLAS scratch buffer allocation failed"));
                }
                VkBufferDeviceAddressInfo si {};
                si.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
                si.buffer = rec.scratch_buf;
                rec.scratch_device_address = vkGetBufferDeviceAddress(device_, &si);
            }

            // Resolve this TLAS's own device address.
            {
                VkAccelerationStructureDeviceAddressInfoKHR info {};
                info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
                info.accelerationStructure = as;
                rec.as_device_address = vkGetAccelerationStructureDeviceAddressKHR(device_, &info);
            }

            // Phase 133 — allocate + upload the TLAS instance buffer
            // (translate engine AccelInstance → VkAS InstanceKHR).
            if (!rec.instances.empty())
            {
                const VkDeviceSize ibytes =
                    sizeof(VkAccelerationStructureInstanceKHR) * rec.instances.size();
                VkBufferCreateInfo ibci {};
                ibci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                ibci.size = ibytes;
                ibci.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT;
                ibci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                VmaAllocationCreateInfo iaci {};
                iaci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                             VMA_ALLOCATION_CREATE_MAPPED_BIT;
                iaci.usage = VMA_MEMORY_USAGE_AUTO;
                VmaAllocationInfo iai {};
                if (vmaCreateBuffer(vma_allocator_, &ibci, &iaci,
                                    &rec.instance_buf, &rec.instance_alloc, &iai) != VK_SUCCESS)
                {
                    vmaDestroyBuffer(vma_allocator_, rec.scratch_buf, rec.scratch_alloc);
                    vkDestroyAccelerationStructureKHR(device_, as, nullptr);
                    vmaDestroyBuffer(vma_allocator_, storage_buf, storage_alloc);
                    return std::unexpected(cd::rhi::rhi_errors::make(
                        cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                        "TLAS instance buffer allocation failed"));
                }
                auto* dst = static_cast<VkAccelerationStructureInstanceKHR*>(iai.pMappedData);
                for (std::size_t i = 0; i < rec.instances.size(); ++i)
                {
                    const auto& src = rec.instances[i];
                    VkAccelerationStructureInstanceKHR& out = dst[i];
                    std::memcpy(&out.transform, src.transform, sizeof(out.transform));
                    out.instanceCustomIndex = src.instance_id & 0xFFFFFFu;
                    out.mask                = src.mask;
                    out.instanceShaderBindingTableRecordOffset = src.hit_offset & 0xFFFFFFu;
                    out.flags               = src.flags;
                    if (src.blas.is_valid())
                    {
                        auto blas_it = accels_.find(src.blas.index());
                        out.accelerationStructureReference =
                            (blas_it != accels_.end()) ? blas_it->second.as_device_address : 0;
                    }
                    else
                    {
                        out.accelerationStructureReference = 0;
                    }
                }
                VkBufferDeviceAddressInfo bdai {};
                bdai.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
                bdai.buffer = rec.instance_buf;
                rec.instance_device_address = vkGetBufferDeviceAddress(device_, &bdai);
            }

            const auto id = next_id_++;
            accels_.emplace(id, std::move(rec));
            return cd::rhi::AccelStructureHandle { id, 1u };
        }
        if (desc.triangles.empty())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "create_acceleration_structure: BLAS must have >=1 triangle geometry"));
        }

        // Build the per-geometry VkAccelerationStructureGeometryKHR list +
        // primitive-count list. We rely on the buffers already existing in
        // buffers_ with bufferDeviceAddress retrievable.
        std::vector<VkAccelerationStructureGeometryKHR> geos;
        std::vector<std::uint32_t> primitive_counts;
        geos.reserve(desc.triangles.size());
        primitive_counts.reserve(desc.triangles.size());

        auto get_device_address = [this](cd::rhi::BufferHandle bh,
                                         std::uint64_t offset) -> VkDeviceAddress {
            auto it = buffers_.find(bh.index());
            if (it == buffers_.end()) return 0;
            VkBufferDeviceAddressInfo info {};
            info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            info.buffer = it->second;
            return vkGetBufferDeviceAddress(device_, &info) + offset;
        };

        for (const auto& t : desc.triangles)
        {
            VkAccelerationStructureGeometryKHR g {};
            g.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
            g.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            g.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
            g.geometry.triangles.sType =
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
            g.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
            g.geometry.triangles.vertexData.deviceAddress =
                get_device_address(t.vertex_buffer, t.vertex_offset);
            g.geometry.triangles.vertexStride = t.vertex_stride;
            g.geometry.triangles.maxVertex =
                t.vertex_count == 0 ? 0 : t.vertex_count - 1;
            if (t.index_count == 0)
                g.geometry.triangles.indexType = VK_INDEX_TYPE_NONE_KHR;
            else if (t.index_type == cd::rhi::IndexType::kUInt16)
                g.geometry.triangles.indexType = VK_INDEX_TYPE_UINT16;
            else
                g.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
            g.geometry.triangles.indexData.deviceAddress =
                t.index_count > 0
                    ? get_device_address(t.index_buffer, t.index_offset)
                    : 0;
            geos.push_back(g);
            primitive_counts.push_back(
                t.index_count > 0 ? t.index_count / 3 : t.vertex_count / 3);
        }

        // A-AS-FLAGS — thread the descriptor's build flags into the BLAS size
        // query (default kPreferFastTrace reproduces the old hardcode).
        const VkBuildAccelerationStructureFlagsKHR blas_flags =
            vk_build_flags_from(desc.build_flags);
        VkAccelerationStructureBuildGeometryInfoKHR bgi {};
        bgi.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        bgi.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        bgi.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        bgi.flags = blas_flags;
        bgi.geometryCount = static_cast<std::uint32_t>(geos.size());
        bgi.pGeometries = geos.data();

        VkAccelerationStructureBuildSizesInfoKHR sizes {};
        sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        vkGetAccelerationStructureBuildSizesKHR(
            device_,
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &bgi, primitive_counts.data(), &sizes);

        // AS-storage buffer (DEVICE_ADDRESS + AS_STORAGE).
        VkBufferCreateInfo bci {};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = sizes.accelerationStructureSize;
        bci.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo aci {};
        aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        VkBuffer storage_buf { VK_NULL_HANDLE };
        VmaAllocation storage_alloc { VK_NULL_HANDLE };
        if (vmaCreateBuffer(vma_allocator_, &bci, &aci, &storage_buf, &storage_alloc, nullptr) != VK_SUCCESS)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "AS storage buffer allocation failed"));
        }

        VkAccelerationStructureCreateInfoKHR aci_as {};
        aci_as.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        aci_as.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        aci_as.buffer = storage_buf;
        aci_as.offset = 0;
        aci_as.size = sizes.accelerationStructureSize;
        VkAccelerationStructureKHR as { VK_NULL_HANDLE };
        if (vkCreateAccelerationStructureKHR(device_, &aci_as, nullptr, &as) != VK_SUCCESS)
        {
            vmaDestroyBuffer(vma_allocator_, storage_buf, storage_alloc);
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "vkCreateAccelerationStructureKHR failed"));
        }

        AccelRecord rec;
        rec.as = as;
        rec.storage_buf = storage_buf;
        rec.storage_alloc = storage_alloc;
        // A-REFIT — size the scratch buffer to cover both the build and the
        // (cheaper) in-place update so a refit re-uses the same allocation.
        rec.scratch_size = std::max(sizes.buildScratchSize, sizes.updateScratchSize);
        rec.update_scratch_size = sizes.updateScratchSize;
        rec.as_size = sizes.accelerationStructureSize;
        rec.build_flags = desc.build_flags;
        rec.kind = desc.kind;
        rec.triangles.assign(desc.triangles.begin(), desc.triangles.end());
        // Phase 132 — keep the Vk-format triangle list for the build pass.
        rec.vk_triangle_geos = geos;
        rec.vk_primitive_counts = primitive_counts;

        // Phase 132 — allocate per-BLAS scratch buffer.
        {
            VkBufferCreateInfo sbci {};
            sbci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            sbci.size = rec.scratch_size;
            sbci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            sbci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            VmaAllocationCreateInfo saci {};
            saci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            if (vmaCreateBuffer(vma_allocator_, &sbci, &saci,
                                &rec.scratch_buf, &rec.scratch_alloc, nullptr) != VK_SUCCESS)
            {
                vkDestroyAccelerationStructureKHR(device_, as, nullptr);
                vmaDestroyBuffer(vma_allocator_, storage_buf, storage_alloc);
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                    "BLAS scratch buffer allocation failed"));
            }
            VkBufferDeviceAddressInfo si {};
            si.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            si.buffer = rec.scratch_buf;
            rec.scratch_device_address = vkGetBufferDeviceAddress(device_, &si);
        }
        // Resolve this BLAS's device address for downstream TLAS instances.
        {
            VkAccelerationStructureDeviceAddressInfoKHR info {};
            info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
            info.accelerationStructure = as;
            rec.as_device_address = vkGetAccelerationStructureDeviceAddressKHR(device_, &info);
        }

        const auto id = next_id_++;
        accels_.emplace(id, std::move(rec));
        return cd::rhi::AccelStructureHandle { id, 1u };
    }

    void destroy_acceleration_structure(cd::rhi::AccelStructureHandle h) override
    {
        auto it = accels_.find(h.index());
        if (it == accels_.end()) return;
        if (it->second.as != VK_NULL_HANDLE)
            vkDestroyAccelerationStructureKHR(device_, it->second.as, nullptr);
        // Free scratch + instance auxiliary buffers (Phase 132/133).
        if (it->second.scratch_buf != VK_NULL_HANDLE)
            vmaDestroyBuffer(vma_allocator_, it->second.scratch_buf, it->second.scratch_alloc);
        if (it->second.instance_buf != VK_NULL_HANDLE)
            vmaDestroyBuffer(vma_allocator_, it->second.instance_buf, it->second.instance_alloc);
        if (it->second.storage_buf != VK_NULL_HANDLE)
            vmaDestroyBuffer(vma_allocator_, it->second.storage_buf, it->second.storage_alloc);
        accels_.erase(it);
    }

    // A-COMPACTION (Backend-to-100 Wave 3b) — DEBUG/TEST observability of the
    // device-reported result-data size of an AS. The compaction tests assert
    // the compacted AS is strictly smaller than its source.
    [[nodiscard]] std::uint64_t
    acceleration_structure_size(cd::rhi::AccelStructureHandle h) const noexcept override
    {
        auto it = accels_.find(h.index());
        if (it == accels_.end()) return 0u;
        return static_cast<std::uint64_t>(it->second.as_size);
    }

    // A-COMPACTION (Backend-to-100 Wave 3b) — compact a BUILT AS that opted into
    // kAllowCompaction. Self-contained one-shot path mirroring copy_image_to_
    // buffer: (1) record vkCmdWriteAccelerationStructuresPropertiesKHR(
    // COMPACTED_SIZE) into a VkQueryPool, submit+wait; (2) read the compacted
    // size back; (3) create a smaller storage buffer + AS; (4) record
    // vkCmdCopyAccelerationStructureKHR(MODE_COMPACT) src->dst, submit+wait.
    // Returns a NEW, smaller AccelStructureHandle that traces identically. The
    // source handle is left intact for the caller to destroy.
    [[nodiscard]] cd::core::Result<cd::rhi::AccelStructureHandle>
    compact_acceleration_structure(cd::rhi::AccelStructureHandle src) override
    {
        if (!features_.ray_tracing)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kNotImplemented,
                "compact_acceleration_structure: device lacks RT extensions"));
        }
        auto src_it = accels_.find(src.index());
        if (src_it == accels_.end() || src_it->second.as == VK_NULL_HANDLE)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "compact_acceleration_structure: unknown source AS handle"));
        }
        if (!cd::rhi::has(src_it->second.build_flags,
                          cd::rhi::AccelBuildFlags::kAllowCompaction))
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "compact_acceleration_structure: source AS was not created with "
                "AccelBuildFlags::kAllowCompaction"));
        }
        // NB: VkAccelerationStructureKHR is a pointer typedef — a top-level
        // const would const-qualify the pointer (misc-misplaced-const), not the
        // pointee, so it is intentionally non-const here (the handle is only
        // read, never mutated).
        VkAccelerationStructureKHR src_as = src_it->second.as;
        const cd::rhi::AccelStructureKind kind = src_it->second.kind;

        if (ensure_graphics_pool_internal() != VK_SUCCESS)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "compact_acceleration_structure: vkCreateCommandPool failed"));
        }

        // --- Step 1: query the compacted size ------------------------------
        VkQueryPoolCreateInfo qpci {};
        qpci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        qpci.queryType =
            VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
        qpci.queryCount = 1;
        VkQueryPool qpool { VK_NULL_HANDLE };
        if (vkCreateQueryPool(device_, &qpci, nullptr, &qpool) != VK_SUCCESS)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "compact_acceleration_structure: vkCreateQueryPool failed"));
        }

        VkCommandBuffer cmd { VK_NULL_HANDLE };
        if (alloc_oneshot_cmd_internal(cmd) != VK_SUCCESS)
        {
            vkDestroyQueryPool(device_, qpool, nullptr);
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "compact_acceleration_structure: cmd alloc failed (size query)"));
        }
        begin_oneshot_cmd_internal(cmd);
        vkCmdResetQueryPool(cmd, qpool, 0, 1);
        vkCmdWriteAccelerationStructuresPropertiesKHR(
            cmd, 1, &src_as,
            VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
            qpool, 0);
        vkEndCommandBuffer(cmd);
        if (submit_oneshot_wait_internal(cmd) != VK_SUCCESS)
        {
            vkFreeCommandBuffers(device_, graphics_pool_, 1, &cmd);
            vkDestroyQueryPool(device_, qpool, nullptr);
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kDeviceLost,
                "compact_acceleration_structure: size-query submit failed"));
        }
        vkFreeCommandBuffers(device_, graphics_pool_, 1, &cmd);

        VkDeviceSize compacted_size { 0 };
        const VkResult qres = vkGetQueryPoolResults(
            device_, qpool, 0, 1, sizeof(compacted_size), &compacted_size,
            sizeof(compacted_size),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        vkDestroyQueryPool(device_, qpool, nullptr);
        if (qres != VK_SUCCESS || compacted_size == 0)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "compact_acceleration_structure: compacted-size readback failed"));
        }

        // --- Step 2: allocate the smaller storage buffer + AS --------------
        VkBufferCreateInfo bci {};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = compacted_size;
        bci.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo aci {};
        aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        VkBuffer dst_buf { VK_NULL_HANDLE };
        VmaAllocation dst_alloc { VK_NULL_HANDLE };
        if (vmaCreateBuffer(vma_allocator_, &bci, &aci, &dst_buf, &dst_alloc, nullptr)
            != VK_SUCCESS)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "compact_acceleration_structure: compacted storage alloc failed"));
        }

        VkAccelerationStructureCreateInfoKHR ci {};
        ci.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        ci.type = (kind == cd::rhi::AccelStructureKind::kTopLevel)
            ? VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR
            : VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        ci.buffer = dst_buf;
        ci.offset = 0;
        ci.size = compacted_size;
        VkAccelerationStructureKHR dst_as { VK_NULL_HANDLE };
        if (vkCreateAccelerationStructureKHR(device_, &ci, nullptr, &dst_as)
            != VK_SUCCESS)
        {
            vmaDestroyBuffer(vma_allocator_, dst_buf, dst_alloc);
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "compact_acceleration_structure: vkCreateAccelerationStructure "
                "(compacted) failed"));
        }

        // --- Step 3: copy-compact src -> dst -------------------------------
        VkCommandBuffer cmd2 { VK_NULL_HANDLE };
        if (alloc_oneshot_cmd_internal(cmd2) != VK_SUCCESS)
        {
            vkDestroyAccelerationStructureKHR(device_, dst_as, nullptr);
            vmaDestroyBuffer(vma_allocator_, dst_buf, dst_alloc);
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "compact_acceleration_structure: cmd alloc failed (copy)"));
        }
        begin_oneshot_cmd_internal(cmd2);
        VkCopyAccelerationStructureInfoKHR copy {};
        copy.sType = VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR;
        copy.src = src_as;
        copy.dst = dst_as;
        copy.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
        vkCmdCopyAccelerationStructureKHR(cmd2, &copy);
        vkEndCommandBuffer(cmd2);
        if (submit_oneshot_wait_internal(cmd2) != VK_SUCCESS)
        {
            vkFreeCommandBuffers(device_, graphics_pool_, 1, &cmd2);
            vkDestroyAccelerationStructureKHR(device_, dst_as, nullptr);
            vmaDestroyBuffer(vma_allocator_, dst_buf, dst_alloc);
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kDeviceLost,
                "compact_acceleration_structure: copy-compact submit failed"));
        }
        vkFreeCommandBuffers(device_, graphics_pool_, 1, &cmd2);

        // --- Step 4: register the compacted AS as a new handle -------------
        AccelRecord rec;
        rec.as = dst_as;
        rec.storage_buf = dst_buf;
        rec.storage_alloc = dst_alloc;
        rec.as_size = compacted_size;
        // The compacted copy is read-only (no further build/refit/compaction);
        // it has no scratch/instance buffers and no build flags.
        rec.build_flags = cd::rhi::AccelBuildFlags::kNone;
        rec.kind = kind;
        {
            VkAccelerationStructureDeviceAddressInfoKHR info {};
            info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
            info.accelerationStructure = dst_as;
            rec.as_device_address =
                vkGetAccelerationStructureDeviceAddressKHR(device_, &info);
        }
        const auto id = next_id_++;
        accels_.emplace(id, std::move(rec));
        return cd::rhi::AccelStructureHandle { id, 1u };
    }

    // ===== phase838-W8-BE-rt-bindless-texture-sampling ====================
    //
    // Each bindless texture array owns:
    //   * VkDescriptorPool (UPDATE_AFTER_BIND_POOL flag).
    //   * VkDescriptorSetLayout (binding 0, type COMBINED_IMAGE_SAMPLER,
    //     count = slot_count, flags UPDATE_AFTER_BIND | PARTIALLY_BOUND |
    //     VARIABLE_DESCRIPTOR_COUNT).
    //   * VkDescriptorSet (allocated with VARIABLE_DESCRIPTOR_COUNT).
    //   * VkSampler (caller-supplied; we hold a reference, not ownership).
    //
    // Per-slot writes go through `write_bindless_texture_slot` which
    // re-uses the existing samplers_ + views_ caches.

    [[nodiscard]] cd::core::Result<cd::rhi::BindlessTextureArrayHandle>
    create_bindless_texture_array(
        const cd::rhi::BindlessTextureArrayDesc& desc) override
    {
        if (!features_.bindless_resources)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kNotImplemented,
                "create_bindless_texture_array: device lacks descriptor_indexing"));
        }
        if (desc.slot_count == 0)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "create_bindless_texture_array: slot_count must be > 0"));
        }
        auto smp_it = samplers_.find(desc.sampler.index());
        if (smp_it == samplers_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "create_bindless_texture_array: unknown sampler"));
        }

        // 1. Descriptor pool (UPDATE_AFTER_BIND).
        const VkDescriptorPoolSize pool_size {
            .type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = desc.slot_count,
        };
        const VkDescriptorPoolCreateInfo pool_ci {
            .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext         = nullptr,
            .flags         = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
            .maxSets       = 1,
            .poolSizeCount = 1,
            .pPoolSizes    = &pool_size,
        };
        VkDescriptorPool pool { VK_NULL_HANDLE };
        if (vkCreateDescriptorPool(device_, &pool_ci, nullptr, &pool) != VK_SUCCESS)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "create_bindless_texture_array: vkCreateDescriptorPool failed"));
        }

        // 2. Descriptor set layout (with binding flags).
        const VkDescriptorBindingFlags binding_flag =
              VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
            | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
            | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
        const VkDescriptorSetLayoutBindingFlagsCreateInfo flags_ci {
            .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
            .pNext         = nullptr,
            .bindingCount  = 1,
            .pBindingFlags = &binding_flag,
        };
        const VkDescriptorSetLayoutBinding layout_binding {
            .binding            = 0,
            .descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount    = desc.slot_count,
            .stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr,
        };
        const VkDescriptorSetLayoutCreateInfo layout_ci {
            .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext        = &flags_ci,
            .flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
            .bindingCount = 1,
            .pBindings    = &layout_binding,
        };
        VkDescriptorSetLayout set_layout { VK_NULL_HANDLE };
        if (vkCreateDescriptorSetLayout(device_, &layout_ci, nullptr, &set_layout) != VK_SUCCESS)
        {
            vkDestroyDescriptorPool(device_, pool, nullptr);
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "create_bindless_texture_array: vkCreateDescriptorSetLayout failed"));
        }

        // 3. Allocate the descriptor set with VARIABLE_DESCRIPTOR_COUNT.
        const VkDescriptorSetVariableDescriptorCountAllocateInfo var_count_ci {
            .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
            .pNext              = nullptr,
            .descriptorSetCount = 1,
            .pDescriptorCounts  = &desc.slot_count,
        };
        const VkDescriptorSetAllocateInfo alloc_ci {
            .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext              = &var_count_ci,
            .descriptorPool     = pool,
            .descriptorSetCount = 1,
            .pSetLayouts        = &set_layout,
        };
        VkDescriptorSet set { VK_NULL_HANDLE };
        if (vkAllocateDescriptorSets(device_, &alloc_ci, &set) != VK_SUCCESS)
        {
            vkDestroyDescriptorSetLayout(device_, set_layout, nullptr);
            vkDestroyDescriptorPool(device_, pool, nullptr);
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "create_bindless_texture_array: vkAllocateDescriptorSets failed"));
        }

        // 4. Stash + return handle.
        const std::uint32_t id = next_bindless_id_++;
        bindless_arrays_.emplace(id, BindlessTextureArrayRecord {
            .pool       = pool,
            .set_layout = set_layout,
            .set        = set,
            .sampler    = smp_it->second,
            .slot_count = desc.slot_count,
        });
        return cd::rhi::BindlessTextureArrayHandle { id, 0u };
    }

    [[nodiscard]] cd::core::Result<void>
    write_bindless_texture_slot(cd::rhi::BindlessTextureArrayHandle array,
                                std::uint32_t                       slot,
                                cd::rhi::TextureViewHandle          view) override
    {
        auto arr_it = bindless_arrays_.find(array.index());
        if (arr_it == bindless_arrays_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "write_bindless_texture_slot: unknown bindless array handle"));
        }
        if (slot >= arr_it->second.slot_count)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "write_bindless_texture_slot: slot out of range"));
        }
        auto view_it = views_.find(view.index());
        if (view_it == views_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "write_bindless_texture_slot: unknown texture view"));
        }
        const VkDescriptorImageInfo img {
            .sampler     = arr_it->second.sampler,
            .imageView   = view_it->second,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        const VkWriteDescriptorSet w {
            .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext            = nullptr,
            .dstSet           = arr_it->second.set,
            .dstBinding       = 0,
            .dstArrayElement  = slot,
            .descriptorCount  = 1,
            .descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo       = &img,
            .pBufferInfo      = nullptr,
            .pTexelBufferView = nullptr,
        };
        vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
        return {};
    }

    void destroy_bindless_texture_array(cd::rhi::BindlessTextureArrayHandle h) override
    {
        auto it = bindless_arrays_.find(h.index());
        if (it == bindless_arrays_.end()) return;
        // VkDescriptorSet is freed transitively when the pool is destroyed.
        if (it->second.set_layout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(device_, it->second.set_layout, nullptr);
        if (it->second.pool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(device_, it->second.pool, nullptr);
        bindless_arrays_.erase(it);
    }

    // Internal accessors so the sample-side wiring can attach the array
    // to a pipeline layout or query its set/layout.
    [[nodiscard]] VkDescriptorSet bindless_descriptor_set(
        cd::rhi::BindlessTextureArrayHandle h) const noexcept
    {
        auto it = bindless_arrays_.find(h.index());
        return it == bindless_arrays_.end() ? VK_NULL_HANDLE : it->second.set;
    }

    [[nodiscard]] VkDescriptorSetLayout bindless_set_layout(
        cd::rhi::BindlessTextureArrayHandle h) const noexcept
    {
        auto it = bindless_arrays_.find(h.index());
        return it == bindless_arrays_.end() ? VK_NULL_HANDLE : it->second.set_layout;
    }

    // ===== Phase 135 — Ray-tracing pipeline (Vulkan impl) =================

    [[nodiscard]] cd::core::Result<cd::rhi::RtPipelineHandle>
    create_rt_pipeline(const cd::rhi::RtPipelineDesc& desc,
                      cd::rhi::PipelineLayoutHandle layout) override
    {
        if (!features_.ray_tracing || vkCreateRayTracingPipelinesKHR == nullptr)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kNotImplemented,
                "create_rt_pipeline: device lacks RT pipeline extension"));
        }
        if (desc.shaders.empty())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "create_rt_pipeline: empty shader list"));
        }

        auto layout_it = pipeline_layouts_.find(layout.index());
        if (layout_it == pipeline_layouts_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "create_rt_pipeline: unknown pipeline layout"));
        }

        // Build VkPipelineShaderStageCreateInfo array. RtShaderEntry
        // carries pre-compiled SPIR-V via ShaderModuleHandle.
        std::vector<VkPipelineShaderStageCreateInfo> stages;
        stages.reserve(desc.shaders.size());
        auto stage_bit = [](cd::rhi::RtShaderStage s) -> VkShaderStageFlagBits {
            switch (s)
            {
                case cd::rhi::RtShaderStage::kRaygen:       return VK_SHADER_STAGE_RAYGEN_BIT_KHR;
                case cd::rhi::RtShaderStage::kMiss:         return VK_SHADER_STAGE_MISS_BIT_KHR;
                case cd::rhi::RtShaderStage::kClosestHit:   return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
                case cd::rhi::RtShaderStage::kAnyHit:       return VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
                case cd::rhi::RtShaderStage::kIntersection: return VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
                case cd::rhi::RtShaderStage::kCallable:     return VK_SHADER_STAGE_CALLABLE_BIT_KHR;
            }
            return VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        };
        // Vulkan's pPipelineShaderStageCreateInfo::pName needs a c_str
        // that outlives the create call. Pin every entry name into a
        // std::vector<std::string> to keep stable pointers.
        std::vector<std::string> entry_arena;
        entry_arena.reserve(desc.shaders.size());
        for (const auto& e : desc.shaders)
        {
            auto sm_it = shaders_.find(e.module.index());
            if (sm_it == shaders_.end())
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kInvalidArgument,
                    "create_rt_pipeline: unknown shader module handle"));
            }
            const auto& pinned_name = entry_arena.emplace_back(e.entry);
            VkPipelineShaderStageCreateInfo s {};
            s.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            s.stage  = stage_bit(e.stage);
            s.module = sm_it->second;
            s.pName  = pinned_name.c_str();
            stages.push_back(s);
        }

        // Build shader-group list. Each unique `group` index becomes
        // one VkRayTracingShaderGroupCreateInfoKHR. Entries within
        // the same group either represent a triangle-hit-group
        // (closest_hit + optional any_hit + intersection) or a
        // single general shader (raygen / miss / callable).
        std::vector<VkRayTracingShaderGroupCreateInfoKHR> groups;
        // Collect groups by first-occurrence order.
        std::vector<std::uint32_t> group_ids_seen;
        for (const auto& e : desc.shaders)
        {
            if (std::ranges::find(group_ids_seen, e.group) == group_ids_seen.end())
            {
                group_ids_seen.push_back(e.group);
            }
        }
        for (auto gid : group_ids_seen)
        {
            VkRayTracingShaderGroupCreateInfoKHR g {};
            g.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            g.generalShader      = VK_SHADER_UNUSED_KHR;
            g.closestHitShader   = VK_SHADER_UNUSED_KHR;
            g.anyHitShader       = VK_SHADER_UNUSED_KHR;
            g.intersectionShader = VK_SHADER_UNUSED_KHR;
            bool is_hit_group = false;
            for (std::uint32_t i = 0; i < desc.shaders.size(); ++i)
            {
                const auto& e = desc.shaders[i];
                if (e.group != gid) continue;
                switch (e.stage)
                {
                    case cd::rhi::RtShaderStage::kRaygen:
                    case cd::rhi::RtShaderStage::kMiss:
                    case cd::rhi::RtShaderStage::kCallable:
                        g.generalShader = i;
                        break;
                    case cd::rhi::RtShaderStage::kClosestHit:
                        g.closestHitShader = i;
                        is_hit_group = true;
                        break;
                    case cd::rhi::RtShaderStage::kAnyHit:
                        g.anyHitShader = i;
                        is_hit_group = true;
                        break;
                    case cd::rhi::RtShaderStage::kIntersection:
                        g.intersectionShader = i;
                        is_hit_group = true;
                        break;
                }
            }
            if (!is_hit_group)
                g.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            else if (g.intersectionShader != VK_SHADER_UNUSED_KHR)
                g.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
            else
                g.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
            groups.push_back(g);
        }

        VkRayTracingPipelineCreateInfoKHR pci {};
        pci.sType      = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
        pci.stageCount = static_cast<std::uint32_t>(stages.size());
        pci.pStages    = stages.data();
        pci.groupCount = static_cast<std::uint32_t>(groups.size());
        pci.pGroups    = groups.data();
        pci.maxPipelineRayRecursionDepth = desc.max_recursion;
        pci.layout     = layout_it->second;

        VkPipeline pipeline { VK_NULL_HANDLE };
        // V-PIPECACHE: route RT pipeline creation through the device cache too
        // (deferred-operation handle stays VK_NULL_HANDLE — synchronous build).
        VkResult res = vkCreateRayTracingPipelinesKHR(
            device_, VK_NULL_HANDLE, pipeline_cache_,
            1, &pci, nullptr, &pipeline);
        if (res != VK_SUCCESS || pipeline == VK_NULL_HANDLE)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "vkCreateRayTracingPipelinesKHR failed"));
        }

        const auto id = next_id_++;
        rt_pipelines_.emplace(id, RtPipelineRecord {
            .pipeline    = pipeline,
            .group_count = static_cast<std::uint32_t>(groups.size()),
        });
        return cd::rhi::RtPipelineHandle { id, 1u };
    }

    void destroy_rt_pipeline(cd::rhi::RtPipelineHandle h) override
    {
        auto it = rt_pipelines_.find(h.index());
        if (it == rt_pipelines_.end()) return;
        if (it->second.pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device_, it->second.pipeline, nullptr);
        rt_pipelines_.erase(it);
    }

    [[nodiscard]] std::uint32_t rt_shader_group_handle_size() const noexcept override
    {
        return rt_pipeline_props_.shaderGroupHandleSize;
    }
    [[nodiscard]] std::uint32_t rt_shader_group_handle_alignment() const noexcept override
    {
        return rt_pipeline_props_.shaderGroupHandleAlignment;
    }
    [[nodiscard]] std::uint32_t rt_shader_group_base_alignment() const noexcept override
    {
        return rt_pipeline_props_.shaderGroupBaseAlignment;
    }

    [[nodiscard]] cd::core::Result<void>
    get_rt_shader_group_handles(cd::rhi::RtPipelineHandle pipeline,
                                std::uint32_t first_group,
                                std::uint32_t group_count,
                                std::span<std::byte> out) override
    {
        if (vkGetRayTracingShaderGroupHandlesKHR == nullptr)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kNotImplemented,
                "get_rt_shader_group_handles: extension unavailable"));
        }
        auto it = rt_pipelines_.find(pipeline.index());
        if (it == rt_pipelines_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "get_rt_shader_group_handles: unknown pipeline"));
        }
        const std::size_t needed =
            static_cast<std::size_t>(group_count) * rt_pipeline_props_.shaderGroupHandleSize;
        if (out.size() < needed)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "get_rt_shader_group_handles: output buffer too small"));
        }
        if (vkGetRayTracingShaderGroupHandlesKHR(
                device_, it->second.pipeline,
                first_group, group_count,
                needed, out.data()) != VK_SUCCESS)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "vkGetRayTracingShaderGroupHandlesKHR failed"));
        }
        return {};
    }

    struct BufferMeta
    {
        VkDeviceSize size { 0 };
        cd::rhi::MemoryUsage memory_usage { cd::rhi::MemoryUsage::kAuto };
        // VMA tells us at allocation time whether the heap is host-visible;
        // upload_buffer uses this to reject GPU-only buffers without a round-trip
        // through vkGetMemoryProperties.
        bool host_visible { false };
    };

    void populate_limits()
    {
        VkPhysicalDeviceProperties p {};
        vkGetPhysicalDeviceProperties(physical_, &p);
        adapter_name_ = p.deviceName;
        limits_.max_texture_dimension_1d = p.limits.maxImageDimension1D;
        limits_.max_texture_dimension_2d = p.limits.maxImageDimension2D;
        limits_.max_texture_dimension_3d = p.limits.maxImageDimension3D;
        limits_.max_texture_array_layers = p.limits.maxImageArrayLayers;
        limits_.max_uniform_buffer_range = p.limits.maxUniformBufferRange;
        limits_.max_storage_buffer_range = p.limits.maxStorageBufferRange;
        limits_.max_push_constants_size = p.limits.maxPushConstantsSize;
        limits_.max_bound_descriptor_sets = p.limits.maxBoundDescriptorSets;
        limits_.max_vertex_input_attributes = p.limits.maxVertexInputAttributes;
        limits_.max_vertex_input_bindings = p.limits.maxVertexInputBindings;
        limits_.max_color_attachments = p.limits.maxColorAttachments;
        limits_.max_anisotropy = p.limits.maxSamplerAnisotropy;
        limits_.min_uniform_buffer_offset_alignment = p.limits.minUniformBufferOffsetAlignment;
        limits_.min_storage_buffer_offset_alignment = p.limits.minStorageBufferOffsetAlignment;

        VkPhysicalDeviceFeatures f {};
        vkGetPhysicalDeviceFeatures(physical_, &f);
        features_.sampler_anisotropy = (f.samplerAnisotropy != 0);
        features_.depth_clamp = (f.depthClamp != 0);
        features_.dual_source_blend = (f.dualSrcBlend != 0);
        features_.geometry_shader = (f.geometryShader != 0);
        features_.tessellation_shader = (f.tessellationShader != 0);

        // V-FEAT-TS: timestamp queries are usable iff the device reports
        // timestampComputeAndGraphics (every graphics+compute queue supports
        // vkCmdWriteTimestamp2). D3D12 already sets this flag; Vulkan was
        // leaving it false despite the capability being readily queryable.
        features_.timestamp_queries = (p.limits.timestampComputeAndGraphics != 0);
        // A-QUERY (Backend-to-100 Wave 3a): nanoseconds-per-timestamp-tick.
        // get_query_results multiplies the raw GPU tick delta by this to
        // return a nanosecond duration (0 on adapters without timestamps).
        timestamp_period_ns_ = p.limits.timestampPeriod;

        // V-FEAT-PS: pipeline-statistics queries gate the inactive-but-real
        // pipelineStatisticsQuery feature already queried above. (We do NOT
        // set variable_rate_shading: there is no VRS command surface yet, so
        // lighting it would be a dead flag — deferred to Wave 3.)
        features_.pipeline_statistics_queries = (f.pipelineStatisticsQuery != 0);

        // Phase 12.D / v0.29.0 — RT + mesh shader detection. We probe
        // the device extension list (the cheapest possible signal) and
        // set the coarse-grained capability bits in DeviceFeatures.
        // Wiring the actual ray-tracing-pipeline / mesh-shader pipeline
        // RHI surface lands in Phase 13; today, callers can branch on
        // these bits before writing a code path that would otherwise
        // fail later.
        std::uint32_t ext_count = 0;
        vkEnumerateDeviceExtensionProperties(physical_, nullptr, &ext_count, nullptr);
        std::vector<VkExtensionProperties> exts(ext_count);
        vkEnumerateDeviceExtensionProperties(physical_, nullptr, &ext_count, exts.data());
        const auto has_ext = [&](const char* name) -> bool {
            for (const auto& e : exts)
                if (std::strcmp(e.extensionName, name) == 0)
                    return true;
            return false;
        };
        // Both accel-struct and rt-pipeline must be present for full RT.
        // Ray query is a separate capability that some drivers expose
        // independently of the rt-pipeline path.
        const bool has_accel  = has_ext("VK_KHR_acceleration_structure");
        const bool has_rt_pl  = has_ext("VK_KHR_ray_tracing_pipeline");
        const bool has_rq     = has_ext("VK_KHR_ray_query");
        features_.ray_tracing = has_accel && has_rt_pl;
        features_.ray_query   = has_rq;
        // Prefer the cross-vendor EXT extension over the older NV one;
        // either lights the bit.
        features_.mesh_shader = has_ext("VK_EXT_mesh_shader") ||
                                has_ext("VK_NV_mesh_shader");
        // phase838-W8-BE-rt-bindless-texture-sampling:
        // VK_EXT_descriptor_indexing was promoted to Vulkan 1.2 core.
        // The instance / device is created at API 1.3 (see vmaCreateAllocator
        // call site `.vulkanApiVersion = VK_API_VERSION_1_3`), so a
        // successful device creation guarantees the descriptor-indexing
        // feature bits are honored. Light the bit unconditionally; a
        // vendor / driver that lacks the bits would have failed
        // vkCreateDevice with the enables we pushed (phase838 device
        // creation path) and we would never reach this code.
        features_.bindless_resources = true;

        // Phase 135 — probe RT pipeline properties (handle size +
        // alignment values needed for SBT authoring). Only meaningful
        // when ray_tracing is true; harmless otherwise (struct stays
        // zero-initialized so the getters return 0).
        if (features_.ray_tracing)
        {
            rt_pipeline_props_.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
            VkPhysicalDeviceProperties2 props2 {};
            props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            props2.pNext = &rt_pipeline_props_;
            vkGetPhysicalDeviceProperties2(physical_, &props2);
        }
    }

    std::unique_ptr<VulkanInstance> inst_;
    VkPhysicalDevice physical_ { VK_NULL_HANDLE };
    VkDevice device_ { VK_NULL_HANDLE };
    std::uint32_t graphics_family_ { 0 };
    VkQueue graphics_queue_ { VK_NULL_HANDLE };
    // Vulkan V2 (multi-queue model): async-compute / dedicated-transfer /
    // present families + queues. When a QueueType aliases graphics, the family
    // equals graphics_family_ and the VkQueue equals graphics_queue_, so the
    // routing in submit()/present() degenerates to the legacy single-queue path
    // — byte-identical for the graphics render loop.
    std::uint32_t compute_family_ { 0 };
    std::uint32_t transfer_family_ { 0 };
    std::uint32_t present_family_ { 0 };
    VkQueue compute_queue_ { VK_NULL_HANDLE };
    VkQueue transfer_queue_ { VK_NULL_HANDLE };
    VkQueue present_queue_ { VK_NULL_HANDLE };
    bool compute_dedicated_ { false };
    bool transfer_dedicated_ { false };
    // Constructed in the factory after vkCreateDevice; owned by us.
    VmaAllocator vma_allocator_ { VK_NULL_HANDLE };
    VkCommandPool graphics_pool_ { VK_NULL_HANDLE };
    // Vulkan V2 (multi-queue model): per-QueueType command pools. Each pool is
    // created on its QueueType's family the first time a command buffer for
    // that type is requested. When the family aliases graphics, the pool is a
    // separate VkCommandPool on the SAME family index (pools are not shareable
    // across threads, but here they just partition by usage) — the graphics
    // pool (graphics_pool_) is never touched by the compute/transfer paths.
    VkCommandPool compute_pool_ { VK_NULL_HANDLE };
    VkCommandPool transfer_pool_ { VK_NULL_HANDLE };

    std::string adapter_name_;
    cd::rhi::DeviceLimits limits_ {};
    cd::rhi::DeviceFeatures features_ {};

    struct TextureMeta
    {
        cd::rhi::Format format { cd::rhi::Format::kUndefined };
        cd::rhi::TextureType type { cd::rhi::TextureType::k2D };
        std::uint32_t mip_levels { 1 };
        std::uint32_t array_layers { 1 };
    };

    struct SwapchainRecord
    {
        VkSurfaceKHR surface { VK_NULL_HANDLE };
        VkSwapchainKHR swapchain { VK_NULL_HANDLE };
        std::vector<VkImage> images {};  // owned by the swapchain itself
        /// Synthetic engine-side TextureHandle indices, one per swapchain image,
        /// emplaced into images_ so barrier() can resolve them. Skipped by the
        /// images_ vmaDestroyImage loop in the dtor (cleared first below).
        std::vector<std::uint32_t> image_ids {};
        std::vector<VkImageView> views {};  // owned by us (created in create_swapchain)
        /// Engine-side TextureViewHandle indices, parallel to `views`.
        /// Emplaced into views_ so begin_render_pass() can resolve the
        /// attachment views. Removed from views_ before destroy_swapchain
        /// destroys the underlying VkImageView, so the engine never tries
        /// to vkDestroyImageView them twice.
        std::vector<std::uint32_t> view_ids {};
        VkFormat format { VK_FORMAT_UNDEFINED };
        VkExtent2D extent {};
    };

    // VMA owns every buffer/image allocation. Tracked per-handle so destroy_*
    // can call vmaDestroyBuffer/vmaDestroyImage atomically. The VkBuffer/VkImage
    // mirror in buffers_/images_ is the duplicate VMA hands back.
    std::uint32_t next_id_ { 1 };
    std::unordered_map<std::uint32_t, VkBuffer> buffers_;
    std::unordered_map<std::uint32_t, VmaAllocation> buffer_alloc_;
    std::unordered_map<std::uint32_t, BufferMeta> buffer_meta_;
    std::unordered_map<std::uint32_t, VkShaderModule> shaders_;
    std::unordered_map<std::uint32_t, VkImage> images_;
    std::unordered_map<std::uint32_t, VmaAllocation> image_alloc_;
    std::unordered_map<std::uint32_t, TextureMeta> image_meta_;
    /// Vulkan V1 (depth-aware barrier fix): flat texture-id -> cd::rhi::Format,
    /// maintained alongside image_meta_. Exposed via ResourceTables so the
    /// command buffer can compute the correct VkImageAspectFlags for
    /// depth/stencil image transitions and buffer<->image copies (mirrors the
    /// view_formats_ pattern — a flat map the cmd buffer reads at record time).
    std::unordered_map<std::uint32_t, cd::rhi::Format> image_formats_;
    std::unordered_map<std::uint32_t, VkImageView> views_;
    /// phase1116: view-id -> format, for parallel-lane inheritance info.
    std::unordered_map<std::uint32_t, VkFormat> view_formats_;
    std::unordered_map<std::uint32_t, VkSampler> samplers_;
    // phase843-W8-BE: per-layout metadata so allocate_descriptor_set knows
    // whether to chain VkDescriptorSetVariableDescriptorCountAllocateInfo.
    struct DescriptorSetLayoutMeta
    {
        VkDescriptorSetLayout layout                  { VK_NULL_HANDLE };
        std::uint32_t         variable_count_max      { 0u };   // 0 = no variable binding
    };
    std::unordered_map<std::uint32_t, VkDescriptorSetLayout> set_layouts_;
    std::unordered_map<std::uint32_t, DescriptorSetLayoutMeta> set_layout_meta_;
    std::unordered_map<std::uint32_t, VkPipelineLayout> pipeline_layouts_;
    std::unordered_map<std::uint32_t, VkPipeline> compute_pipelines_;
    std::unordered_map<std::uint32_t, VkPipeline> graphics_pipelines_;
    /// pipeline-id → VkPipelineLayout it was built against. Used by the
    /// command buffer so bind_descriptor_set can supply the layout the bound
    /// pipeline expects without the caller passing it again.
    std::unordered_map<std::uint32_t, VkPipelineLayout> pipeline_to_layout_;
    VkDescriptorPool descriptor_pool_ { VK_NULL_HANDLE };
    std::unordered_map<std::uint32_t, VkDescriptorSet> descriptor_sets_;
    std::unordered_map<std::uint32_t, SwapchainRecord> swapchains_;

    // Phase 17.A — acceleration-structure storage.
    // Phase 130 — record stores deep copy of build-input lists.
    // Phase 132 — record also carries per-AS scratch buffer + device
    // address + Vulkan-format triangle geometry list so
    // `vkCmdBuildAccelerationStructuresKHR` can fire with no further
    // allocation from the cmd-buffer side.
    // Phase 133 — TLAS instance buffer (host-visible VkAS InstanceKHR
    // array). BLAS leaves these null.
    struct AccelRecord
    {
        VkAccelerationStructureKHR as { VK_NULL_HANDLE };
        VkBuffer storage_buf { VK_NULL_HANDLE };
        VmaAllocation storage_alloc { VK_NULL_HANDLE };
        VkDeviceSize scratch_size { 0 };
        VkBuffer scratch_buf { VK_NULL_HANDLE };
        VmaAllocation scratch_alloc { VK_NULL_HANDLE };
        VkDeviceAddress scratch_device_address { 0 };
        VkDeviceAddress as_device_address { 0 };
        // A-AS-FLAGS / A-REFIT / A-COMPACTION (Backend-to-100 Wave 3b):
        //   build_flags         — re-applied by the cmd-buffer build/refit path
        //                          (Vulkan requires build flags to match the
        //                          size-query flags).
        //   update_scratch_size — scratch bytes a MODE_UPDATE refit needs.
        //   as_size             — result-data size reported by the size query;
        //                          exposed via acceleration_structure_size and
        //                          used as the compaction shrink baseline.
        cd::rhi::AccelBuildFlags build_flags { cd::rhi::AccelBuildFlags::kPreferFastTrace };
        VkDeviceSize update_scratch_size { 0 };
        VkDeviceSize as_size { 0 };
        cd::rhi::AccelStructureKind kind { cd::rhi::AccelStructureKind::kBottomLevel };
        std::vector<cd::rhi::AccelTriangleGeometry> triangles;
        std::vector<cd::rhi::AccelInstance>         instances;
        VkBuffer        instance_buf { VK_NULL_HANDLE };
        VmaAllocation   instance_alloc { VK_NULL_HANDLE };
        VkDeviceAddress instance_device_address { 0 };
        // Phase 132 — Vulkan-format triangle list cached at create
        // time so the cmd-buffer build path can reference it without
        // rebuilding from the engine descriptor.
        std::vector<VkAccelerationStructureGeometryKHR> vk_triangle_geos;
        std::vector<std::uint32_t>                      vk_primitive_counts;
    };
    std::unordered_map<std::uint32_t, AccelRecord> accels_;

    // phase838-W8-BE-rt-bindless-texture-sampling: bindless texture
    // array storage. Each array owns its own VkDescriptorPool (so the
    // UPDATE_AFTER_BIND flag lives at pool granularity),
    // VkDescriptorSetLayout (with the descriptor-indexing binding
    // flags), and VkDescriptorSet (allocated with
    // VARIABLE_DESCRIPTOR_COUNT). Callers retrieve the descriptor set
    // / layout / sampler through dedicated accessors when wiring it
    // into a pipeline layout. The lifetime is owner-controlled via
    // create_bindless_texture_array / destroy_bindless_texture_array.
    struct BindlessTextureArrayRecord
    {
        VkDescriptorPool      pool       { VK_NULL_HANDLE };
        VkDescriptorSetLayout set_layout { VK_NULL_HANDLE };
        VkDescriptorSet       set        { VK_NULL_HANDLE };
        VkSampler             sampler    { VK_NULL_HANDLE };
        std::uint32_t         slot_count { 0 };
    };
    std::unordered_map<std::uint32_t, BindlessTextureArrayRecord> bindless_arrays_;
    std::uint32_t next_bindless_id_ { 1 };

    // Phase 135 — RT pipeline storage + cached props.
    struct RtPipelineRecord
    {
        VkPipeline    pipeline { VK_NULL_HANDLE };
        std::uint32_t group_count { 0 };
    };
    std::unordered_map<std::uint32_t, RtPipelineRecord> rt_pipelines_;
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rt_pipeline_props_ {};
    std::unordered_map<std::uint32_t, VkSemaphore> semaphores_;
    // Timeline semaphores live in their own map even though Vulkan represents
    // both flavors via VkSemaphore; the separation lets the type system catch
    // misuse (binary↔timeline are not interchangeable at the API level).
    std::unordered_map<std::uint32_t, VkSemaphore> timeline_semaphores_;
    std::unordered_map<std::uint32_t, VkFence> fences_;

    // A-QUERY (Backend-to-100 Wave 3a): VkQueryPool storage + the query
    // type each pool was created with (get_query_results applies the
    // timestamp period only to kTimestamp pools). `timestamp_period_ns_` is
    // captured from VkPhysicalDeviceLimits::timestampPeriod in populate_limits.
    struct QueryPoolRecord
    {
        VkQueryPool        pool { VK_NULL_HANDLE };
        cd::rhi::QueryType type { cd::rhi::QueryType::kTimestamp };
        std::uint32_t      count { 0 };
    };
    std::unordered_map<std::uint32_t, QueryPoolRecord> query_pools_;
    // Flat query-pool-id -> VkQueryPool mirror handed to the cmd-buffer
    // ResourceTables (same pattern as buffers_/images_); kept in sync with
    // query_pools_ on create/destroy.
    std::unordered_map<std::uint32_t, VkQueryPool> query_pool_handles_;
    float timestamp_period_ns_ { 1.0f };

    /// VkPipelineCache used by vkCreate{Graphics,Compute}Pipelines.
    /// Loaded from disk on first create_*_pipeline call (lazy because the
    /// device ctor runs before we know the cache path is writable), saved
    /// to disk in the dtor. Empty cache is a valid state — Vulkan just
    /// builds pipelines from scratch as if VK_NULL_HANDLE were passed.
    VkPipelineCache pipeline_cache_ { VK_NULL_HANDLE };

    /// V-PIPECACHE — in-memory seed blob from VulkanCreateInfo, consulted by
    /// init_pipeline_cache_() before the on-disk fallback. Empty when the caller
    /// supplied no blob (the legacy disk path then drives the seed).
    std::vector<std::byte> pipeline_cache_seed_ {};
};

// ---------------------------------------------------------------------------

namespace
{

[[nodiscard]] cd::core::Result<VkPhysicalDevice> pick_physical_device(VkInstance instance, bool prefer_discrete)
{
    std::uint32_t count { 0 };
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (count == 0)
    {
        return std::unexpected(
            cd::rhi::rhi_errors::make(cd::rhi::rhi_errors::Code::kNoSuitableAdapter, "no Vulkan devices")
        );
    }
    std::vector<VkPhysicalDevice> devs(count);
    vkEnumeratePhysicalDevices(instance, &count, devs.data());

    // Optional explicit selection via env variable, useful for
    // cross-vendor validation (Phase 11 Track B): when the loader sees
    // multiple physical devices, the test harness can pin a specific
    // index without modifying any sample. Value out of range falls
    // back to the default policy below.
    // MSVC marks std::getenv deprecated in favour of _dupenv_s; we accept
    // the read-only env lookup as safe (single thread at device-create
    // time, no buffer manipulation) and silence the deprecation locally.
#if defined(_MSC_VER) || defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
    // std::getenv is concurrency-mt-unsafe, but we only call it once during
    // device init from the main thread before any worker threads exist.
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const char* env_index = std::getenv("CD_VULKAN_DEVICE_INDEX");
#if defined(_MSC_VER) || defined(__clang__)
    #pragma clang diagnostic pop
#endif
    if (env_index != nullptr)
    {
        char* end = nullptr;
        // NOLINTNEXTLINE(google-runtime-int) — strtol returns long by C ABI.
        const long idx_long = std::strtol(env_index, &end, 10);
        const int idx = static_cast<int>(idx_long);
        if (end != env_index && idx >= 0 && static_cast<std::uint32_t>(idx) < devs.size())
        {
            VkPhysicalDeviceProperties p {};
            vkGetPhysicalDeviceProperties(devs[static_cast<std::size_t>(idx)], &p);
            std::fprintf(stderr,
                         "[cd-rhi-vulkan] CD_VULKAN_DEVICE_INDEX=%d -> %s (vendor=0x%04x)\n",
                         idx, p.deviceName, p.vendorID);
            return devs[static_cast<std::size_t>(idx)];
        }
        std::fprintf(stderr,
                     "[cd-rhi-vulkan] CD_VULKAN_DEVICE_INDEX=%d out of range [0,%u); falling back to default policy\n",
                     idx, count);
    }

    VkPhysicalDevice chosen = devs.front();
    if (prefer_discrete)
    {
        for (auto d : devs)
        {
            VkPhysicalDeviceProperties p {};
            vkGetPhysicalDeviceProperties(d, &p);
            if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            {
                chosen = d;
                break;
            }
        }
    }
    return chosen;
}

}  // namespace

// Vulkan V2 (multi-queue model) — queue-family selection. Policy:
//   * GRAPHICS family: first family with GRAPHICS_BIT. Vulkan guarantees a
//     graphics family also reports COMPUTE_BIT + TRANSFER_BIT, so this is the
//     universal fallback for every QueueType.
//   * ASYNC-COMPUTE family: prefer a family with COMPUTE_BIT but NOT
//     GRAPHICS_BIT (a true async-compute engine); else alias to graphics.
//   * DEDICATED-TRANSFER family: prefer a family with TRANSFER_BIT but neither
//     GRAPHICS_BIT nor COMPUTE_BIT (a pure DMA/copy engine); else alias to
//     graphics.
//   * PRESENT family: prefer graphics when it supports the surface (the common
//     case — present on the render queue); else the first present-capable
//     family; else alias to graphics (headless / no surface probed).
// Overlap/alias is the NORMAL outcome on GPUs that expose one universal family;
// callers dedupe by index before emitting VkDeviceQueueCreateInfos.
[[nodiscard]] cd::core::Result<QueueFamilySelection>
select_queue_families(VkPhysicalDevice pd, VkSurfaceKHR surface) noexcept
{
    std::uint32_t count { 0 };
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &count, families.data());

    constexpr VkQueueFlags kG = VK_QUEUE_GRAPHICS_BIT;
    constexpr VkQueueFlags kC = VK_QUEUE_COMPUTE_BIT;
    constexpr VkQueueFlags kT = VK_QUEUE_TRANSFER_BIT;

    // ---- graphics (required) ------------------------------------------------
    std::uint32_t graphics = count;  // sentinel = not found
    for (std::uint32_t i = 0; i < count; ++i)
    {
        if ((families[i].queueFlags & kG) != 0)
        {
            graphics = i;
            break;
        }
    }
    if (graphics == count)
    {
        return std::unexpected(
            cd::rhi::rhi_errors::make(cd::rhi::rhi_errors::Code::kNoSuitableAdapter, "no graphics queue family")
        );
    }

    QueueFamilySelection sel {};
    sel.graphics = graphics;
    sel.compute = graphics;   // default alias
    sel.transfer = graphics;  // default alias
    sel.present = graphics;   // default alias

    // ---- async-compute: COMPUTE without GRAPHICS ----------------------------
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const VkQueueFlags f = families[i].queueFlags;
        if ((f & kC) != 0 && (f & kG) == 0)
        {
            sel.compute = i;
            sel.compute_dedicated = true;
            break;
        }
    }

    // ---- dedicated transfer: TRANSFER without GRAPHICS or COMPUTE -----------
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const VkQueueFlags f = families[i].queueFlags;
        if ((f & kT) != 0 && (f & kG) == 0 && (f & kC) == 0)
        {
            sel.transfer = i;
            sel.transfer_dedicated = true;
            break;
        }
    }

    // ---- present: prefer graphics, else any present-capable family ----------
    if (surface != VK_NULL_HANDLE)
    {
        sel.present_resolved = true;
        VkBool32 gfx_present { VK_FALSE };
        vkGetPhysicalDeviceSurfaceSupportKHR(pd, graphics, surface, &gfx_present);
        if (gfx_present == VK_TRUE)
        {
            sel.present = graphics;
        }
        else
        {
            for (std::uint32_t i = 0; i < count; ++i)
            {
                VkBool32 supported { VK_FALSE };
                vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface, &supported);
                if (supported == VK_TRUE)
                {
                    sel.present = i;
                    break;
                }
            }
        }
    }

    return sel;
}

[[nodiscard]] cd::core::Result<std::unique_ptr<cd::rhi::IDevice>> create_device(
    std::unique_ptr<VulkanInstance> inst,
    const std::vector<std::string>& device_extensions,
    bool prefer_discrete,
    std::vector<std::byte> pipeline_cache_blob
)
{
    auto pd_res = pick_physical_device(inst->instance, prefer_discrete);
    if (!pd_res.has_value())
        return std::unexpected(pd_res.error());
    VkPhysicalDevice pd = *pd_res;

    // Vulkan V2 (multi-queue model): select graphics / async-compute /
    // dedicated-transfer / present families. Device creation has no surface
    // yet, so present is resolved per-swapchain later (and aliases to graphics
    // until then); we pass VK_NULL_HANDLE here. The graphics family is the
    // required one and drives the unchanged render path.
    auto qf_res = select_queue_families(pd, VK_NULL_HANDLE);
    if (!qf_res.has_value())
        return std::unexpected(qf_res.error());
    const QueueFamilySelection sel = *qf_res;
    const std::uint32_t qf = sel.graphics;

    std::vector<const char*> exts;
    exts.reserve(device_extensions.size() + 2);
    for (const auto& e : device_extensions)
        exts.push_back(e.c_str());
    // Swapchain: enabled by default so create_swapchain just works. dynamic_
    // rendering: Vulkan 1.3 core feature, no extension string needed.
    exts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    // ---- Ray tracing extension auto-enable (Phase 15.E / Wave 170)
    //
    // Probe the device's extension list once here; auto-add the
    // KHR ray-tracing trio when ALL THREE are present:
    //   VK_KHR_acceleration_structure   — BLAS / TLAS build
    //   VK_KHR_ray_tracing_pipeline     — tracing pipeline + SBT
    //   VK_KHR_deferred_host_operations — required dependency
    // If any of the three is missing the entire RT path stays off
    // (matches Phase 12.D detection contract). Ray-query is enabled
    // independently when its extension is present.
    bool rt_enabled = false;
    bool rq_enabled = false;
    bool mesh_shader_enabled = false;
    {
        std::uint32_t count = 0;
        vkEnumerateDeviceExtensionProperties(pd, nullptr, &count, nullptr);
        std::vector<VkExtensionProperties> available(count);
        vkEnumerateDeviceExtensionProperties(pd, nullptr, &count, available.data());
        auto has = [&](const char* name) {
            for (const auto& e : available)
                if (std::strcmp(e.extensionName, name) == 0) return true;
            return false;
        };
        const bool a = has("VK_KHR_acceleration_structure");
        const bool p = has("VK_KHR_ray_tracing_pipeline");
        const bool d = has("VK_KHR_deferred_host_operations");
        if (a && p && d)
        {
            exts.push_back("VK_KHR_acceleration_structure");
            exts.push_back("VK_KHR_ray_tracing_pipeline");
            exts.push_back("VK_KHR_deferred_host_operations");
            rt_enabled = true;
        }
        rq_enabled = has("VK_KHR_ray_query");
        if (rq_enabled)
            exts.push_back("VK_KHR_ray_query");

        // Phase 765 W2A F5 — Mesh shader (Nanite-class virtual geometry).
        // Prefer the cross-vendor VK_EXT_mesh_shader; fall back to NV's
        // legacy VK_NV_mesh_shader on older NVIDIA-only ICDs. The detection
        // bit was already lit up in init_features_() at Phase 12.D; this
        // factory path lights the *enablement* so vkCreateDevice accepts
        // the meshShader / taskShader feature bits we chain in below.
        if (has("VK_EXT_mesh_shader"))
        {
            exts.push_back("VK_EXT_mesh_shader");
            mesh_shader_enabled = true;
        }
        else if (has("VK_NV_mesh_shader"))
        {
            exts.push_back("VK_NV_mesh_shader");
            mesh_shader_enabled = true;
        }
    }

    // Vulkan V2 (multi-queue model): emit ONE VkDeviceQueueCreateInfo per
    // DISTINCT family index. You cannot list the same family twice with
    // separate infos (VUID-VkDeviceCreateInfo-queueFamilyIndex-02802); when two
    // QueueTypes share a family we request a single queue and ALIAS the VkQueue
    // handle after creation. Dedupe preserves graphics first so the unchanged
    // render path is unaffected.
    const float queue_priority = 1.0F;
    std::vector<std::uint32_t> distinct_families;
    distinct_families.reserve(4);
    const auto add_distinct = [&](std::uint32_t fam) {
        for (std::uint32_t existing : distinct_families)
            if (existing == fam)
                return;
        distinct_families.push_back(fam);
    };
    add_distinct(sel.graphics);
    add_distinct(sel.compute);
    add_distinct(sel.transfer);
    add_distinct(sel.present);

    std::vector<VkDeviceQueueCreateInfo> qcis;
    qcis.reserve(distinct_families.size());
    for (std::uint32_t fam : distinct_families)
    {
        qcis.push_back(VkDeviceQueueCreateInfo {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queueFamilyIndex = fam,
            .queueCount = 1,
            .pQueuePriorities = &queue_priority,
        });
    }

    // Vulkan 1.3 core features we rely on must be enabled explicitly via the
    // pNext chain — drivers that gate them behind enablement will reject the
    // device otherwise (Mesa/AMDVLK do; NVIDIA tends to be permissive). The
    // chain head is VkPhysicalDeviceFeatures2 so VkDeviceCreateInfo::pEnabled-
    // Features must be null (cannot mix the two paths).
    //
    // Required for the current backend:
    //   * dynamicRendering   — create_graphics_pipeline targets dynamic rendering.
    //   * synchronization2   — submit() uses vkQueueSubmit2 + barrier() uses
    //                          VkImageMemoryBarrier2 via vkCmdPipelineBarrier2.
    //   * timelineSemaphore  — required for the timeline-semaphore API surface
    //                          (host wait/signal/value) we expose to callers.
    // Value-init + assign specific fields rather than a designated-initializer
    // list: these feature structs have ~60 fields and Clang's -Wmissing-
    // designated-field-initializers would flag every omitted bool.
    VkPhysicalDeviceVulkan13Features f13 {};
    f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    f13.pNext = nullptr;
    f13.synchronization2 = VK_TRUE;
    f13.dynamicRendering = VK_TRUE;

    VkPhysicalDeviceVulkan12Features f12 {};
    f12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    f12.pNext = &f13;
    f12.timelineSemaphore = VK_TRUE;
    // bufferDeviceAddress is required by VK_KHR_acceleration_structure
    // (the AS build path consumes GPU pointers via VkDeviceAddress).
    // Enable it when RT was opted in; no-op otherwise.
    if (rt_enabled)
        f12.bufferDeviceAddress = VK_TRUE;
    // phase838-W8-BE-rt-bindless-texture-sampling:
    // VK_EXT_descriptor_indexing was promoted to Vulkan 1.2 core, so the
    // feature bits live directly on f12 — no separate extension to push.
    // We turn on the four bits required by ADR W8-BE's bindless
    // texture-array path:
    //   * runtimeDescriptorArray — sampler2D arrays sized at runtime.
    //   * shaderSampledImageArrayNonUniformIndexing — runtime-indexed
    //     reads with non-uniform indices (chrome reflections hit different
    //     prims per pixel).
    //   * descriptorBindingSampledImageUpdateAfterBind — write a slot
    //     after the descriptor set is bound but before use.
    //   * descriptorBindingPartiallyBound — keep empty slots valid.
    //   * descriptorBindingVariableDescriptorCount — last binding's
    //     descriptor count is set at allocation time.
    //
    // Drivers that don't expose VK_EXT_descriptor_indexing pre-1.2 are
    // out of scope for the engine target matrix; the rhi falls back to
    // kNotImplemented in `create_bindless_texture_array` and the chrome
    // path falls back to the W8-BD per-prim avg-colour reflection.
    f12.runtimeDescriptorArray                          = VK_TRUE;
    f12.shaderSampledImageArrayNonUniformIndexing       = VK_TRUE;
    f12.descriptorBindingSampledImageUpdateAfterBind    = VK_TRUE;
    f12.descriptorBindingPartiallyBound                 = VK_TRUE;
    f12.descriptorBindingVariableDescriptorCount        = VK_TRUE;

    // RT feature structs — only chained when rt_enabled. The driver
    // would reject vkCreateDevice if we requested a feature without
    // its required extension also enabled, so the guard matches the
    // extension push above.
    VkPhysicalDeviceAccelerationStructureFeaturesKHR f_as {};
    f_as.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    f_as.accelerationStructure = VK_TRUE;
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR f_rtp {};
    f_rtp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    f_rtp.rayTracingPipeline = VK_TRUE;
    VkPhysicalDeviceRayQueryFeaturesKHR f_rq {};
    f_rq.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    f_rq.rayQuery = VK_TRUE;
    if (rt_enabled)
    {
        // Chain: f12 → f_as → f_rtp → existing-tail
        f_as.pNext  = f12.pNext;
        f_rtp.pNext = &f_as;
        f12.pNext   = &f_rtp;
    }
    if (rq_enabled)
    {
        f_rq.pNext = f12.pNext;
        f12.pNext  = &f_rq;
    }

    // Phase 765 W2A F5 — Chain VkPhysicalDeviceMeshShaderFeaturesEXT when the
    // extension was enabled above. The mesh-shader feature bits are gated
    // exactly like the RT structs: the driver rejects vkCreateDevice if the
    // pNext bit is requested without the matching extension also in the
    // enabled list. Backends without VK_EXT_mesh_shader skip the entire
    // chain element.
    VkPhysicalDeviceMeshShaderFeaturesEXT f_mesh {};
    f_mesh.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
    f_mesh.meshShader = VK_TRUE;
    f_mesh.taskShader = VK_TRUE;
    if (mesh_shader_enabled)
    {
        f_mesh.pNext = f12.pNext;
        f12.pNext   = &f_mesh;
    }

    // Vulkan 1.4 features (opt-in). Headers ≥ 1.4 SDK define the structure;
    // the device only honors it when the driver supports 1.4 AND we present
    // an API version that covers 1.4. Engines that don't need 1.4 (the
    // default) skip the entire chain element so older drivers stay happy.
#if CD_RHI_VULKAN_14 && defined(VK_API_VERSION_1_4)
    VkPhysicalDeviceVulkan14Features f14 {};
    f14.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
    f14.pNext = &f12;
    // Headline 1.4 features: push descriptors are core (was extension),
    // maintenance5/6 add the modern vkCmdBindIndexBuffer2 / Bind*2 fast paths,
    // dynamicRenderingLocalRead enables tile-based read-modify-write.
    f14.pushDescriptor = VK_TRUE;
    f14.maintenance5 = VK_TRUE;
    f14.maintenance6 = VK_TRUE;
    f14.dynamicRenderingLocalRead = VK_TRUE;
    void* feature_chain_head = &f14;
#else
    void* feature_chain_head = &f12;
#endif

    const VkPhysicalDeviceFeatures2 f2 {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = feature_chain_head,
        .features = {},  // base features stay off until requested
    };

    const VkDeviceCreateInfo dci {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &f2,
        .flags = 0,
        .queueCreateInfoCount = static_cast<std::uint32_t>(qcis.size()),
        .pQueueCreateInfos = qcis.data(),
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = static_cast<std::uint32_t>(exts.size()),
        .ppEnabledExtensionNames = exts.data(),
        .pEnabledFeatures = nullptr,  // cannot coexist with VkPhysicalDeviceFeatures2 in pNext
    };

    VkDevice dev {};
    if (vkCreateDevice(pd, &dci, nullptr, &dev) != VK_SUCCESS)
    {
        return std::unexpected(
            cd::rhi::rhi_errors::make(cd::rhi::rhi_errors::Code::kBackendInitFailed, "vkCreateDevice failed")
        );
    }
    volkLoadDevice(dev);

    // Vulkan V2 (multi-queue model): retrieve ONE VkQueue per distinct family
    // (queueIndex 0 — we requested queueCount=1 per family). QueueTypes that
    // share a family alias the same handle, which is correct: submitting to the
    // same VkQueue from two QueueType slots is just sequential submission on the
    // one queue (the legacy single-queue behaviour, preserved exactly for the
    // graphics path).
    VkQueue q { VK_NULL_HANDLE };  // graphics queue (unchanged render path)
    vkGetDeviceQueue(dev, qf, 0, &q);
    VkQueue compute_q { VK_NULL_HANDLE };
    vkGetDeviceQueue(dev, sel.compute, 0, &compute_q);
    VkQueue transfer_q { VK_NULL_HANDLE };
    vkGetDeviceQueue(dev, sel.transfer, 0, &transfer_q);
    VkQueue present_q { VK_NULL_HANDLE };
    vkGetDeviceQueue(dev, sel.present, 0, &present_q);

    VkPhysicalDeviceProperties props {};
    vkGetPhysicalDeviceProperties(pd, &props);

    // VMA bootstraps the rest of its function table from these two entry
    // points (VMA_DYNAMIC_VULKAN_FUNCTIONS=1); volk's globally-resolved
    // pointers satisfy them.
    VmaVulkanFunctions vma_fns {};
    vma_fns.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vma_fns.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    // Phase 141: when ray-tracing is enabled the engine creates
    // SBT buffers + acceleration-structure scratch buffers that
    // require VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT — VMA must
    // also be told about device-address support or it asserts on the
    // first SBT allocation.
    const VmaAllocatorCreateFlags vma_flags =
        rt_enabled ? VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT : 0;

    const VmaAllocatorCreateInfo vaci {
        .flags = vma_flags,
        .physicalDevice = pd,
        .device = dev,
        .preferredLargeHeapBlockSize = 0,  // VMA default (256 MiB)
        .pAllocationCallbacks = nullptr,
        .pDeviceMemoryCallbacks = nullptr,
        .pHeapSizeLimit = nullptr,
        .pVulkanFunctions = &vma_fns,
        .instance = inst->instance,
        .vulkanApiVersion = VK_API_VERSION_1_3,
        .pTypeExternalMemoryHandleTypes = nullptr,
    };
    VmaAllocator allocator { VK_NULL_HANDLE };
    if (vmaCreateAllocator(&vaci, &allocator) != VK_SUCCESS)
    {
        vkDestroyDevice(dev, nullptr);
        return std::unexpected(
            cd::rhi::rhi_errors::make(cd::rhi::rhi_errors::Code::kBackendInitFailed, "vmaCreateAllocator failed")
        );
    }

    return std::unique_ptr<cd::rhi::IDevice> {
        std::make_unique<VulkanDevice>(
            std::move(inst), pd, dev, qf, q, allocator, props.deviceName,
            VulkanDevice::QueueSet {
                .compute_family = sel.compute,
                .transfer_family = sel.transfer,
                .present_family = sel.present,
                .compute_queue = compute_q,
                .transfer_queue = transfer_q,
                .present_queue = present_q,
                .compute_dedicated = sel.compute_dedicated,
                .transfer_dedicated = sel.transfer_dedicated,
            },
            std::move(pipeline_cache_blob))
    };
}

// ---------------------------------------------------------------------------
// Bridge for cd::rhi::vulkan::get_native(IDevice&) — defined here because
// `VulkanDevice` is a private class of this TU. NativeHandles.cpp calls
// through this trampoline so it doesn't need the full class layout.
// ---------------------------------------------------------------------------

bool try_fill_native_handles(
    cd::rhi::IDevice& dev,
    VkInstance* out_instance,
    VkPhysicalDevice* out_physical,
    VkDevice* out_device,
    VkQueue* out_graphics_queue,
    std::uint32_t* out_graphics_family
) noexcept
{
    auto* concrete = dynamic_cast<VulkanDevice*>(&dev);
    if (concrete == nullptr)
        return false;
    if (out_instance != nullptr)
        *out_instance = concrete->native_instance();
    if (out_physical != nullptr)
        *out_physical = concrete->native_physical_device();
    if (out_device != nullptr)
        *out_device = concrete->native_device();
    if (out_graphics_queue != nullptr)
        *out_graphics_queue = concrete->graphics_queue();
    if (out_graphics_family != nullptr)
        *out_graphics_family = concrete->graphics_family();
    return true;
}

// Vulkan V2 (multi-queue model) — public introspection bridge. Defined here
// because VulkanDevice is a private class of this TU.
bool query_queue_families(cd::rhi::IDevice& dev, QueueFamilyInfo& out) noexcept
{
    auto* concrete = dynamic_cast<VulkanDevice*>(&dev);
    if (concrete == nullptr)
        return false;
    out.graphics = concrete->graphics_family();
    out.compute = concrete->compute_family();
    out.transfer = concrete->transfer_family();
    out.present = concrete->present_family();
    out.compute_dedicated = concrete->compute_family_dedicated();
    out.transfer_dedicated = concrete->transfer_family_dedicated();
    return true;
}

}  // namespace cd::rhi::vulkan
