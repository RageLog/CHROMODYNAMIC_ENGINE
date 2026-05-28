// =============================================================================
// HelloTlasRing.hpp
// -----------------------------------------------------------------------------
// hello_engine-local POD aggregate carrying the deferred-destroy entry
// for a TLAS that has just been rotated out of the descriptor set. The
// renderer is double-buffered (frames_in_flight = 2), so a TLAS handle
// still in-flight in the queue at submit-time cannot be destroyed for
// at least 2 frames. We use a defensive 3-frame margin (matches the
// destroy_at_frame convention in main()'s per-frame TLAS rebuild).
//
// Lifted out of main() in Marathon Run 11 phase N9-prep so the upcoming
// rebuild_tlas_and_transition_depth helper can take a queue ref instead
// of templating on the type.
// =============================================================================
#pragma once

#include <cd/rhi/Descriptors.hpp>

#include <cstdint>

namespace cd_sample {

// Per-frame TLAS scratch entry. The renderer points at `current_tlas`
// this frame; this struct describes a handle whose destroy must wait
// until the renderer has cycled past the submission that referenced it.
struct DeferredTlas
{
    cd::rhi::AccelStructureHandle h;
    std::uint32_t destroy_at_frame;
};

}  // namespace cd_sample
