// =============================================================================
// CHROMODYNAMIC — cd/ui/renderer_webgpu/WgpuContract.hpp
//
// Pure host-side WebGPU UI contract: the WGSL shader source, the vertex
// buffer layout math, the colour / texture-format conversion, the
// scissor-clamp math and the draw-command byte-offset translation.
//
// EVERYTHING in this header is Dawn-FREE and GPU-FREE.  It is the testable
// host surface that a real Dawn pipeline (Submitter, CD_UI_WEBGPU_HAVE_DAWN
// == 1) would consume verbatim:
//
//   * the WGSL strings become wgpu::ShaderModule source,
//   * the VertexAttribute table becomes wgpu::VertexAttribute[] +
//     wgpu::VertexBufferLayout,
//   * clamp_scissor() feeds wgpu::RenderPassEncoder::SetScissorRect,
//   * draw_slice_byte_offsets() feeds SetIndexBuffer offset / DrawIndexed
//     firstIndex.
//
// Keeping these in a header (not buried in the Dawn-only `#if` block of
// Submitter.cpp) means they are unit-tested on EVERY CI tier without a
// physical GPU or the Dawn library.  When the multi-week Dawn pipeline
// lands (see the SEALED block in Submitter.cpp), it wires these constants
// straight through — there is no second copy to drift.
//
// The WGSL itself is NOT compiled here (no Naga/Tint host validator is a
// project dependency yet); the tests pin its *structural* contract — the
// attribute @location indices, the variant branch ids, the bind-group
// indices — so a silent shader-vs-Vertex layout drift fails on revert.
// =============================================================================
#pragma once

