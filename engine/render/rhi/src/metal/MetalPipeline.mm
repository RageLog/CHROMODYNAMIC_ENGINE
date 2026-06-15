// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/src/metal/MetalPipeline.mm
// phase548 — Metal pipeline real impl (Sprint-1).
// phase559 — Sprint-2 addition: build_metal_sampler (MTLSamplerState).
// phase572 — Sprint-3 addition: build_metal_shader_function (MTLLibrary +
//            MTLFunction from MSL source supplied via ShaderModuleDesc).
//
// Compiled only when CD_RHI_METAL_ENABLED=ON (Apple platform).
//
// Sprint-1 scope:
//   * build_sprint1_triangle_pipeline() compiles an INLINE MSL one-triangle
//     vertex + fragment program at runtime and returns an MTLRenderPipelineState.
//     The shader hard-codes three NDC vertices and a flat colour — enough for
//     hello_metal to demonstrate "clear + visible triangle".
//   * Real shader-module compilation (SPIRV-Cross MSL translation,
//     vertex-attribute mapping, descriptor-set lowering, depth/stencil/blend
//     state translation) lands in Sprint 2+.
//
// Sprint-2 scope:
//   * build_metal_sampler() translates a cd::rhi::SamplerDesc into a
//     MTLSamplerDescriptor and creates the matching MTLSamplerState. The
//     enum mapping is SOTA — matches the Vulkan back-end's parity test
//     expectations modulo Metal's discrete border-colour vocabulary.
//
// Sprint-3 scope:
//   * build_metal_shader_function() compiles MSL source carried by a
//     cd::rhi::ShaderModuleDesc into an id<MTLLibrary> and resolves the
//     id<MTLFunction> named by ShaderModuleDesc::entry_point. The descriptor's
//     `code` is treated as a UTF-8 MSL source string of length `code_size`
//     for Sprint-3; SPIRV-Cross MSL translation arrives when the engine
//     introduces a unified shader pipeline (the Vulkan SPIR-V byte-code
//     path is unaffected).
// =============================================================================
#if defined(__APPLE__)

#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

#include "MetalInternal.hpp"

namespace cd::rhi::metal::detail
{

namespace
{

// One-triangle MSL: vertex shader emits a hard-coded NDC triangle and a
// per-vertex tint; fragment shader returns the tint. Kept compact so the
// runtime-compile path is fast (no #include, no resources, no buffers).
constexpr const char* kSprint1TriangleMSL = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct VSOut {
    float4 position [[position]];
    float3 color;
};

vertex VSOut vs_sprint1(uint vid [[vertex_id]])
{
    // CCW triangle in NDC; Metal's NDC matches D3D12 (Y-down framebuffer
    // but Y-up clip-space when not flipped). For Sprint-1 we issue the
    // triangle in clip-space coordinates so it always covers ~half the
    // viewport regardless of orientation. front_face does not matter
    // because the pipeline has cullMode = none.
    const float2 positions[3] = {
        float2(-0.6, -0.5),
        float2( 0.6, -0.5),
        float2( 0.0,  0.6)
    };
    const float3 colors[3] = {
        float3(1.0, 0.2, 0.2),
        float3(0.2, 1.0, 0.2),
        float3(0.2, 0.4, 1.0)
    };
    VSOut o;
    o.position = float4(positions[vid], 0.0, 1.0);
    o.color    = colors[vid];
    return o;
}

fragment float4 fs_sprint1(VSOut in [[stage_in]])
{
    return float4(in.color, 1.0);
}
)MSL";

}  // namespace

id<MTLRenderPipelineState>
build_sprint1_triangle_pipeline(id<MTLDevice> device, MTLPixelFormat color_format,
                                std::string* error_out) noexcept
{
    NSError* err = nil;
    NSString* src = [NSString stringWithUTF8String:kSprint1TriangleMSL];

    MTLCompileOptions* opts = [[MTLCompileOptions alloc] init];
    // Metal 2.0 baseline gives us [[vertex_id]] without extras.
    opts.languageVersion = MTLLanguageVersion2_0;

    id<MTLLibrary> lib = [device newLibraryWithSource:src options:opts error:&err];
    if (lib == nil)
    {
        if (error_out != nullptr && err != nil)
        {
            *error_out = std::string {
                [[err localizedDescription] UTF8String]
            };
        }
        return nil;
    }

    id<MTLFunction> vs = [lib newFunctionWithName:@"vs_sprint1"];
    id<MTLFunction> fs = [lib newFunctionWithName:@"fs_sprint1"];
    if (vs == nil || fs == nil)
    {
        if (error_out != nullptr)
        {
            *error_out = "Metal::pipeline: vs_sprint1/fs_sprint1 not found in library";
        }
        return nil;
    }

    MTLRenderPipelineDescriptor* pd = [[MTLRenderPipelineDescriptor alloc] init];
    pd.vertexFunction   = vs;
    pd.fragmentFunction = fs;
    pd.colorAttachments[0].pixelFormat = color_format;
    pd.rasterSampleCount = 1;
    pd.label = @"cd::rhi::metal::sprint1_triangle";

    id<MTLRenderPipelineState> pso = [device newRenderPipelineStateWithDescriptor:pd
                                                                            error:&err];
    if (pso == nil && error_out != nullptr && err != nil)
    {
        *error_out = std::string {
            [[err localizedDescription] UTF8String]
        };
    }
    return pso;
}

