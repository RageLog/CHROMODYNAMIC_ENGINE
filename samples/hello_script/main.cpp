// =============================================================================
// CHROMODYNAMIC — samples/hello_script
//
// Demo of cd::script::Engine. Shows bidirectional C++ ↔ Lua:
//   * C++ → Lua: set_global (player health), run_string (game logic)
//   * Lua → C++: register_function (log_damage), call_global (on_tick)
//   * C++ → Lua: get_global_number (read final score)
//
// Tiny "tick the simulation" loop: 5 frames where Lua decrements the
// player's health and the C++ side logs each damage event via a
// registered callback. After the loop, C++ reads the final score
// global and prints the verdict.
// =============================================================================
#include <cd/script/Engine.hpp>

#include <cstdio>

int main()
{
    std::printf("=== hello_script — cd::script Lua demo ===\n");

    cd::script::Engine eng;
    if (!eng.valid())
    {
        std::printf("[hello_script] Lua state allocation failed\n");
        return 1;
    }

    // Register a C++ callback exposed as a Lua global function.
    int damage_events = 0;
    eng.register_function("log_damage", [&] {
        ++damage_events;
        std::printf("  [c++] log_damage fired (event #%d)\n", damage_events);
    });

    // Seed initial state from C++.
    eng.set_global("health", 100.0);
    eng.set_global("player", std::string_view { "alice" });

    // Load the game-tick script.
    constexpr const char* kGameScript = R"(
        function on_tick(dt_ms)
            -- Lua reads the C++-provided health, drops it by 7,
            -- and signals C++ via the registered callback.
            health = health - 7
            log_damage()
        end

        function final_score()
            return health
        end
    )";

    if (auto r = eng.run_string(kGameScript); !r.has_value())
    {
        std::printf("[hello_script] script load failed: %.*s\n",
                    static_cast<int>(eng.last_error().size()),
                    eng.last_error().data());
        return 1;
    }

    // Tick 5 frames. Each tick calls on_tick() from C++.
    std::printf("\n=== Ticking 5 frames ===\n");
    for (int frame = 0; frame < 5; ++frame)
    {
        std::printf("frame %d:\n", frame);
        auto r = eng.call_global("on_tick");
        if (!r.has_value())
        {
            std::printf("[hello_script] tick failed: %.*s\n",
                        static_cast<int>(eng.last_error().size()),
                        eng.last_error().data());
            return 1;
        }
    }

    // Read the resulting health back from Lua.
    auto health = eng.get_global_number("health");
    auto name = eng.get_global_string("player");
    std::printf("\n=== Summary ===\n");
    std::printf("  player           = %s\n",
                name.has_value() ? name->c_str() : "(unknown)");
    std::printf("  final health     = %.1f\n", health.value_or(-1.0));
    std::printf("  damage_events    = %d\n", damage_events);
    std::printf("  scripts_run      = %llu\n",
                static_cast<unsigned long long>(eng.script_count()));
    std::printf("[hello_script] done\n");
    return 0;
}
