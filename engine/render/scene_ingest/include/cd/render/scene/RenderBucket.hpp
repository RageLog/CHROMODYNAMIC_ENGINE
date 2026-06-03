// =============================================================================
// CHROMODYNAMIC — cd/render/scene/RenderBucket.hpp
// M11 W1 — T1.15 (HIGHEST LEVERAGE): two-pass alpha render order.
//
// Splits a scene's draw-primitive set into three render buckets that the
// framegraph issues as sequential passes:
//
//   1. kOpaque     — depth-tested + depth-written, front-to-back early-Z.
//   2. kAlphaMask  — after Opaque, depth-tested + depth-written, fragment
//                    discarded when sampled alpha < material cutoff. Safe to
//                    sort with the opaque set for early-Z because the
//                    discard happens after the depth test.
//   3. kAlphaBlend — after both, back-to-front sort for correct over-blend,
//                    depth-tested + NOT depth-written. Drawing this set last
//                    is what stops the "curtain bleed-through" failure mode
//                    documented in
//                    docs/AUDIT/learned-lessons-pbr-rt-and-curtain-alpha-2026-06-03.md:
//                    glTF alpha-blended primitives drawn before their
//                    opaque background occluded the opaque geometry because
//                    blend writes had already populated the depth buffer.
//
// Why a tiny dedicated header (not buried inside SceneIngest.hpp or
// Renderer.hpp):
//   * Both the engine-side scene aggregator (cd::render::scene_ingest) and
//     the framegraph render passes need to share the same routing rule.
//   * Sample / editor / test code wants to call `bucket_for` directly when
//     building draw lists without dragging in the full ingest pipeline.
//   * Header-only + trivially copyable so it is safe to use inside a hot
//     classification loop over thousands of primitives.
//
// The classifier reads `cd::material::MaterialInstance` via the T1.16
// alpha-mode predicate accessors (is_blend / is_mask / is_opaque) so the
// routing rule lives in exactly one place. Default-constructed
// `MaterialInstance` returns `kOpaque` per the glTF 2.0 default alphaMode.
// =============================================================================
#pragma once

#include <cd/material/Material.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cd::render::scene
{

/// Three-bucket draw-call partition. Numeric values are stable and may be
/// used as array indices into a per-bucket fixed-size container (see
/// `kBucketCount` and `BucketedPrims::buckets`).
enum class RenderBucket : std::uint8_t
{
    kOpaque     = 0,  ///< standard depth-test + depth-write, sort front-to-back.
    kAlphaMask  = 1,  ///< same depth state as opaque; fragment discard on alpha < cutoff.
    kAlphaBlend = 2,  ///< depth-test on, depth-write OFF, sort back-to-front, over-blend.
};

inline constexpr std::size_t kBucketCount = 3;

/// Route a `MaterialInstance` to its bucket. The rule is fixed by the
/// material's glTF-mirrored alpha mode and exists in exactly one place
/// (this function) so framegraph passes, the editor preview path and the
/// sample renderer all agree on which bucket a primitive lands in.
///
/// Default-constructed `MaterialInstance` is `kOpaque`. A material whose
/// alpha mode has been mutated by any of `set_alpha_mode` /
/// `set_alpha_params` (e.g. by the glTF ingest path) routes according to
/// the resulting predicate accessor.
[[nodiscard]] constexpr RenderBucket
bucket_for(const cd::material::MaterialInstance& m) noexcept
{
    // Order matters: blend wins over mask wins over opaque, mirroring the
    // glTF spec's intent (a material is at most one of the three states).
    if (m.is_blend())
        return RenderBucket::kAlphaBlend;
    if (m.is_mask())
        return RenderBucket::kAlphaMask;
    return RenderBucket::kOpaque;
}

/// Convenience overload for code paths that already pulled the alpha mode
/// scalar out of the material (e.g. ECS component readers).
[[nodiscard]] constexpr RenderBucket
bucket_for(cd::material::AlphaMode m) noexcept
{
    switch (m)
    {
        case cd::material::AlphaMode::kBlend: return RenderBucket::kAlphaBlend;
        case cd::material::AlphaMode::kMask:  return RenderBucket::kAlphaMask;
        case cd::material::AlphaMode::kOpaque:
        default:                              return RenderBucket::kOpaque;
    }
}

// ---- Bucket-split primitive list -------------------------------------------
//
// A scene's draw set is a flat vector of opaque "prim handles" (anything
// the caller's render path knows how to draw). The framegraph does NOT
// need to know what's inside — it only needs three contiguous spans to
// loop over in the right pass order.
//
// `PrimHandle` is a 32-bit opaque ID the caller assigns; the engine uses
// it to look up GPU mesh + material on the draw-call side. Keeping it
// trivially copyable lets us cheaply move between buckets.

using PrimHandle = std::uint32_t;

/// Output of `partition_prims`: three independent prim-handle vectors,
/// stable-indexed by `RenderBucket` numeric value via
/// `BucketedPrims::buckets[size_t(bucket)]`. Span accessors below provide
/// the immutable views the framegraph consumes.
struct BucketedPrims
{
    std::array<std::vector<PrimHandle>, kBucketCount> buckets {};

    [[nodiscard]] std::span<const PrimHandle> opaque_prims() const noexcept
    {
        return std::span<const PrimHandle>(buckets[static_cast<std::size_t>(RenderBucket::kOpaque)]);
    }

    [[nodiscard]] std::span<const PrimHandle> alpha_mask_prims() const noexcept
    {
        return std::span<const PrimHandle>(buckets[static_cast<std::size_t>(RenderBucket::kAlphaMask)]);
    }

    [[nodiscard]] std::span<const PrimHandle> alpha_blend_prims() const noexcept
    {
        return std::span<const PrimHandle>(buckets[static_cast<std::size_t>(RenderBucket::kAlphaBlend)]);
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return buckets[0].size() + buckets[1].size() + buckets[2].size();
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return buckets[0].empty() && buckets[1].empty() && buckets[2].empty();
    }
};

/// Partition `prims` (parallel array with `materials`) into three buckets.
/// Inputs must satisfy `prims.size() == materials.size()`; mismatched sizes
/// short-circuit to an empty result (defensive — the caller is expected to
/// keep these two arrays index-aligned).
///
/// Preserves input order within each bucket so callers can apply a stable
/// secondary sort (front-to-back / back-to-front) afterwards without
/// losing primitive identity. The partition itself is O(N) and allocation-
/// only on the per-bucket vectors.
[[nodiscard]] inline BucketedPrims
partition_prims(std::span<const PrimHandle>                          prims,
                std::span<const cd::material::MaterialInstance* const> materials) noexcept
{
    BucketedPrims out {};
    if (prims.size() != materials.size())
        return out;

    // Reserve a conservative upper bound: opaque is usually >>50% of the
    // scene, so paying for one allocation up-front avoids two reallocs as
    // the buckets fill.
    out.buckets[static_cast<std::size_t>(RenderBucket::kOpaque)].reserve(prims.size());

    for (std::size_t i = 0; i < prims.size(); ++i)
    {
        const cd::material::MaterialInstance* mat = materials[i];
        const RenderBucket b = (mat != nullptr)
                               ? bucket_for(*mat)
                               : RenderBucket::kOpaque;  // null material → opaque default
        out.buckets[static_cast<std::size_t>(b)].push_back(prims[i]);
    }

    return out;
}

}  // namespace cd::render::scene