// ---------------------------------------------------------------------------
// build_metal_sampler — phase559 / Sprint-2.
//
// Translate cd::rhi::SamplerDesc into MTLSamplerDescriptor + create the
// MTLSamplerState. The enum mapping is SOTA modulo Metal's discrete border
// vocabulary (transparent / opaque black / opaque white) which silently
// merges the int-typed border-colour variants into their float-typed
// counterparts — same behaviour as the Vulkan path under VK_BORDER_COLOR_*.
//
// Notes:
//   * `lodMinClamp` / `lodMaxClamp` default to [0, FLT_MAX] in Metal; we
//     honour the caller's range so anisotropic filtering at narrow LOD
//     bands behaves consistently with Vulkan + D3D12.
//   * `maxAnisotropy` is clamped to [1, 16]; values outside the range cause
//     Metal to assert in validation mode.
//   * `compareFunction` is only meaningful when the sampler is consumed by
//     a *_compare sample call; for non-compare samplers Metal ignores it.
//     We still propagate the requested CompareOp so depth-bound shadow
//     samplers light up without further wiring.
// ---------------------------------------------------------------------------
namespace
{

[[nodiscard]] MTLSamplerMinMagFilter to_mtl_min_mag(SamplerFilter f) noexcept
{
    return (f == SamplerFilter::kLinear) ? MTLSamplerMinMagFilterLinear
                                         : MTLSamplerMinMagFilterNearest;
}

[[nodiscard]] MTLSamplerMipFilter to_mtl_mip(SamplerMipmapMode m) noexcept
{
    return (m == SamplerMipmapMode::kLinear) ? MTLSamplerMipFilterLinear
                                             : MTLSamplerMipFilterNearest;
}

[[nodiscard]] MTLSamplerAddressMode to_mtl_addr(SamplerAddressMode a) noexcept
{
    switch (a)
    {
    case SamplerAddressMode::kRepeat:            return MTLSamplerAddressModeRepeat;
    case SamplerAddressMode::kMirroredRepeat:    return MTLSamplerAddressModeMirrorRepeat;
    case SamplerAddressMode::kClampToEdge:       return MTLSamplerAddressModeClampToEdge;
    case SamplerAddressMode::kClampToBorder:     return MTLSamplerAddressModeClampToBorderColor;
    case SamplerAddressMode::kMirrorClampToEdge: return MTLSamplerAddressModeMirrorClampToEdge;
    }
    return MTLSamplerAddressModeRepeat;
}

[[nodiscard]] MTLCompareFunction to_mtl_compare(CompareOp op) noexcept
{
    switch (op)
    {
    case CompareOp::kNever:        return MTLCompareFunctionNever;
    case CompareOp::kLess:         return MTLCompareFunctionLess;
    case CompareOp::kEqual:        return MTLCompareFunctionEqual;
    case CompareOp::kLessEqual:    return MTLCompareFunctionLessEqual;
    case CompareOp::kGreater:      return MTLCompareFunctionGreater;
    case CompareOp::kNotEqual:     return MTLCompareFunctionNotEqual;
    case CompareOp::kGreaterEqual: return MTLCompareFunctionGreaterEqual;
    case CompareOp::kAlways:       return MTLCompareFunctionAlways;
    }
    return MTLCompareFunctionAlways;
}

[[nodiscard]] MTLSamplerBorderColor to_mtl_border(BorderColor c) noexcept
{
    switch (c)
    {
    case BorderColor::kFloatTransparentBlack:
    case BorderColor::kIntTransparentBlack:    return MTLSamplerBorderColorTransparentBlack;
    case BorderColor::kFloatOpaqueBlack:
    case BorderColor::kIntOpaqueBlack:         return MTLSamplerBorderColorOpaqueBlack;
    case BorderColor::kFloatOpaqueWhite:
    case BorderColor::kIntOpaqueWhite:         return MTLSamplerBorderColorOpaqueWhite;
    }
    return MTLSamplerBorderColorTransparentBlack;
}

}  // namespace

