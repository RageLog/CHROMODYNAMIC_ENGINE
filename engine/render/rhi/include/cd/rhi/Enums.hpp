// =============================================================================
// CHROMODYNAMIC — cd/rhi/Enums.hpp
// ADR-001 (Sprint S3.0) — RHI pipeline state enums + flag bitsets.
//
// Conventions:
//   * Enums of strongly-typed numeric tags use `enum class : underlying-type`.
//   * Bit-flag enums are typed `enum class : std::uint32_t` and accompanied by
//     bitwise operator overloads. Use `has(flags, FlagA::kX)` to test.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <type_traits>

namespace cd::rhi
{

// ---- Pipeline state -------------------------------------------------------

enum class PrimitiveTopology : std::uint8_t
{
    kPointList = 0,
    kLineList,
    kLineStrip,
    kTriangleList,
    kTriangleStrip,
    kTriangleFan,
};

enum class PolygonMode : std::uint8_t
{
    kFill,
    kLine,
    kPoint
};

enum class CullMode : std::uint8_t
{
    kNone,
    kFront,
    kBack,
    kFrontAndBack
};

enum class FrontFace : std::uint8_t
{
    kCounterClockwise,
    kClockwise
};

enum class CompareOp : std::uint8_t
{
    kNever,
    kLess,
    kEqual,
    kLessEqual,
    kGreater,
    kNotEqual,
    kGreaterEqual,
    kAlways,
};

enum class StencilOp : std::uint8_t
{
    kKeep,
    kZero,
    kReplace,
    kIncrementClamp,
    kDecrementClamp,
    kInvert,
    kIncrementWrap,
    kDecrementWrap,
};

enum class BlendOp : std::uint8_t
{
    kAdd,
    kSubtract,
    kReverseSubtract,
    kMin,
    kMax,
};

enum class BlendFactor : std::uint8_t
{
    kZero,
    kOne,
    kSrcColor,
    kOneMinusSrcColor,
    kDstColor,
    kOneMinusDstColor,
    kSrcAlpha,
    kOneMinusSrcAlpha,
    kDstAlpha,
    kOneMinusDstAlpha,
    kConstantColor,
    kOneMinusConstantColor,
    kConstantAlpha,
    kOneMinusConstantAlpha,
    kSrcAlphaSaturate,
};

// ---- Texture / sampler ----------------------------------------------------

enum class TextureType : std::uint8_t
{
    k1D,
    k2D,
    k3D,
    kCube,
    k1DArray,
    k2DArray,
    kCubeArray,
};

enum class SamplerFilter : std::uint8_t
{
    kNearest,
    kLinear
};
enum class SamplerMipmapMode : std::uint8_t
{
    kNearest,
    kLinear
};
enum class SamplerAddressMode : std::uint8_t
{
    kRepeat,
    kMirroredRepeat,
    kClampToEdge,
    kClampToBorder,
    kMirrorClampToEdge,
};
enum class BorderColor : std::uint8_t
{
    kFloatTransparentBlack,
    kFloatOpaqueBlack,
    kFloatOpaqueWhite,
    kIntTransparentBlack,
    kIntOpaqueBlack,
    kIntOpaqueWhite,
};

// ---- Pass operations ------------------------------------------------------

enum class LoadOp : std::uint8_t
{
    kLoad,
    kClear,
    kDontCare
};
enum class StoreOp : std::uint8_t
{
    kStore,
    kDontCare
};

// ---- Shader stages --------------------------------------------------------

enum class ShaderStage : std::uint32_t
{
    kNone = 0u,
    kVertex = 1u << 0,
    kFragment = 1u << 1,
    kGeometry = 1u << 2,
    kTessControl = 1u << 3,
    kTessEval = 1u << 4,
    kCompute = 1u << 5,
    kRayGen = 1u << 6,
    kAnyHit = 1u << 7,
    kClosestHit = 1u << 8,
    kMiss = 1u << 9,
    kIntersection = 1u << 10,
    kCallable = 1u << 11,
    kMesh = 1u << 12,
    kTask = 1u << 13,

