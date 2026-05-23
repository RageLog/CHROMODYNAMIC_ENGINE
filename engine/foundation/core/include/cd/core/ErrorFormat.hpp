// =============================================================================
// CHROMODYNAMIC — cd/core/ErrorFormat.hpp
// Phase 10 / Sprint 1 / Wave 109 — domain+code name registry for errors.
//
// `ErrorCode` carries a numeric (domain, code) plus a non-owning
// `std::string_view` message. That's enough for routing and equality,
// but log-friendly formatting needs human names: "core::InvalidArgument"
// reads, "0:2" does not.
//
// This header adds:
//   * register_domain(name, code_lookup) — subsystems call this once at
//     startup to register their domain ID and a callback that maps
//     code-within-domain → name.
//   * format(ec) — returns "domain::code: msg" if both are registered,
//     "0x00ab:7 msg" otherwise (best-effort).
//
// Storage is process-wide, guarded by an internal mutex (registration
// is rare and out of the hot path). The core domain registers itself
// from the cd_core TU at first use.
//
// Why not enum→string macros? Subsystems live in independent libraries
// and may even be loaded as plugins — a static enum reflection won't
// reach across boundaries. A small runtime registry costs one mutex
// hit per format() and zero per error construction.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>

#include <cstdint>
#include <string>
#include <string_view>

/// @addtogroup cd_foundation_core
/// @{
namespace cd::core
{

/// Callback shape: code-within-domain → name (empty if unknown).
using CodeNameFn = std::string_view (*)(std::uint32_t code);

/// Register (or replace) the human-readable name for a domain and the
/// callback that resolves codes-within-domain. The pointers must outlive
/// the process; pass static string literals.
void register_domain(std::uint32_t domain, std::string_view name, CodeNameFn lookup) noexcept;

/// Read back a registered domain name (empty if unregistered).
[[nodiscard]] std::string_view domain_name(std::uint32_t domain) noexcept;

/// Read back a code name within a domain (empty if unregistered or unknown).
[[nodiscard]] std::string_view code_name(std::uint32_t domain, std::uint32_t code) noexcept;

/// Pretty-print an ErrorCode. Format options:
///   "core::InvalidArgument: <msg>"       — both names known, message present
///   "core::InvalidArgument"              — both names known, no message
///   "core::0x07"                         — domain known, code unknown
///   "0x0000:0x02: <msg>"                 — neither known
/// Always returns a self-contained owned string.
[[nodiscard]] std::string format(const ErrorCode& ec);

}  // namespace cd::core
/// @}