id<MTLSamplerState>
build_metal_sampler(id<MTLDevice> device, const SamplerDesc& desc,
                    std::string* error_out) noexcept
{
    if (device == nil)
    {
        if (error_out != nullptr)
        {
            *error_out = "Metal::build_metal_sampler: nil MTLDevice";
        }
        return nil;
    }

    MTLSamplerDescriptor* sd = [[MTLSamplerDescriptor alloc] init];
    sd.label                = @"cd::rhi::metal::sampler";
    sd.minFilter            = to_mtl_min_mag(desc.min_filter);
    sd.magFilter            = to_mtl_min_mag(desc.mag_filter);
    sd.mipFilter            = to_mtl_mip(desc.mipmap_mode);
    sd.sAddressMode         = to_mtl_addr(desc.address_u);
    sd.tAddressMode         = to_mtl_addr(desc.address_v);
    sd.rAddressMode         = to_mtl_addr(desc.address_w);
    sd.lodMinClamp          = desc.min_lod;
    sd.lodMaxClamp          = desc.max_lod;
    // Metal clamps anisotropy into [1, 16]; values outside hit validation
    // asserts. Mirror the Vulkan back-end behaviour by clamping on the
    // host side rather than letting Metal trap.
    float aniso = desc.anisotropy_enable ? desc.max_anisotropy : 1.0F;
    if (aniso < 1.0F) { aniso = 1.0F; }
    if (aniso > 16.0F) { aniso = 16.0F; }
    sd.maxAnisotropy        = static_cast<NSUInteger>(aniso);
    sd.borderColor          = to_mtl_border(desc.border_color);
    sd.normalizedCoordinates = YES;
    sd.compareFunction      = desc.compare_enable
        ? to_mtl_compare(desc.compare_op)
        : MTLCompareFunctionAlways;
    // supportArgumentBuffers makes the sampler usable from argument-buffer
    // descriptor sets (Sprint 3). Cost is negligible for sampler states.
    sd.supportArgumentBuffers = YES;

    id<MTLSamplerState> state = [device newSamplerStateWithDescriptor:sd];
    if (state == nil && error_out != nullptr)
    {
        *error_out = "Metal::build_metal_sampler: newSamplerStateWithDescriptor returned nil";
    }
    return state;
}

// ---------------------------------------------------------------------------
// build_metal_shader_function — phase572 / Sprint-3.
//
// Compile MSL source carried in ShaderModuleDesc::code (treated as a UTF-8
// MSL string of length code_size) into an id<MTLLibrary>, then resolve the
// id<MTLFunction> named by ShaderModuleDesc::entry_point. The library is
// emitted via `lib_out` so the caller can own its lifetime alongside the
// function (Metal keeps a strong reference on its own, but the engine wants
// to release the library when the shader module is destroyed).
//
// Sprint-3 contract:
//   * code/code_size = UTF-8 MSL source (no NUL terminator required).
//   * entry_point   = function name (defaults to "main").
//   * Errors        = nil return + populated `error_out`; caller maps to
//                     kResourceCreationFailed.
//
// The MSL language version is pinned at MTLLanguageVersion2_0 to match
// build_sprint1_triangle_pipeline so the Sprint-3 path is byte-for-byte
// compatible with the Sprint-1 triangle PSO when the same source is fed
// through both code-paths.
// ---------------------------------------------------------------------------
id<MTLFunction>
build_metal_shader_function(id<MTLDevice> device,
                            const ShaderModuleDesc& desc,
                            id<MTLLibrary>* lib_out,
                            std::string* error_out) noexcept
{
    if (device == nil)
    {
        if (error_out != nullptr)
        {
            *error_out = "Metal::build_metal_shader_function: nil MTLDevice";
        }
        return nil;
    }
    if (desc.code == nullptr || desc.code_size == 0)
    {
        if (error_out != nullptr)
        {
            *error_out = "Metal::build_metal_shader_function: empty shader code";
        }
        return nil;
    }

    NSString* src = [[NSString alloc]
        initWithBytes:desc.code
               length:static_cast<NSUInteger>(desc.code_size)
             encoding:NSUTF8StringEncoding];
    if (src == nil)
    {
        if (error_out != nullptr)
        {
            *error_out = "Metal::build_metal_shader_function: shader code is not valid UTF-8 MSL";
        }
        return nil;
    }

    MTLCompileOptions* opts = [[MTLCompileOptions alloc] init];
    opts.languageVersion = MTLLanguageVersion2_0;

    NSError* err = nil;
    id<MTLLibrary> lib = [device newLibraryWithSource:src options:opts error:&err];
    if (lib == nil)
    {
        if (error_out != nullptr)
        {
            if (err != nil)
            {
                *error_out = std::string {
                    [[err localizedDescription] UTF8String]
                };
            }
            else
            {
                *error_out = "Metal::build_metal_shader_function: newLibraryWithSource returned nil";
            }
        }
        return nil;
    }

    // ShaderModuleDesc::entry_point defaults to "main" via its
    // string_view initializer; honour empty / missing entry points by
    // falling back to that.
    std::string_view ep = desc.entry_point.empty()
        ? std::string_view { "main" }
        : desc.entry_point;
    NSString* ns_ep = [[NSString alloc] initWithBytes:ep.data()
                                               length:ep.size()
                                             encoding:NSUTF8StringEncoding];
    if (ns_ep == nil)
    {
        if (error_out != nullptr)
        {
            *error_out = "Metal::build_metal_shader_function: entry point not valid UTF-8";
        }
        return nil;
    }

    id<MTLFunction> fn = [lib newFunctionWithName:ns_ep];
    if (fn == nil)
    {
        if (error_out != nullptr)
        {
            *error_out = std::string {
                "Metal::build_metal_shader_function: entry point '" }
                + std::string { ep } + "' not found in MTLLibrary";
        }
        return nil;
    }

    if (lib_out != nullptr)
    {
        *lib_out = lib;
    }
    return fn;
}

