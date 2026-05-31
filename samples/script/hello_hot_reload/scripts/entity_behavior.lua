-- =============================================================================
-- CHROMODYNAMIC — samples/script/hello_hot_reload/scripts/entity_behavior.lua
-- Phase 534 — Initial entity behavior: move along the X axis.
--
-- HOW HOT-RELOAD WORKS
-- --------------------
-- While hello_hot_reload is running, edit and save this file.  The C++ side
-- uses cd::game::asset_hot_reload::HotReloadBus to watch the file path.
-- Within the 100 ms throttle window after your save the bus fires the reload
-- callback, which calls cd::script::Engine::run_string() on the new file
-- contents.  Because Lua global assignments overwrite the previous value,
-- the new on_tick definition replaces the old one atomically — the next
-- frame calls the edited version.
--
-- TRY THESE EDITS
-- ---------------
--  1. Change speed (kSpeed) to a different value and save → watch the log
--     show a faster or slower x advance.
--  2. Change AXIS to "z" and save → the entity will start moving along Z
--     instead of X from its current position.
--  3. Switch to circular motion by replacing the body of on_tick with:
--       local t = world:get_local_transform(the_entity)
--       local angle = (t.position.x / kRadius)  -- use x as phase proxy
--       t.position.x = kRadius * math.cos(angle + kSpeed * dt)
--       t.position.z = kRadius * math.sin(angle + kSpeed * dt)
--       world:set_local_transform(the_entity, t)
--     and save.
--
-- INTERFACE CONTRACT (do not change the function signature)
-- ---------------------------------------------------------
-- on_tick(dt) is called by C++ each frame via
--   eng.call_global_numeric("on_tick", {kDt}, out, 0)
-- The global "the_entity" holds the entity table { id, generation }.
-- The global "world" exposes the cd::ecs::World binding so this script can
-- read/write components via world:get_local_transform() / set_local_transform().
-- =============================================================================

-- Movement speed in world units per second along the X axis.
local kSpeed = 1.0

-- Axis to move along: "x", "y", or "z".
local AXIS = "x"

--- on_tick(dt)
--- Called once per simulated frame by C++.
--- @param dt number  Timestep in seconds (typically 1/60).
function on_tick(dt)
    -- Retrieve the current transform from the ECS world.
    local t = world:get_local_transform(the_entity)
    if t == nil then
        -- Entity does not yet have a LocalTransform; nothing to update.
        return
    end

    -- Advance the position along the chosen axis.
    if AXIS == "x" then
        t.position.x = t.position.x + kSpeed * dt
    elseif AXIS == "y" then
        t.position.y = t.position.y + kSpeed * dt
    else
        t.position.z = t.position.z + kSpeed * dt
    end

    -- Write the mutated transform back to the ECS world.
    world:set_local_transform(the_entity, t)
end
