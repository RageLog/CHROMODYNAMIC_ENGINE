// =============================================================================
// CHROMODYNAMIC — cd/rhi/TlasBuilder.hpp
//
// Phase 656 (M11 W2A / T1.11) — TLAS coverage builder.
//
// Host-side helper that turns a span of `TlasInstanceCandidate` (the
// engine-facing struct that pairs an `AccelInstance` with the
// `tlas_eligible` gate) into the flat `std::vector<AccelInstance>` ready
// to feed into `AccelStructureDesc::instances`.
//
// Contract -- see Descriptors.hpp `TlasInstanceCandidate` doc-comment for
// the full statement. Short form:
//
//   * `build_acceleration_structure` accepts ALL geometry kinds.  There
//     is NO geometry-kind filter at the RHI level.
//   * The host-side `tlas_eligible` gate is the single, documented knob
//     for opting an instance out of the TLAS.  It defaults to TRUE so a
//     glTF prim enters the TLAS without per-asset setup.
//   * Builder helper preserves the input candidate ordering for the
//     subset that survives the eligibility gate.  GPU
//     `rayQueryGetIntersectionInstanceIdEXT` indexing into per-instance
//     SSBOs relies on this stable dense ordering.
// =============================================================================
#pragma once

#include <cd/rhi/Descriptors.hpp>

#include <cstddef>
#include <span>
#include <vector>

namespace cd::rhi
{

/// Filter a span of `TlasInstanceCandidate` down to the
/// `AccelInstance`s whose host-side `tlas_eligible` flag is true.
///
/// Output ordering preserves the input ordering for surviving entries.
/// `out` is `clear()`ed before the walk; the caller may reuse a scratch
/// vector across frames without reallocating.
///
/// @returns the number of eligible instances appended to `out`.
inline std::size_t build_tlas_instances(
    std::span<const TlasInstanceCandidate> candidates,
    std::vector<AccelInstance>&            out) noexcept
{
    out.clear();
    out.reserve(candidates.size());
    for (const auto& c : candidates)
    {
        if (!c.tlas_eligible)
            continue;
        out.push_back(c.instance);
    }
    return out.size();
}

/// Overload that returns the filtered vector by value.  Convenient for
/// one-shot scene-ingest paths where the caller does not maintain a
/// scratch buffer.
[[nodiscard]] inline std::vector<AccelInstance> build_tlas_instances(
    std::span<const TlasInstanceCandidate> candidates)
{
    std::vector<AccelInstance> out;
    (void)build_tlas_instances(candidates, out);
    return out;
}

}  // namespace cd::rhi