// ===========================================================================
// build_metal_graphics_pipeline — M2 (ADR-20260615) desc-driven PSO.
//
// Translates a GraphicsPipelineDesc into an MTLRenderPipelineDescriptor +
// MTLVertexDescriptor + MTLDepthStencilDescriptor, method-by-method against
// the Vulkan VkGraphicsPipelineCreateInfo path (VulkanDevice.cpp:1650-1841).
// The format / topology / cull / blend mapping tables mirror the Vulkan
// map_* helpers (VulkanDevice.cpp:286+) so identical descriptors produce
// parity-equivalent native pipelines.
// ===========================================================================
namespace
{

[[nodiscard]] MTLPixelFormat to_pixel_format(Format f) noexcept
{
    switch (f)
    {
    case Format::kUndefined:       return MTLPixelFormatInvalid;
    case Format::kR8Unorm:         return MTLPixelFormatR8Unorm;
    case Format::kRG8Unorm:        return MTLPixelFormatRG8Unorm;
    case Format::kRGBA8Unorm:      return MTLPixelFormatRGBA8Unorm;
    case Format::kRGBA8Srgb:       return MTLPixelFormatRGBA8Unorm_sRGB;
    case Format::kBGRA8Unorm:      return MTLPixelFormatBGRA8Unorm;
    case Format::kBGRA8Srgb:       return MTLPixelFormatBGRA8Unorm_sRGB;
    case Format::kR16Float:        return MTLPixelFormatR16Float;
    case Format::kRG16Float:       return MTLPixelFormatRG16Float;
    case Format::kRGBA16Float:     return MTLPixelFormatRGBA16Float;
    case Format::kR32Float:        return MTLPixelFormatR32Float;
    case Format::kRG32Float:       return MTLPixelFormatRG32Float;
    case Format::kRGBA32Float:     return MTLPixelFormatRGBA32Float;
    case Format::kR32Uint:         return MTLPixelFormatR32Uint;
    case Format::kRG32Uint:        return MTLPixelFormatRG32Uint;
    case Format::kRGBA32Uint:      return MTLPixelFormatRGBA32Uint;
    case Format::kR11G11B10Float:  return MTLPixelFormatRG11B10Float;
    case Format::kRGB10A2Unorm:    return MTLPixelFormatRGB10A2Unorm;
    case Format::kRGB9E5Float:     return MTLPixelFormatRGB9E5Float;
    case Format::kD16Unorm:        return MTLPixelFormatDepth16Unorm;
    case Format::kD32Float:        return MTLPixelFormatDepth32Float;
    case Format::kD24UnormS8Uint:  return MTLPixelFormatDepth24Unorm_Stencil8;
    case Format::kD32FloatS8Uint:  return MTLPixelFormatDepth32Float_Stencil8;
    case Format::kS8Uint:          return MTLPixelFormatStencil8;
    default:                       return MTLPixelFormatInvalid;
    }
}

// Vertex-attribute format. The engine's vertex layouts use float / packed
// formats; the Vulkan side maps these through map_format too.
[[nodiscard]] MTLVertexFormat to_vertex_format(Format f) noexcept
{
    switch (f)
    {
    case Format::kR32Float:     return MTLVertexFormatFloat;
    case Format::kRG32Float:    return MTLVertexFormatFloat2;
    case Format::kRGB32Float:   return MTLVertexFormatFloat3;
    case Format::kRGBA32Float:  return MTLVertexFormatFloat4;
    case Format::kRGBA8Unorm:   return MTLVertexFormatUChar4Normalized;
    case Format::kRGBA8Uint:    return MTLVertexFormatUChar4;
    case Format::kRG16Float:    return MTLVertexFormatHalf2;
    case Format::kRGBA16Float:  return MTLVertexFormatHalf4;
    case Format::kR32Uint:      return MTLVertexFormatUInt;
    case Format::kRG32Uint:     return MTLVertexFormatUInt2;
    case Format::kRGB32Uint:    return MTLVertexFormatUInt3;
    case Format::kRGBA32Uint:   return MTLVertexFormatUInt4;
    default:                    return MTLVertexFormatFloat3;
    }
}

[[nodiscard]] MTLPrimitiveType to_primitive_type(PrimitiveTopology t) noexcept
{
    switch (t)
    {
    case PrimitiveTopology::kPointList:     return MTLPrimitiveTypePoint;
    case PrimitiveTopology::kLineList:      return MTLPrimitiveTypeLine;
    case PrimitiveTopology::kLineStrip:     return MTLPrimitiveTypeLineStrip;
    case PrimitiveTopology::kTriangleList:  return MTLPrimitiveTypeTriangle;
    case PrimitiveTopology::kTriangleStrip: return MTLPrimitiveTypeTriangleStrip;
    case PrimitiveTopology::kTriangleFan:   return MTLPrimitiveTypeTriangle;  // no fan on Metal
    }
    return MTLPrimitiveTypeTriangle;
}

// Topology class for the PSO's inputPrimitiveTopology (tessellation/restart).
[[nodiscard]] MTLPrimitiveTopologyClass
to_topology_class(PrimitiveTopology t) noexcept
{
    switch (t)
    {
    case PrimitiveTopology::kPointList:
        return MTLPrimitiveTopologyClassPoint;
    case PrimitiveTopology::kLineList:
    case PrimitiveTopology::kLineStrip:
        return MTLPrimitiveTopologyClassLine;
    default:
        return MTLPrimitiveTopologyClassTriangle;
    }
}

[[nodiscard]] MTLCullMode to_cull_mode(CullMode m) noexcept
{
    switch (m)
    {
    case CullMode::kNone:         return MTLCullModeNone;
    case CullMode::kFront:        return MTLCullModeFront;
    case CullMode::kBack:         return MTLCullModeBack;
    case CullMode::kFrontAndBack: return MTLCullModeNone;  // Metal has no both
    }
    return MTLCullModeNone;
}

// FrontFace -> MTLWinding, INVERTED vs the engine descriptor (winding
// compensation; ADR-20260615-ndc-y-handedness §Karar 3 + Sonuclar, the Metal
// analog of the D3D12 phase1204 FrontCounterClockwise inversion).
//
// The command buffer applies a NEGATIVE-HEIGHT viewport (MetalCommandBuffer.mm
// set_viewport / begin_render_pass) so Metal's +Y-up framebuffer matches
// Vulkan's +Y-down pixel-for-pixel. The rasterizer decides front/back facing in
// WINDOW space (after the viewport transform), so a negative-height viewport
// flips the sign of the window-space signed area -> it INVERTS the apparent
// winding the rasterizer sees. The SAME clip-space triangle Vulkan classifies as
// FRONT therefore reaches the Metal rasterizer with the OPPOSITE winding.
//
// To keep engine cull semantics backend-IDENTICAL (a front_face=kClockwise +
// cull=kBack triangle visible under Vulkan stays visible under Metal),
// setFrontFacingWinding must be the LOGICAL NEGATION of the geometric mapping,
// exactly like D3D12 sets FrontCounterClockwise = (kClockwise ? TRUE : FALSE):
//   kClockwise        -> MTLWindingCounterClockwise
//   kCounterClockwise -> MTLWindingClockwise
//
// The PRE-FIX mapping honoured the descriptor directly (kClockwise ->
// MTLWindingClockwise) on the stale premise that the viewport flip "cancels out"
// — true before the negative-height viewport landed (D16/phase1196), a face-cull
// parity bug after it. This compensation is the Metal half of the cross-backend
// invariant the D3D12 cull-parity test locks; a Metal cull-parity GPU test is a
// future strand (ROADMAP_PHASE_2 backend-parity mega-marathon). The inversion is
// BOUND to the negative-height viewport: if set_viewport ever returns to a
// positive height, this compensation becomes wrong (documented in the ADR).
[[nodiscard]] MTLWinding to_winding(FrontFace f) noexcept
{
    return (f == FrontFace::kClockwise) ? MTLWindingCounterClockwise
                                        : MTLWindingClockwise;
}

[[nodiscard]] MTLBlendFactor to_blend_factor(BlendFactor f) noexcept
{
    switch (f)
    {
    case BlendFactor::kZero:                 return MTLBlendFactorZero;
    case BlendFactor::kOne:                  return MTLBlendFactorOne;
    case BlendFactor::kSrcColor:             return MTLBlendFactorSourceColor;
    case BlendFactor::kOneMinusSrcColor:     return MTLBlendFactorOneMinusSourceColor;
    case BlendFactor::kDstColor:             return MTLBlendFactorDestinationColor;
    case BlendFactor::kOneMinusDstColor:     return MTLBlendFactorOneMinusDestinationColor;
    case BlendFactor::kSrcAlpha:             return MTLBlendFactorSourceAlpha;
    case BlendFactor::kOneMinusSrcAlpha:     return MTLBlendFactorOneMinusSourceAlpha;
    case BlendFactor::kDstAlpha:             return MTLBlendFactorDestinationAlpha;
    case BlendFactor::kOneMinusDstAlpha:     return MTLBlendFactorOneMinusDestinationAlpha;
    case BlendFactor::kConstantColor:        return MTLBlendFactorBlendColor;
    case BlendFactor::kOneMinusConstantColor:return MTLBlendFactorOneMinusBlendColor;
    case BlendFactor::kConstantAlpha:        return MTLBlendFactorBlendAlpha;
    case BlendFactor::kOneMinusConstantAlpha:return MTLBlendFactorOneMinusBlendAlpha;
    case BlendFactor::kSrcAlphaSaturate:     return MTLBlendFactorSourceAlphaSaturated;
    }
    return MTLBlendFactorZero;
}

[[nodiscard]] MTLBlendOperation to_blend_op(BlendOp o) noexcept
{
    switch (o)
    {
    case BlendOp::kAdd:             return MTLBlendOperationAdd;
    case BlendOp::kSubtract:        return MTLBlendOperationSubtract;
    case BlendOp::kReverseSubtract: return MTLBlendOperationReverseSubtract;
    case BlendOp::kMin:             return MTLBlendOperationMin;
    case BlendOp::kMax:             return MTLBlendOperationMax;
    }
    return MTLBlendOperationAdd;
}

[[nodiscard]] MTLColorWriteMask to_color_write_mask(std::uint8_t m) noexcept
{
    MTLColorWriteMask out = MTLColorWriteMaskNone;
    if ((m & 0x1u) != 0u) { out |= MTLColorWriteMaskRed; }
    if ((m & 0x2u) != 0u) { out |= MTLColorWriteMaskGreen; }
    if ((m & 0x4u) != 0u) { out |= MTLColorWriteMaskBlue; }
    if ((m & 0x8u) != 0u) { out |= MTLColorWriteMaskAlpha; }
    return out;
}

[[nodiscard]] MTLCompareFunction depth_compare(CompareOp op) noexcept
{
    switch (op)
    {
    case CompareOp::kNever:        return MTLCompareFunctionNever;
    case CompareOp::kLess:         return MTLCompareFunctionLess;
    case CompareOp::kEqual:        return MTLCompareFunctionEqual;
    case CompareOp::kLessEqual:    return MTLCompareFunctionLessEqual;
    case CompareOp::kGreater:      return MTLCompareFunctionGreater;
    case CompareOp::kNotEqual:     return MTLCompareFunctionNotEqual;
    case CompareOp::kGreaterEqual: return MTLCompareFunctionGreaterEqual;
    case CompareOp::kAlways:       return MTLCompareFunctionAlways;
    }
    return MTLCompareFunctionLess;
}

}  // namespace

