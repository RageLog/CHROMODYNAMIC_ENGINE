// =============================================================================
// CHROMODYNAMIC — cd/core placeholder TU
// Phase 2 Sprint S2.0 — keeps cd_core a non-INTERFACE library so it can carry
// per-target compile definitions (CD_CORE_BUILD_DLL etc.). Real cd::core code
// lands in Sprint S2.1 (Result, Handle, HandleStore, CVarRegistry, etc.).
// =============================================================================
#include <cd/core/Defines.hpp>
#include <cd/core/Version.hpp>

namespace cd::core::detail
{

// Force ODR-used symbol so linker doesn't drop the TU.
CD_CORE_API const char* engine_name() noexcept
{
    return kEngineName.data();
}

}  // namespace cd::core::detail
