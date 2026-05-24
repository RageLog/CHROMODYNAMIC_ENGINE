// =============================================================================
// CHROMODYNAMIC — samples/hello_command_palette
//
// Headless showcase of the editor's fuzzy command palette. Registers
// 30+ commands that mirror what the real editor will eventually surface
// (file ops, transform, viewport, debug overlays), then runs several
// queries against the registry and prints the ranked top-5 results
// for each so you can see the FZF-style scoring in action without
// having to launch the GUI.
//
// Wires:
//   cd::editor::CommandPalette  — Phase 31 fuzzy registry.
//
// No GUI, no input — just registry + filter + ranked print.
// =============================================================================
#include <cd/editor/CommandPalette.hpp>

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace
{

struct Cmd
{
    std::uint32_t id;
    const char*   label;
};

constexpr Cmd kCmds[] = {
    {  1, "File: New Scene"                },
    {  2, "File: Open Scene..."            },
    {  3, "File: Save"                     },
    {  4, "File: Save As..."               },
    {  5, "File: Save All"                 },
    {  6, "File: Reload"                   },
    {  7, "File: Exit"                     },
    {  8, "Edit: Undo"                     },
    {  9, "Edit: Redo"                     },
    { 10, "Edit: Cut"                      },
    { 11, "Edit: Copy"                     },
    { 12, "Edit: Paste"                    },
    { 13, "Edit: Duplicate"                },
    { 14, "Edit: Delete"                   },
    { 15, "Edit: Find"                     },
    { 16, "Transform: Translate"           },
    { 17, "Transform: Rotate"              },
    { 18, "Transform: Scale"               },
    { 19, "Transform: Reset"               },
    { 20, "Transform: Align To View"       },
    { 21, "View: Top"                      },
    { 22, "View: Front"                    },
    { 23, "View: Side"                     },
    { 24, "View: Perspective"              },
    { 25, "View: Toggle Wireframe"         },
    { 26, "Render: Reload Shaders"         },
    { 27, "Render: Capture RenderDoc"      },
    { 28, "Debug: Toggle Bounding Boxes"   },
    { 29, "Debug: Toggle Profiler"         },
    { 30, "Debug: Print Scene Stats"       },
    { 31, "Window: Hierarchy"              },
    { 32, "Window: Inspector"              },
    { 33, "Window: Console"                },
    { 34, "Help: About"                    },
};

void run_query(const cd::editor::CommandPalette& palette,
               std::string_view query)
{
    const std::vector<std::size_t> hits = palette.filter(std::string { query });
    std::printf("\n  query \"%.*s\"  ->  %zu match(es)",
                static_cast<int>(query.size()), query.data(), hits.size());
    if (hits.empty()) { std::printf("\n"); return; }
    std::printf(" (top 5):\n");
    const std::size_t shown = (hits.size() < 5u) ? hits.size() : 5u;
    for (std::size_t i = 0; i < shown; ++i)
    {
        const auto& entry = palette.at(hits[i]);
        std::printf("    %zu.  [#%u]  %s\n", i + 1, entry.id, entry.label.c_str());
    }
}

}  // namespace

int main()
{
    std::printf("=== hello_command_palette — fuzzy command-palette filter showcase ===\n");

    cd::editor::CommandPalette palette;
    int invoke_count = 0;
    for (const auto& c : kCmds)
    {
        palette.register_command(c.id, c.label,
            [&invoke_count]() { ++invoke_count; });
    }
    std::printf("registered %zu commands\n", palette.size());

    // Empty query returns everything (in registration order).
    {
        const auto all = palette.filter("");
        std::printf("\n  query \"\"  ->  %zu (returns all in registration order)\n",
                    all.size());
    }

    // The interesting queries: showcase FZF scoring.
    run_query(palette, "save");                 // multiple "Save" hits, exact wins
    run_query(palette, "trs");                  // subsequence: Transform -> ... -> Scale/Reset
    run_query(palette, "tog");                  // matches both "Toggle" entries
    run_query(palette, "view");                 // boundary boost on "View:"
    run_query(palette, "rndshd");               // dense subsequence -> "Reload Shaders"
    run_query(palette, "rdc");                  // -> "Capture RenderDoc"
    run_query(palette, "xyz_no_match");

    // Fire the top match of "save" so the action callback wakes up.
    const auto top = palette.filter("save");
    if (!top.empty())
    {
        const bool ok = palette.invoke(top.front());
        std::printf("\ninvoked top \"save\" entry: ok=%s, invoke_count=%d\n",
                    ok ? "true" : "false", invoke_count);
    }

    std::printf("[hello_command_palette] OK\n");
    return 0;
}