    kAllGraphics = kVertex | kFragment | kGeometry | kTessControl | kTessEval,
};

// ---- Shader source language (D12, phase1184) ------------------------------
//
// Describes what `ShaderModuleDesc::code` actually contains, so a backend's
// `create_shader_module` can route it through the right (cross-)compile
// path. `kBytecode` (the default) is the legacy contract: `code` is already
// the backend's native binary — SPIR-V words for Vulkan, DXIL/DXBC for
// D3D12 — and is consumed verbatim (no recompilation, byte-identical to the
// pre-D12 behaviour). The non-default values let a caller hand the device a
// *source* blob and ask the backend to cross-compile to its native binary
// (e.g. the engine GLSL corpus → DXIL on D3D12 via SPIR-V→HLSL→DXIL).
//
// Not every backend implements every language: a backend that cannot
// cross-compile a given language returns a typed error from
// `create_shader_module` rather than crashing or silently producing an
// empty module.
enum class ShaderSourceLanguage : std::uint8_t
{
    kBytecode = 0,  ///< Native binary (SPIR-V on Vulkan, DXIL/DXBC on D3D12) — pass-through.
    kGlsl,          ///< Vulkan-style GLSL source (the engine corpus). cd::gluon #includes resolve.
    kSpirv,         ///< SPIR-V words presented as a source to be re-targeted (D3D12: SPIR-V→HLSL→DXIL).
    kHlsl,          ///< HLSL source (D3D12 only) compiled straight through DXC.
};

// ---- Resource usage flags -------------------------------------------------

enum class BufferUsage : std::uint32_t
{
    kNone = 0u,
    kTransferSrc = 1u << 0,
    kTransferDst = 1u << 1,
    kUniform = 1u << 2,
    kStorage = 1u << 3,
    kVertex = 1u << 4,
    kIndex = 1u << 5,
    kIndirect = 1u << 6,
    kQuery = 1u << 7,
    // Phase 141 — buffers carrying the SBT regions consumed by
    // vkCmdTraceRaysKHR / DispatchRays. Backends implicitly also
    // enable shader-device-address since SBT regions are addressed
    // by device VA.
    kShaderBindingTable = 1u << 8,
};

enum class TextureUsage : std::uint32_t
{
    kNone = 0u,
    kTransferSrc = 1u << 0,
    kTransferDst = 1u << 1,
    kSampled = 1u << 2,
    kStorage = 1u << 3,
    kColorAttachment = 1u << 4,
    kDepthStencilAttachment = 1u << 5,
    kInputAttachment = 1u << 6,
};

enum class MemoryUsage : std::uint8_t
{
    kAuto,      // RHI picks (recommended)
    kGpuOnly,   // device-local
    kCpuToGpu,  // staging upload
    kGpuToCpu,  // readback
    kCpuRandomAccess,
};

// ---- Resource state (for transitions / barriers) --------------------------

enum class ResourceState : std::uint32_t
{
    kUndefined = 0u,
    kCommon = 1u << 0,
    kVertexBuffer = 1u << 1,
    kIndexBuffer = 1u << 2,
    kConstantBuffer = 1u << 3,
    kShaderResource = 1u << 4,
    kUnorderedAccess = 1u << 5,
    kColorAttachment = 1u << 6,
    kDepthRead = 1u << 7,
    kDepthWrite = 1u << 8,
    kTransferSrc = 1u << 9,
    kTransferDst = 1u << 10,
    kPresent = 1u << 11,
    kIndirectArgument = 1u << 12,
};

enum class SampleCount : std::uint8_t
{
    k1 = 1,
    k2 = 2,
    k4 = 4,
    k8 = 8,
    k16 = 16
};

enum class IndexType : std::uint8_t
{
    kUInt16,
    kUInt32
};

enum class QueueType : std::uint8_t
{
    kGraphics,
    kCompute,
    kTransfer
};

// ---- GPU query subsystem (A-QUERY, Backend-to-100 Wave 3a) ----------------
//
// The kind of measurement a query pool records. `kTimestamp` writes a GPU
// clock tick (resolved to nanoseconds via the device's timestamp period);
// `kOcclusion` counts samples that pass the depth/stencil test between
// begin_query/end_query; `kPipelineStatistics` accumulates the pipeline-stage
// counters Vulkan VkQueryPipelineStatisticFlags / D3D12
// D3D12_QUERY_DATA_PIPELINE_STATISTICS expose. Backends gate each type on the
// matching DeviceFeatures flag (timestamp_queries / pipeline_statistics_queries)
// or the per-backend occlusion capability.
enum class QueryType : std::uint8_t
{
    kTimestamp,
    kPipelineStatistics,
    kOcclusion,
};

// ---- Bit-flag helpers -----------------------------------------------------

#define CD_RHI_DEFINE_FLAG_OPS(E)                                                             \
    [[nodiscard]] constexpr E operator|(E a, E b) noexcept                                    \
    {                                                                                         \
        return static_cast<E>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b)); \
    }                                                                                         \
    [[nodiscard]] constexpr E operator&(E a, E b) noexcept                                    \
    {                                                                                         \
        return static_cast<E>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b)); \
    }                                                                                         \
    [[nodiscard]] constexpr E operator~(E a) noexcept                                         \
    {                                                                                         \
        return static_cast<E>(~static_cast<std::uint32_t>(a));                                \
    }                                                                                         \
    constexpr E& operator|=(E& a, E b) noexcept                                               \
    {                                                                                         \
        a = a | b;                                                                            \
        return a;                                                                             \
    }                                                                                         \
    constexpr E& operator&=(E& a, E b) noexcept                                               \
    {                                                                                         \
        a = a & b;                                                                            \
        return a;                                                                             \
    }                                                                                         \
    [[nodiscard]] constexpr bool has(E flags, E test) noexcept                                \
    {                                                                                         \
        return (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(test)) ==      \
               static_cast<std::uint32_t>(test);                                              \
    }                                                                                         \
    [[nodiscard]] constexpr bool any(E flags) noexcept                                        \
    {                                                                                         \
        return static_cast<std::uint32_t>(flags) != 0u;                                       \
    }

CD_RHI_DEFINE_FLAG_OPS(ShaderStage)
CD_RHI_DEFINE_FLAG_OPS(BufferUsage)
CD_RHI_DEFINE_FLAG_OPS(TextureUsage)
CD_RHI_DEFINE_FLAG_OPS(ResourceState)

#undef CD_RHI_DEFINE_FLAG_OPS

}  // namespace cd::rhi