id<MTLRenderPipelineState>
build_metal_graphics_pipeline(id<MTLDevice> device,
                              const GraphicsPipelineDesc& desc,
                              id<MTLFunction> vertex_fn,
                              id<MTLFunction> fragment_fn,
                              id<MTLDepthStencilState>* dss_out,
                              MTLPrimitiveType* primitive_out,
                              MTLCullMode* cull_out,
                              MTLWinding* winding_out,
                              std::string* error_out) noexcept
{
    if (device == nil || vertex_fn == nil)
    {
        if (error_out != nullptr)
        {
            *error_out =
                "Metal::build_metal_graphics_pipeline: nil device / vertex fn";
        }
        return nil;
    }

    MTLRenderPipelineDescriptor* pd = [[MTLRenderPipelineDescriptor alloc] init];
    pd.label = @"cd::rhi::metal::graphics_pipeline";
    pd.vertexFunction = vertex_fn;
    pd.fragmentFunction = fragment_fn;  // may be nil for depth-only
    pd.rasterSampleCount = static_cast<NSUInteger>(desc.samples);
    pd.inputPrimitiveTopology = to_topology_class(desc.topology);

    // --- Vertex descriptor (MTLVertexDescriptor) ---------------------------
    // VertexAttribute: location -> attribute index, binding -> bufferIndex,
    // format, offset. VertexBinding: stride + per_instance step function.
    //
    // FIX 1 (ADR-20260615 namespace, hardened phase1122): Metal shares ONE
    // [[buffer(N)]] namespace per stage across descriptor-set argument buffers
    // (set N -> [[buffer(N)]], sets [0..7], M3 contract), the push block
    // ([[buffer(8)]]), these vertex-input buffers, and SPIRV-Cross's own aux
    // buffers ([20..30]). To keep the four classes DISJOINT, both the
    // attribute's bufferIndex AND the layout slot are relocated to
    // kVertexBufferBaseIndex + binding (range [9..15], strictly below the aux
    // floor of 20). The runtime bind_vertex_buffer (MetalCommandBuffer.mm)
    // applies the IDENTICAL offset, so the PSO's stage_in layout and the bound
    // buffer index agree. Bindings beyond the reserved range are dropped (would
    // collide with reserved slots).
    if (!desc.vertex_attributes.empty() || !desc.vertex_bindings.empty())
    {
        MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
        for (const VertexAttribute& a : desc.vertex_attributes)
        {
            if (a.binding >= kMaxVertexBufferSlots)
            {
                continue;
            }
            const NSUInteger loc = static_cast<NSUInteger>(a.location);
            const NSUInteger buf_idx =
                static_cast<NSUInteger>(kVertexBufferBaseIndex + a.binding);
            vd.attributes[loc].format = to_vertex_format(a.format);
            vd.attributes[loc].offset = static_cast<NSUInteger>(a.offset);
            vd.attributes[loc].bufferIndex = buf_idx;
        }
        for (const VertexBinding& b : desc.vertex_bindings)
        {
            if (b.binding >= kMaxVertexBufferSlots)
            {
                continue;
            }
            const NSUInteger slot =
                static_cast<NSUInteger>(kVertexBufferBaseIndex + b.binding);
            vd.layouts[slot].stride = static_cast<NSUInteger>(b.stride);
            vd.layouts[slot].stepFunction = b.per_instance
                ? MTLVertexStepFunctionPerInstance
                : MTLVertexStepFunctionPerVertex;
            vd.layouts[slot].stepRate = 1;
        }
        pd.vertexDescriptor = vd;
    }

    // --- Colour attachments (format + blend) -------------------------------
    for (std::size_t i = 0; i < desc.color_attachment_formats.size(); ++i)
    {
        MTLRenderPipelineColorAttachmentDescriptor* ca =
            pd.colorAttachments[static_cast<NSUInteger>(i)];
        ca.pixelFormat = to_pixel_format(desc.color_attachment_formats[i]);
        // Per-attachment blend state when supplied, else "opaque, write-all"
        // (mirrors the Vulkan synthesised default when blend_attachments is
        // empty).
        if (i < desc.blend_attachments.size())
        {
            const BlendAttachmentState& b = desc.blend_attachments[i];
            ca.blendingEnabled = b.blend_enable ? YES : NO;
            ca.sourceRGBBlendFactor = to_blend_factor(b.src_color);
            ca.destinationRGBBlendFactor = to_blend_factor(b.dst_color);
            ca.rgbBlendOperation = to_blend_op(b.color_op);
            ca.sourceAlphaBlendFactor = to_blend_factor(b.src_alpha);
            ca.destinationAlphaBlendFactor = to_blend_factor(b.dst_alpha);
            ca.alphaBlendOperation = to_blend_op(b.alpha_op);
            ca.writeMask = to_color_write_mask(b.color_write_mask);
        }
        else
        {
            ca.blendingEnabled = NO;
            ca.writeMask = MTLColorWriteMaskAll;
        }
    }

    // --- Depth / stencil attachment formats --------------------------------
    const bool has_depth =
        desc.depth_attachment_format != Format::kUndefined;
    const bool has_stencil =
        desc.stencil_attachment_format != Format::kUndefined;
    if (has_depth)
    {
        pd.depthAttachmentPixelFormat =
            to_pixel_format(desc.depth_attachment_format);
    }
    if (has_stencil)
    {
        pd.stencilAttachmentPixelFormat =
            to_pixel_format(desc.stencil_attachment_format);
    }

    NSError* err = nil;
    id<MTLRenderPipelineState> pso =
        [device newRenderPipelineStateWithDescriptor:pd error:&err];
    if (pso == nil)
    {
        if (error_out != nullptr)
        {
            *error_out = (err != nil)
                ? std::string { [[err localizedDescription] UTF8String] }
                : std::string {
                    "Metal::build_metal_graphics_pipeline: nil PSO" };
        }
        return nil;
    }

    // --- Depth-stencil state (SEPARATE Metal object) -----------------------
    // Gated on a present depth attachment exactly like the Vulkan back-end
    // (depthTest && has_depth_attach). When there is no depth attachment we
    // emit nil so the command buffer skips setDepthStencilState.
    if (dss_out != nullptr)
    {
        *dss_out = nil;
        if (has_depth)
        {
            MTLDepthStencilDescriptor* dsd =
                [[MTLDepthStencilDescriptor alloc] init];
            dsd.depthCompareFunction = desc.depth_stencil.depth_test
                ? depth_compare(desc.depth_stencil.depth_compare)
                : MTLCompareFunctionAlways;
            dsd.depthWriteEnabled =
                (desc.depth_stencil.depth_write && desc.depth_stencil.depth_test)
                    ? YES
                    : NO;
            *dss_out = [device newDepthStencilStateWithDescriptor:dsd];
        }
    }

    if (primitive_out != nullptr)
    {
        *primitive_out = to_primitive_type(desc.topology);
    }
    if (cull_out != nullptr)
    {
        *cull_out = to_cull_mode(desc.raster.cull);
    }
    if (winding_out != nullptr)
    {
        *winding_out = to_winding(desc.raster.front_face);
    }
    return pso;
}

