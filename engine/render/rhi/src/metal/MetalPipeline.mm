// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/src/metal/MetalPipeline.mm
// phase548 — Metal pipeline real impl (Sprint-1).
// phase559 — Sprint-2 addition: build_metal_sampler (MTLSamplerState).
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

}  // namespace cd::rhi::metal::detail

#endif  // __APPLE__