#include <cd/ui/renderer/DrawBatcher.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cd::ui::renderer_webgpu
{

// ---------------------------------------------------------------------------
// WebGPU texture-format enum values (subset relevant to a UI colour target).
//
// These mirror the numeric values of wgpu::TextureFormat so the host side
// can validate SubmitterCreateInfo::color_format WITHOUT pulling Dawn in.
// Values are from the WebGPU spec (webgpu.h WGPUTextureFormat); they are
// stable ABI constants.
// ---------------------------------------------------------------------------
namespace texture_format
{
inline constexpr std::uint32_t kUndefined      = 0x00U;
inline constexpr std::uint32_t kRgba8Unorm     = 0x12U;  ///< WGPUTextureFormat_RGBA8Unorm
inline constexpr std::uint32_t kRgba8UnormSrgb = 0x13U;  ///< WGPUTextureFormat_RGBA8UnormSrgb
inline constexpr std::uint32_t kBgra8Unorm     = 0x17U;  ///< WGPUTextureFormat_BGRA8Unorm (common surface)
inline constexpr std::uint32_t kBgra8UnormSrgb = 0x18U;  ///< WGPUTextureFormat_BGRA8UnormSrgb
}  // namespace texture_format

/// True when `fmt` is one of the four 8-bit RGBA/BGRA colour formats the UI
/// pipeline supports as a render target.  `kUndefined` (0) is rejected.
[[nodiscard]] constexpr bool is_supported_color_format(std::uint32_t fmt) noexcept
{
    return fmt == texture_format::kRgba8Unorm
        || fmt == texture_format::kRgba8UnormSrgb
        || fmt == texture_format::kBgra8Unorm
        || fmt == texture_format::kBgra8UnormSrgb;
}

/// True when the colour format applies the sRGB electro-optical transfer on
/// store (so the UI shader must output LINEAR colour and let the hardware
/// encode).  Non-sRGB formats expect the shader to output the byte value
/// directly.
[[nodiscard]] constexpr bool is_srgb_color_format(std::uint32_t fmt) noexcept
{
    return fmt == texture_format::kRgba8UnormSrgb
        || fmt == texture_format::kBgra8UnormSrgb;
}

// ---------------------------------------------------------------------------
// Vertex buffer layout — must match cd::ui::renderer::Vertex byte-for-byte.
//
// Vertex (24 bytes):
//   offset  0 : float  pos_x, pos_y           -> @location(0) vec2<f32>
//   offset  8 : float  uv_x,  uv_y            -> @location(1) vec2<f32>
//   offset 16 : u8 r,g,b,a (unorm8x4)         -> @location(2) vec4<f32>
//   offset 20 : u8 variant + 3 pad (uint8x4)  -> @location(3) vec4<u32> (x = variant)
//
// WebGPU vertex format codes (wgpu::VertexFormat) used here:
//   Float32x2 = 0x1C, Unorm8x4 = 0x05, Uint8x4 = 0x01.
// ---------------------------------------------------------------------------
namespace vertex_format
{
inline constexpr std::uint32_t kFloat32x2 = 0x1CU;  ///< wgpu::VertexFormat::Float32x2
inline constexpr std::uint32_t kUnorm8x4  = 0x05U;  ///< wgpu::VertexFormat::Unorm8x4
inline constexpr std::uint32_t kUint8x4   = 0x01U;  ///< wgpu::VertexFormat::Uint8x4
}  // namespace vertex_format

/// One vertex attribute, decoupled from Dawn's wgpu::VertexAttribute so the
/// layout can be asserted on the host.  Mirrors the field order of
/// wgpu::VertexAttribute (format / offset / shaderLocation).
struct VertexAttributeDesc
{
    std::uint32_t format          { 0U };  ///< vertex_format::k*
    std::uint64_t offset          { 0U };  ///< byte offset within the Vertex
    std::uint32_t shader_location { 0U };  ///< @location() in the WGSL
};

/// Byte stride between consecutive vertices in the GPU vertex buffer.
/// Equals sizeof(cd::ui::renderer::Vertex) so the GPU layout never drifts
/// from the CPU struct.
[[nodiscard]] constexpr std::uint64_t vertex_stride_bytes() noexcept
{
    return static_cast<std::uint64_t>(sizeof(cd::ui::renderer::Vertex));
}

/// The four UI vertex attributes, in @location order.  Offsets are derived
/// from the real Vertex member positions (pos / uv / colour / variant).
[[nodiscard]] constexpr std::array<VertexAttributeDesc, 4U> vertex_attributes() noexcept
{
    return std::array<VertexAttributeDesc, 4U> {{
        { vertex_format::kFloat32x2,  0U, 0U },  // pos  (pos_x @ Vertex offset 0)
        { vertex_format::kFloat32x2,  8U, 1U },  // uv   (uv_x  @ Vertex offset 8)
        { vertex_format::kUnorm8x4,  16U, 2U },  // rgba (r     @ Vertex offset 16)
        { vertex_format::kUint8x4,   20U, 3U },  // variant+pad (variant @ offset 20)
    }};
}

// ---------------------------------------------------------------------------
// Index format
// ---------------------------------------------------------------------------

/// Bytes per index in the GPU index buffer.  The batcher emits u16 indices.
[[nodiscard]] constexpr std::uint64_t index_size_bytes() noexcept
{
    return static_cast<std::uint64_t>(sizeof(std::uint16_t));
}

// ---------------------------------------------------------------------------
// Bind-group layout indices (UI pipeline)
//
// group(0): per-frame globals
//   binding(0) : Globals uniform { vec2 inv_viewport; ... }
// group(1): material atlas
//   binding(0) : texture_2d<f32> atlas
//   binding(1) : sampler         atlas_sampler
// ---------------------------------------------------------------------------
namespace bind_group
{
inline constexpr std::uint32_t kGlobals = 0U;
inline constexpr std::uint32_t kAtlas   = 1U;
}  // namespace bind_group

namespace binding
{
inline constexpr std::uint32_t kGlobalsUbo   = 0U;  ///< group(0) binding(0)
inline constexpr std::uint32_t kAtlasTexture = 0U;  ///< group(1) binding(0)
inline constexpr std::uint32_t kAtlasSampler = 1U;  ///< group(1) binding(1)
}  // namespace binding

// ---------------------------------------------------------------------------
// Globals uniform — the per-frame constant block uploaded once per record().
//
// Holds the inverse viewport so the vertex shader can map pixel-space
// positions to clip space:  clip = pos * inv_viewport * 2 - 1  (with y flip).
// Laid out to match a std140 `vec2 inv_viewport; vec2 _pad;` (16 bytes).
// ---------------------------------------------------------------------------
struct Globals
{
    float inv_viewport_x { 0.0F };  ///< 1.0 / viewport_width
    float inv_viewport_y { 0.0F };  ///< 1.0 / viewport_height
    float pad0           { 0.0F };
    float pad1           { 0.0F };
};
static_assert(sizeof(Globals) == 16U, "Globals must be a std140-friendly 16-byte block");

/// Build the per-frame Globals block from a viewport extent.  A zero extent
/// dimension yields a zero inverse term (no divide-by-zero) — the vertex
/// shader then collapses that axis to the clip origin, which is the correct
/// degenerate behaviour for an empty viewport.
[[nodiscard]] constexpr Globals make_globals(std::uint32_t viewport_w,
                                             std::uint32_t viewport_h) noexcept
{
    Globals g {};
    g.inv_viewport_x = viewport_w == 0U
        ? 0.0F : 1.0F / static_cast<float>(viewport_w);
    g.inv_viewport_y = viewport_h == 0U
        ? 0.0F : 1.0F / static_cast<float>(viewport_h);
    return g;
}

// ---------------------------------------------------------------------------
// Colour conversion — RGBA8 (cd::ui::renderer::Color / Vertex) to normalized
// float, matching the Unorm8x4 vertex-format the GPU applies automatically.
// Exposed so host tests can pin the exact mapping the shader will see.
// ---------------------------------------------------------------------------

/// Normalize a single 0..255 channel to 0..1, exactly as Unorm8x4 does.
[[nodiscard]] constexpr float unorm8_to_float(std::uint8_t v) noexcept
{
    return static_cast<float>(v) / 255.0F;
}

/// One RGBA colour in normalized linear-storage float (the value the shader
/// receives from an Unorm8x4 attribute).  Order is r,g,b,a.
struct ColorF
{
    float r { 0.0F };
    float g { 0.0F };
    float b { 0.0F };
    float a { 0.0F };

    [[nodiscard]] constexpr bool operator==(const ColorF&) const noexcept = default;
};

/// Convert a packed RGBA8 colour to the normalized float vector the GPU
/// Unorm8x4 path produces.
[[nodiscard]] constexpr ColorF to_color_f(const cd::ui::renderer::Color& c) noexcept
{
    return ColorF {
        unorm8_to_float(c.r),
        unorm8_to_float(c.g),
        unorm8_to_float(c.b),
        unorm8_to_float(c.a),
    };
}

// ---------------------------------------------------------------------------
// Scissor-clamp math — extracted from the Dawn-only record() path so it is
// testable on every tier.  WebGPU's validation layer rejects a scissor rect
// with negative origin or one that extends past the attachment, so the
// origin is floored at 0 and the extent clamped to the viewport.
// ---------------------------------------------------------------------------

/// A scissor rectangle in WebGPU's unsigned SetScissorRect parameter space.
struct ScissorPx
{
    std::uint32_t x      { 0U };
    std::uint32_t y      { 0U };
    std::uint32_t width  { 0U };
    std::uint32_t height { 0U };

    [[nodiscard]] constexpr bool operator==(const ScissorPx&) const noexcept = default;
};

/// Clamp a batcher ScissorRect to the [0, viewport] range that
/// wgpu::RenderPassEncoder::SetScissorRect accepts.
///
///   * a negative x/y origin is floored to 0 (and the lost extent removed so
///     the rect's far edge is preserved, never extended);
///   * the far edge (origin + extent) is clamped to the viewport edge;
///   * the batcher's 0xFFFFFFFF "no-clip" sentinel collapses to the full
///     viewport.
[[nodiscard]] constexpr ScissorPx clamp_scissor(const cd::ui::renderer::ScissorRect& s,
                                                std::uint32_t viewport_w,
                                                std::uint32_t viewport_h) noexcept
{
    // Floor the origin at 0; a negative origin shrinks the visible extent by
    // the clipped-off amount so the far edge does not drift outward.
    const auto raw_x = static_cast<std::int64_t>(s.x);
    const auto raw_y = static_cast<std::int64_t>(s.y);

    const std::uint32_t ox = raw_x < 0 ? 0U : static_cast<std::uint32_t>(
        std::min<std::int64_t>(raw_x, static_cast<std::int64_t>(viewport_w)));
    const std::uint32_t oy = raw_y < 0 ? 0U : static_cast<std::uint32_t>(
        std::min<std::int64_t>(raw_y, static_cast<std::int64_t>(viewport_h)));

    // Far edge in signed space, then clamp to the viewport edge.
    const std::int64_t far_x = raw_x + static_cast<std::int64_t>(s.width);
    const std::int64_t far_y = raw_y + static_cast<std::int64_t>(s.height);

    const std::int64_t clamped_far_x =
        std::min<std::int64_t>(far_x, static_cast<std::int64_t>(viewport_w));
    const std::int64_t clamped_far_y =
        std::min<std::int64_t>(far_y, static_cast<std::int64_t>(viewport_h));

    // Width/height in signed space; clamp the (possibly negative) span to zero.
    // Subtract-then-compare-to-0 keeps both operands signed (avoids a mixed
    // signed/unsigned comparison).
    const std::int64_t w64 = clamped_far_x - static_cast<std::int64_t>(ox);
    const std::int64_t h64 = clamped_far_y - static_cast<std::int64_t>(oy);
    const std::uint32_t w = w64 <= 0 ? 0U : static_cast<std::uint32_t>(w64);
    const std::uint32_t h = h64 <= 0 ? 0U : static_cast<std::uint32_t>(h64);

    return ScissorPx { ox, oy, w, h };
}

// ---------------------------------------------------------------------------
// Draw-command translation — turn a batcher DrawCommand's index range into
// the byte offset wgpu::RenderPassEncoder needs.  WebGPU's DrawIndexed takes
// a firstIndex in *index units*, while SetIndexBuffer takes a *byte* offset;
// this helper exposes both so the Dawn path (and its tests) agree.
// ---------------------------------------------------------------------------

struct DrawSlice
{
    std::uint32_t first_index        { 0U };  ///< DrawIndexed firstIndex (index units)
    std::uint32_t index_count        { 0U };  ///< DrawIndexed indexCount
    std::uint64_t index_byte_offset  { 0U };  ///< first_index * index_size_bytes()
};

/// Translate a batcher DrawCommand into a DrawSlice (index units + byte
/// offset).  Pure arithmetic; no GPU state.
[[nodiscard]] constexpr DrawSlice
draw_slice(const cd::ui::renderer::DrawCommand& dc) noexcept
{
    return DrawSlice {
        dc.index_offset,
        dc.index_count,
        static_cast<std::uint64_t>(dc.index_offset) * index_size_bytes(),
    };
}

// ---------------------------------------------------------------------------
// WGSL shader source — the UI vertex + fragment shaders.
//
// These are the canonical UI shaders the Dawn pipeline compiles.  They are
// authored against the Vertex layout above (4 attributes) and the 6 material
// variant ids from cd::ui::renderer::material.  Stored as a single module so
// one wgpu::ShaderModule covers both entry points (`vs_main`, `fs_main`).
//
// NOT compiled here (no host WGSL validator dependency); the tests pin the
// structural contract (attribute locations, variant branch ids, entry-point
// names, bind-group indices) so a drift vs. the Vertex / material enums fails
// on revert.
// ---------------------------------------------------------------------------
inline constexpr std::string_view kUiShaderWgsl = R"WGSL(
// CHROMODYNAMIC UI shader (cd::ui::renderer_webgpu).
// Vertex layout matches cd::ui::renderer::Vertex (24-byte stride).
// Variant ids: 0 solid, 1 textured, 2 glyph, 3 nine-patch, 4 grad, 5 blur.

struct Globals {
    inv_viewport : vec2<f32>,
    _pad         : vec2<f32>,
};

@group(0) @binding(0) var<uniform> globals : Globals;
@group(1) @binding(0) var atlas         : texture_2d<f32>;
@group(1) @binding(1) var atlas_sampler : sampler;

struct VsIn {
    @location(0) pos     : vec2<f32>,
    @location(1) uv      : vec2<f32>,
    @location(2) color   : vec4<f32>,
    @location(3) variant : vec4<u32>,
};

struct VsOut {
    @builtin(position) clip    : vec4<f32>,
    @location(0)       uv      : vec2<f32>,
    @location(1)       color   : vec4<f32>,
    // Integer inter-stage variables MUST be flat-interpolated (WGSL spec):
    // a non-flat integral vertex output is a Tint compile error.
    @location(2) @interpolate(flat) variant : u32,
};

@vertex
fn vs_main(in : VsIn) -> VsOut {
    var out : VsOut;
    // Pixel-space -> normalized device coordinates (y flipped: pixel origin
    // is top-left, NDC origin is bottom-left).
    let ndc = in.pos * globals.inv_viewport * 2.0 - vec2<f32>(1.0, 1.0);
    out.clip    = vec4<f32>(ndc.x, -ndc.y, 0.0, 1.0);
    out.uv      = in.uv;
    out.color   = in.color;
    out.variant = in.variant.x;
    return out;
}

@fragment
fn fs_main(in : VsOut) -> @location(0) vec4<f32> {
    // Sample the atlas ONCE at uniform control flow.  textureSampleLevel (LOD 0,
    // no implicit derivatives) is mandatory here: a plain textureSample inside
    // the per-variant `if` branches below is non-uniform control flow, which
    // WGSL rejects ("must only be called from uniform control flow").  UI atlas
    // sampling is point/linear at a single mip, so an explicit LOD is correct.
    let tex = textureSampleLevel(atlas, atlas_sampler, in.uv, 0.0);

    // kSolid (0): flat colour.
    if (in.variant == 0u) {
        return in.color;
    }
    // kTextured (1): texture * colour (premultiplied alpha upstream).
    if (in.variant == 1u) {
        return tex * in.color;
    }
    // kGlyph (2): alpha-only atlas -> colour modulated by sampled red.
    if (in.variant == 2u) {
        return vec4<f32>(in.color.rgb, in.color.a * tex.r);
    }
    // kNinePatch (3): same sampling as textured; tiling handled CPU-side.
    if (in.variant == 3u) {
        return tex * in.color;
    }
    // kLinearGrad (4) / kBlur (5): fall back to the vertex colour until the
    // dedicated paths land (matches the batcher's current geometry output).
    return in.color;
}
)WGSL";

/// The vertex-stage entry-point name in kUiShaderWgsl.
inline constexpr std::string_view kVertexEntryPoint   = "vs_main";
/// The fragment-stage entry-point name in kUiShaderWgsl.
inline constexpr std::string_view kFragmentEntryPoint = "fs_main";

}  // namespace cd::ui::renderer_webgpu
