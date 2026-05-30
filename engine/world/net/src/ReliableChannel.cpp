// =============================================================================
// CHROMODYNAMIC — cd/net/ReliableChannel.cpp
// Phase 468 / M0 wave — translation unit for AckWindowChannel.
//
// `AckWindowChannel` is inline / header-only by design (every method
// would inline cleanly into hot send/tick loops); this TU exists to:
//
//   1. Force a full instantiation against the header in EVERY build
//      configuration so missing-include / template-error regressions
//      surface in the library compile rather than the first downstream
//      consumer.
//   2. Anchor the file in the CMake source list for static-analysis
//      tooling (clang-tidy, clang-format, IWYU) — `cd_add_library`
//      walks the SOURCES list, not the include tree.
//   3. Provide a stable symbol — `ack_window_channel_translation_unit()`
//      — that callers can take the address of to verify the symbol
//      table at link time (used by the test binary's static_assert).
// =============================================================================
#include <cd/net/ReliableChannel.hpp>

namespace cd::net
{

const char* ack_window_channel_translation_unit() noexcept
{
    return "cd::net::AckWindowChannel @ ReliableChannel.cpp";
}

}  // namespace cd::net
