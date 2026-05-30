// =============================================================================
// CHROMODYNAMIC - cd/game/fsm/Fsm.cpp
// Phase 472 (G2.1)
//
// cd::game::fsm is a header-only template library; this .cpp exists only to
// keep the library a non-INTERFACE STATIC target so it shows up uniformly in
// the install / export rules with the rest of the engine libraries (CLAUDE.md
// S7 / S10 - target naming convention).
// =============================================================================
#include <cd/game/fsm/Fsm.hpp>

namespace cd::game::fsm
{

// Translation-unit anchor. Touching nothing here ensures the symbol table
// for cd_game_fsm.lib is not empty under MSVC's /OPT:REF.
namespace detail
{
[[maybe_unused]] inline void anchor() noexcept {}
}  // namespace detail

}  // namespace cd::game::fsm
