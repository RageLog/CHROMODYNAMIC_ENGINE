// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/src/metal/MetalPipeline.mm
// phase548 — Metal pipeline real impl (Sprint-1).
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
//     state translation) lands in Sprint 2.
//
// The pipeline does not consume the GraphicsPipelineDesc layout / vertex /
// shader-module fields yet. Sprint-1 always returns the same hardcoded
// triangle PSO so we can verify the swapchain-acquire / encoder-issue /
// drawable-present path end-to-end before wiring real shader compilation.
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

}  // namespace cd::rhi::metal::detail

#endif  // __APPLE__
