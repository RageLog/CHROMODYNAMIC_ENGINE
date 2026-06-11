// =============================================================================
// CHROMODYNAMIC — cd/net/SnapshotReconciler.cpp
// Phase 468 / M0 wave — translation unit for SnapshotReconciler<T>.
//
// `SnapshotReconciler` is a class template; this TU pins a header
// include + a non-template stable symbol so the library compile catches
// header regressions and the file is visible to clang-tidy / IWYU.
// =============================================================================
#include <cd/net/SnapshotReconciler.hpp>

namespace cd::net
{

// NOLINTNEXTLINE(misc-use-internal-linkage) — consumed cross-TU by the net tests via a local declaration.
const char* snapshot_reconciler_translation_unit() noexcept
{
    return "cd::net::SnapshotReconciler @ SnapshotReconciler.cpp";
}

}  // namespace cd::net
