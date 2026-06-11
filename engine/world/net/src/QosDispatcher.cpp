// =============================================================================
// CHROMODYNAMIC — cd/net/QosDispatcher.cpp
// Phase 468 / M0 wave — translation unit for QosDispatcher.
//
// Header-only by design; this TU forces a clean instantiation in every
// build, anchors the file in cd_add_library SOURCES, and exposes a
// stable symbol for link-time presence verification.
// =============================================================================
#include <cd/net/QosDispatcher.hpp>

namespace cd::net
{

// NOLINTNEXTLINE(misc-use-internal-linkage) — consumed cross-TU by the net tests via a local declaration.
const char* qos_dispatcher_translation_unit() noexcept
{
    return "cd::net::QosDispatcher @ QosDispatcher.cpp";
}

}  // namespace cd::net
