// =============================================================================
// CHROMODYNAMIC — engine/ui/font/src/MsdfgenBackend.cpp
//
// Phase 752 Sprint-2 — multi-channel signed distance field via msdfgen 1.13.
//
// This TU is compiled ONLY when BOTH conditions hold:
//   * CD_UI_FONT_HAVE_MSDFGEN = 1  (msdfgen::msdfgen-core linked)
//   * CD_UI_FONT_HAVE_FREETYPE = 1 (FreeType face available for outlines)
//
// Strategy:
//   1. FreeType's FT_Outline_Decompose() walks the glyph outline, producing
//      moveTo / lineTo / conicTo / cubicTo callbacks.
//   2. The callbacks fill an msdfgen::Shape (contours + edges).
//   3. msdfgen::edgeColoringSimple() assigns edge colors for the MSDF math.
//   4. msdfgen::generateMSDF() renders the shape into an msdfgen::Bitmap
//      (float, 3 channels) at the requested pixel dimensions.
//   5. The float result is packed to uint8 with the bias convention
//      (zero-crossing at 128; values in [0,255]).
//   6. The interleaved R/G/B uint8 buffer replaces `dst` in the caller.
//
// Reference: Chlumsky/msdfgen 1.13 (MIT).
//            https://github.com/Chlumsky/msdfgen
// =============================================================================
#include <cd/ui/font/Font.hpp>

#include "FontBackend.hpp"

#if !(defined(CD_UI_FONT_HAVE_MSDFGEN)  && CD_UI_FONT_HAVE_MSDFGEN)  || \
    !(defined(CD_UI_FONT_HAVE_FREETYPE) && CD_UI_FONT_HAVE_FREETYPE)
// Nothing to compile — Font.cpp provides the inline fallback stub.
#else

// msdfgen core headers (no extensions — no font loading, no PNG, no SVG).
// The include path is set by target_include_directories from msdfgen::msdfgen-core.
#include <msdfgen/msdfgen.h>

