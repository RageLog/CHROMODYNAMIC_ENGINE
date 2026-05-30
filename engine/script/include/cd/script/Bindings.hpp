// =============================================================================
// CHROMODYNAMIC — cd/script/Bindings.hpp
// Phase 467 — Lua bindings for cd::math, cd::ecs, cd::scene.
//
// Layers on top of cd::script::Engine. The public header is intentionally
// lua.h-free; the .cpp pulls Lua + cd::math + cd::ecs + cd::scene and wires
// userdata + metatables.
//
// Bound surface (Phase 467):
//   * cd.Vec3f(x, y, z) / cd.Vec4f(x, y, z, w) / cd.Mat4f.identity()
//       Arithmetic via __add / __sub / __mul metamethods. Vec * Vec is
//       component-wise; Mat * Vec and Mat * Mat use math::operator*.
//   * cd.World()  → userdata with :create() / :destroy(e) / :is_alive(e).
//   * world:get_local_transform(e) / :set_local_transform(e, table)
//     where the table mirrors LocalTransform.value as
//       { position = Vec3f, rotation = { x, y, z, w }, scale = Vec3f }.
//   * cd.register_handler(name, fn) / cd.unregister_handler(name, id)
//     plus the C++ side `fire_event(name)` that runs every registered
//     handler in FIFO order. Type errors (wrong arg type, missing global,
//     etc.) return nil + an error string from C++-side helpers; they do
//     NOT raise a Lua error.
//
// Sandbox guarantee: register_bindings() installs everything under a
// single `cd` global table — no other global names are added. Each
// Engine owns its own dispatcher state.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/script/Engine.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace cd::ecs { class World; }

namespace cd::script
{

/// Opaque per-Engine state for event handlers. One instance is owned by
/// `register_bindings` and lives until the Engine is destroyed.
class BindingState;

/// Install the Phase-467 binding surface (cd.* table + math/ecs/scene
/// types + event-handler API) into `engine`. Calling more than once on
/// the same Engine is a no-op after the first call.
///
/// Returns nullptr when the engine is not `valid()`. The returned
/// `BindingState*` is non-owning — `engine` owns the storage and frees
/// it on destruction.
[[nodiscard]] BindingState* register_bindings(Engine& engine);

/// Fire every handler registered for `event_name`, in the order they
/// were registered. No-op when `state` is null or no handlers exist.
/// Returns the number of handlers that ran successfully (Lua-side
/// errors are captured into engine.last_error() and counted as failed).
std::uint32_t fire_event(BindingState* state, std::string_view event_name);

/// Number of currently-registered handlers for the given event.
[[nodiscard]] std::uint32_t handler_count(const BindingState* state,
                                          std::string_view event_name);

/// Bind a `cd::ecs::World*` into the script under the global name
/// `global_name` (typically "world"). The Lua side will see a userdata
/// with the World metatable and can call :create() / :destroy() / etc.
///
/// The Engine does NOT take ownership — the caller must keep the World
/// alive for the duration of every script call that references it.
/// Returns false on null state / invalid engine.
bool bind_world(BindingState* state, cd::ecs::World* world,
                std::string_view global_name = "world");

}  // namespace cd::script
