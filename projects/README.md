# CHROMODYNAMIC Studio - Empty & Sample Projects

This directory contains clean, self-contained 2D projects authored and serialized by CHROMODYNAMIC Studio (`editor.exe`). Open any file instantly. Every project can be opened directly from the Studio Project Hub, opened from the command line via `--project`, run standalone in `cd_player.exe`, or packaged into an independent `.exe` + `.cdpak` distribution with all required runtime libraries bundled alongside it.

## Available Projects

| Folder | Project File | Description |
| :-- | :-- | :-- |
| `00_empty_project/` | `projects/00_empty_project/project.cdproj` | Clean blank 2D project (`0` entities, `0` tiles) ready for authoring from scratch. |
| `01_pong/` | `projects/01_pong/project.cdproj` | **Neon Cyber Pong**: 1P vs CPU arcade court with paddle spin deflection, accelerating plasma ball, and live point scoreboard. |
| `02_break_the_bridges/` | `projects/02_break_the_bridges/project.cdproj` | **Break the Bridges**: Tactical arcade bridge-demolition game where 3 multi-span canyon bridges and keystone piers shatter beneath marching enemy raiders. |
| `03_neon_platformer/` | `projects/03_neon_platformer/project.cdproj` | **Neon Cavern Platformer**: Side-scrolling action platformer with coyote/wall jump, 16-tile Wang auto-tiling, soft 2D shadows, and Lua 5.4 behaviors. |
| `04_topdown_rpg/` | `projects/04_topdown_rpg/project.cdproj` | **Top-Down RPG Dungeon**: Y-sorted dungeon rooms, shadow-casting torches, keys, chests, and patrol guards. |
| `05_twinstick_arena/` | `projects/05_twinstick_arena/project.cdproj` | **Twin-Stick Survival Arena**: 360-degree twin-stick shooter with pooled projectiles and spatial-hash swarm collisions. |

## Opening & Exporting from CLI

```powershell
# Open any project in CHROMODYNAMIC Studio:
.\build\ide-base\bin\editor.exe --project projects/00_empty_project/project.cdproj

# Package any project into its own standalone .exe + .cdpak bundle:
.\build\ide-base\bin\editor.exe --project projects/01_pong/project.cdproj --export-standalone exports/Pong --exe-name Pong.exe
.\build\ide-base\bin\editor.exe --project projects/02_break_the_bridges/project.cdproj --export-standalone exports/BreakTheBridges --exe-name BreakTheBridges.exe
```