// FreeType for outline decomposition.
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cd::ui::font
{

// ---------------------------------------------------------------------------
// Internal state threaded through the FT outline decompose callbacks.
// Each callback appends edges to the current msdfgen::Contour.
// ---------------------------------------------------------------------------
struct MsdfOutlineCtx
{
    msdfgen::Shape*   shape   { nullptr };
    msdfgen::Contour* contour { nullptr };
    msdfgen::Point2   pos     {};

    // FreeType 26.6 fixed-point → double pixel units.
    static constexpr double k26Dot6 = 1.0 / 64.0;
};

// ---------------------------------------------------------------------------
// FT_Outline callbacks — static C functions satisfying FT_Outline_Funcs.
// ---------------------------------------------------------------------------
static int ft_move_to(const FT_Vector* to, void* user) noexcept
{
    auto* ctx = static_cast<MsdfOutlineCtx*>(user);
    ctx->contour = &ctx->shape->addContour();
    ctx->pos = msdfgen::Point2 {
        static_cast<double>(to->x) * MsdfOutlineCtx::k26Dot6,
        static_cast<double>(to->y) * MsdfOutlineCtx::k26Dot6
    };
    return 0;
}

static int ft_line_to(const FT_Vector* to, void* user) noexcept
{
    auto* ctx = static_cast<MsdfOutlineCtx*>(user);
    if (!ctx->contour) return 0;

    msdfgen::Point2 p1 {
        static_cast<double>(to->x) * MsdfOutlineCtx::k26Dot6,
        static_cast<double>(to->y) * MsdfOutlineCtx::k26Dot6
    };
    if (ctx->pos != p1)
    {
        ctx->contour->addEdge(msdfgen::EdgeHolder(ctx->pos, p1));
    }
    ctx->pos = p1;
    return 0;
}

// TrueType quadratic Bézier (one off-curve control point).
static int ft_conic_to(const FT_Vector* ctl, const FT_Vector* to, void* user) noexcept
{
    auto* ctx = static_cast<MsdfOutlineCtx*>(user);
    if (!ctx->contour) return 0;

    msdfgen::Point2 p1 {
        static_cast<double>(ctl->x) * MsdfOutlineCtx::k26Dot6,
        static_cast<double>(ctl->y) * MsdfOutlineCtx::k26Dot6
    };
    msdfgen::Point2 p2 {
        static_cast<double>(to->x) * MsdfOutlineCtx::k26Dot6,
        static_cast<double>(to->y) * MsdfOutlineCtx::k26Dot6
    };
    ctx->contour->addEdge(msdfgen::EdgeHolder(ctx->pos, p1, p2));
    ctx->pos = p2;
    return 0;
}

// OpenType CFF cubic Bézier (two off-curve control points).
static int ft_cubic_to(const FT_Vector* ctl1, const FT_Vector* ctl2,
                        const FT_Vector* to, void* user) noexcept
{
    auto* ctx = static_cast<MsdfOutlineCtx*>(user);
    if (!ctx->contour) return 0;

    msdfgen::Point2 p1 {
        static_cast<double>(ctl1->x) * MsdfOutlineCtx::k26Dot6,
        static_cast<double>(ctl1->y) * MsdfOutlineCtx::k26Dot6
    };
    msdfgen::Point2 p2 {
        static_cast<double>(ctl2->x) * MsdfOutlineCtx::k26Dot6,
        static_cast<double>(ctl2->y) * MsdfOutlineCtx::k26Dot6
    };
    msdfgen::Point2 p3 {
        static_cast<double>(to->x) * MsdfOutlineCtx::k26Dot6,
        static_cast<double>(to->y) * MsdfOutlineCtx::k26Dot6
    };
    ctx->contour->addEdge(msdfgen::EdgeHolder(ctx->pos, p1, p2, p3));
    ctx->pos = p3;
    return 0;
}

// ---------------------------------------------------------------------------
// Public entry point — called from Font.cpp when atlas_mode_ == kMsdfMulti.
// ---------------------------------------------------------------------------
namespace msdfgen_backend
{

// NOLINTNEXTLINE(misc-use-internal-linkage) — consumed cross-TU by Font.cpp via a local declaration.
void to_msdf_multi(FreeTypeBackend&           ft,
                   std::uint32_t               cp,
                   std::vector<std::uint8_t>&  dst,
                   std::uint32_t               w,
                   std::uint32_t               h)
{
    const std::size_t out_bytes = static_cast<std::size_t>(w) * h * 3U;

    if (!ft.face || w == 0U || h == 0U)
    {
        dst.assign(out_bytes, std::uint8_t { 0 });
        return;
    }

    // ---- 1. Load the glyph outline into FreeType. -------------------------
    const FT_UInt gi = FT_Get_Char_Index(ft.face, cp);
    if (gi == 0)
    {
        dst.assign(out_bytes, std::uint8_t { 0 });
        return;
    }
    // FT_LOAD_NO_BITMAP forces the outline to be loaded even for bitmap-
    // embedded fonts. After FT_Set_Char_Size, the outline coords are in
    // 26.6 fixed-point pixel units (scaled to the current pixel size).
    if (FT_Load_Glyph(ft.face, gi, FT_LOAD_NO_BITMAP) != 0)
    {
        dst.assign(out_bytes, std::uint8_t { 0 });
        return;
    }
    if (ft.face->glyph->format != FT_GLYPH_FORMAT_OUTLINE)
    {
        // Bitmap font — no outline available.
        dst.assign(out_bytes, std::uint8_t { 0 });
        return;
    }

    // ---- 2. Decompose outline into msdfgen Shape. -------------------------
    msdfgen::Shape shape;
    // FreeType Y grows upward (positive = up). msdfgen default (Y_UPWARD)
    // matches this, so we do NOT set inverseYAxis.
    // shape.inverseYAxis is deprecated in 1.13; use setYAxisOrientation.
    shape.setYAxisOrientation(msdfgen::Y_UPWARD);

    MsdfOutlineCtx ctx;
    ctx.shape = &shape;

    FT_Outline_Funcs funcs {};
    funcs.move_to  = ft_move_to;
    funcs.line_to  = ft_line_to;
    funcs.conic_to = ft_conic_to;
    funcs.cubic_to = ft_cubic_to;
    funcs.shift    = 0;
    funcs.delta    = 0;

    FT_Outline& outline = ft.face->glyph->outline;
    if (FT_Outline_Decompose(&outline, &funcs, &ctx) != 0 || shape.contours.empty())
    {
        dst.assign(out_bytes, std::uint8_t { 0 });
        return;
    }

    // ---- 3. Normalize + color edges. -------------------------------------
    shape.normalize();
    msdfgen::edgeColoringSimple(shape, /* angleThreshold = */ 3.0);

    // ---- 4. Compute bounds and build the SDFTransformation. --------------
    double xMin = 0.0;
    double yMin = 0.0;
    double xMax = 0.0;
    double yMax = 0.0;
    shape.bound(xMin, yMin, xMax, yMax);

    // The SDF range (in pixel units): how far outside/inside the glyph the
    // SDF is non-trivial. 4 pixels covers typical text rendering scenarios.
    constexpr double kRange = 4.0;

    const double shape_w = (xMax - xMin);
    const double shape_h = (yMax - yMin);
    double scale = 1.0;
    if (shape_w > 0.0 && shape_h > 0.0)
    {
        const double sx = static_cast<double>(w) / (shape_w + 2.0 * kRange);
        const double sy = static_cast<double>(h) / (shape_h + 2.0 * kRange);
        scale = std::min(sx, sy);
    }

    // Translate so the glyph is centered in the bitmap with kRange padding.
    const double translate_x = -xMin + kRange / scale;
    const double translate_y = -yMin + kRange / scale;

    msdfgen::Projection projection {
        msdfgen::Vector2(scale,       scale),
        msdfgen::Vector2(translate_x, translate_y)
    };

    // DistanceMapping from Range(kRange) maps signed distances in
    // [-kRange, +kRange] to the normalised float range [0, 1].
    msdfgen::Range range { kRange };
    msdfgen::SDFTransformation transform { projection, msdfgen::DistanceMapping(range) };

    // ---- 5. Generate the MSDF into a float bitmap. -----------------------
    // Y_UPWARD (default): row 0 of the bitmap is the BOTTOM of the image.
    // We request Y_DOWNWARD so row 0 = top of image (matches our texture UV).
    msdfgen::Bitmap<float, 3> msdf_out(static_cast<int>(w), static_cast<int>(h),
                                        msdfgen::Y_DOWNWARD);

    msdfgen::MSDFGeneratorConfig cfg;
    cfg.overlapSupport = true;  // handles overlapping contours common in fonts

    msdfgen::generateMSDF(msdf_out, shape, transform, cfg);

    // ---- 6. Error correction + pack to uint8. ----------------------------
    msdfgen::msdfErrorCorrection(msdf_out, shape, transform, cfg);

    dst.resize(out_bytes);

    // Pack float [0, 1] (already mapped by DistanceMapping) → uint8 [0, 255].
    // The SDF boundary (zero-crossing) maps to 0.5 float = 128 uint8.
    for (std::uint32_t row = 0U; row < h; ++row)
    {
        for (std::uint32_t col = 0U; col < w; ++col)
        {
            // With Y_DOWNWARD, row 0 is at the top — matches our texture layout.
            const float* src_px = msdf_out(static_cast<int>(col),
                                            static_cast<int>(row));
            std::uint8_t* dst_px =
                dst.data() + (static_cast<std::size_t>(row) * w + col) * 3U;

            for (int c = 0; c < 3; ++c)
            {
                // src_px[c] is already in [0, 1] after DistanceMapping.
                const int packed = static_cast<int>(src_px[c] * 255.0F + 0.5F);
                dst_px[c] = static_cast<std::uint8_t>(std::clamp(packed, 0, 255));
            }
        }
    }
}

}  // namespace msdfgen_backend
}  // namespace cd::ui::font

#endif  // CD_UI_FONT_HAVE_MSDFGEN && CD_UI_FONT_HAVE_FREETYPE