// ---------------------------------------------------------------------------
// build_metal_mesh_pipeline — M10 (B2 — ADR-20260615). See MetalInternal.hpp.
//
// Mirrors build_metal_graphics_pipeline method-by-method, but uses the
// Metal-3 MTLMeshRenderPipelineDescriptor (object/mesh/fragment functions +
// colour/depth/stencil attachment state + MSAA). No MTLVertexDescriptor — the
// object/mesh chain replaces the input assembler. The resulting PSO is stored
// in the SAME graphics-pipeline registry (the engine binds mesh pipelines via
// bind_graphics_pipeline + draw_mesh_tasks, exactly like Vulkan/D3D12 bind
// them on the graphics bind point).
id<MTLRenderPipelineState>
build_metal_mesh_pipeline(id<MTLDevice> device,
                          const MeshPipelineDesc& desc,
                          id<MTLFunction> object_fn,
                          id<MTLFunction> mesh_fn,
                          id<MTLFunction> fragment_fn,
                          id<MTLDepthStencilState>* dss_out,
                          MTLCullMode* cull_out,
                          MTLWinding* winding_out,
                          std::string* error_out) noexcept
    API_AVAILABLE(macos(13.0), ios(16.0))
{
    if (device == nil || mesh_fn == nil)
    {
        if (error_out != nullptr)
        {
            *error_out =
                "Metal::build_metal_mesh_pipeline: nil device / mesh fn";
        }
        return nil;
    }

    MTLMeshRenderPipelineDescriptor* pd =
        [[MTLMeshRenderPipelineDescriptor alloc] init];
    pd.label = @"cd::rhi::metal::mesh_pipeline";
    pd.objectFunction = object_fn;        // may be nil for a mesh-only pipeline
    pd.meshFunction = mesh_fn;            // required
    pd.fragmentFunction = fragment_fn;    // required for rasterized output
    pd.rasterSampleCount = static_cast<NSUInteger>(desc.samples);

    // --- Colour attachments (format + blend) — identical mapping to the
    //     graphics builder (MTLMeshRenderPipelineColorAttachmentDescriptorArray
    //     has the same shape as the classic one). -------------------------
    for (std::size_t i = 0; i < desc.color_attachment_formats.size(); ++i)
    {
        MTLRenderPipelineColorAttachmentDescriptor* ca =
            pd.colorAttachments[static_cast<NSUInteger>(i)];
        ca.pixelFormat = to_pixel_format(desc.color_attachment_formats[i]);
        if (i < desc.blend_attachments.size())
        {
            const BlendAttachmentState& b = desc.blend_attachments[i];
            ca.blendingEnabled = b.blend_enable ? YES : NO;
            ca.sourceRGBBlendFactor = to_blend_factor(b.src_color);
            ca.destinationRGBBlendFactor = to_blend_factor(b.dst_color);
            ca.rgbBlendOperation = to_blend_op(b.color_op);
            ca.sourceAlphaBlendFactor = to_blend_factor(b.src_alpha);
            ca.destinationAlphaBlendFactor = to_blend_factor(b.dst_alpha);
            ca.alphaBlendOperation = to_blend_op(b.alpha_op);
            ca.writeMask = to_color_write_mask(b.color_write_mask);
        }
        else
        {
            ca.blendingEnabled = NO;
            ca.writeMask = MTLColorWriteMaskAll;
        }
    }

    // --- Depth / stencil attachment formats --------------------------------
    const bool has_depth =
        desc.depth_attachment_format != Format::kUndefined;
    const bool has_stencil =
        desc.stencil_attachment_format != Format::kUndefined;
    if (has_depth)
    {
        pd.depthAttachmentPixelFormat =
            to_pixel_format(desc.depth_attachment_format);
    }
    if (has_stencil)
    {
        pd.stencilAttachmentPixelFormat =
            to_pixel_format(desc.stencil_attachment_format);
    }

    NSError* err = nil;
    // newRenderPipelineStateWithMeshDescriptor:options:reflection:error: is the
    // Metal-3 factory for a mesh PSO. options=0 (no reflection requested).
    id<MTLRenderPipelineState> pso =
        [device newRenderPipelineStateWithMeshDescriptor:pd
                                                 options:MTLPipelineOptionNone
                                              reflection:nil
                                                   error:&err];
    if (pso == nil)
    {
        if (error_out != nullptr)
        {
            *error_out = (err != nil)
                ? std::string { [[err localizedDescription] UTF8String] }
                : std::string {
                    "Metal::build_metal_mesh_pipeline: nil PSO" };
        }
        return nil;
    }

    // --- Depth-stencil state (SEPARATE Metal object) — same gating as the
    //     graphics builder. -------------------------------------------------
    if (dss_out != nullptr)
    {
        *dss_out = nil;
        if (has_depth)
        {
            MTLDepthStencilDescriptor* dsd =
                [[MTLDepthStencilDescriptor alloc] init];
            dsd.depthCompareFunction = desc.depth_stencil.depth_test
                ? depth_compare(desc.depth_stencil.depth_compare)
                : MTLCompareFunctionAlways;
            dsd.depthWriteEnabled =
                (desc.depth_stencil.depth_write && desc.depth_stencil.depth_test)
                    ? YES
                    : NO;
            *dss_out = [device newDepthStencilStateWithDescriptor:dsd];
        }
    }

    if (cull_out != nullptr)
    {
        *cull_out = to_cull_mode(desc.raster.cull);
    }
    // Winding compensation applies to mesh pipelines too: the same
    // negative-height viewport flip + descriptor-inverted MTLWinding keeps
    // engine cull semantics backend-identical (ADR-20260615-ndc-y-handedness;
    // the D3D12 phase1204 mesh-PSO FrontCounterClockwise inversion analog).
    if (winding_out != nullptr)
    {
        *winding_out = to_winding(desc.raster.front_face);
    }
    return pso;
}

}  // namespace cd::rhi::metal::detail

#endif  // __APPLE__
