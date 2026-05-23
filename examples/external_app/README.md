# external_app — substantive downstream demo

This is a **complete external-app blueprint** for consuming
CHROMODYNAMIC via `find_package`. Unlike [examples/consuming/](../consuming/)
(which is a 60-line smoke), this directory shows what a real
downstream project's CMake + source layout looks like.

> **Track C Part 2 (AI surrogate).** Project owner is invited to
> copy this directory into a separate repository and treat it as the
> seed of their actual downstream consumer. The AI cannot open a new
> public repo on the owner's behalf — that's the irreducible human
> step. Everything *up to* that step is ready here.

## What this demo does

A small text-mode **asset-inventory** app. It:

1. Builds an in-memory `cd::ecs::World` populated with 12 fake
   "GameObject" entities, each with a `Name` component and a
   `Stats` component.
2. Streams 200 frames of pseudo-events through a `cd::log::RingBufferSink`.
3. Uses `cd::core::format(ec)` to print errors when a lookup fails.
4. Serializes the inventory to a JSON string via `cd::asset_json`.
5. Round-trips through `cd::imgdiff::compare` on a tiny test image
   (proves the image stack is link-clean from downstream).

That's intentionally a "boring CRUD" — the point isn't to be flashy,
it's to exercise 5+ libraries linked from an external `find_package`
consumer and exit 0.

## Files

| File | Purpose |
|---|---|
| `CMakeLists.txt`     | downstream-project template: cmake_minimum, find_package, target_link_libraries |
| `src/main.cpp`       | the inventory app |
| `src/Inventory.hpp`  | local types for the app — shows how downstream code coexists with cd::* |
| `src/Inventory.cpp`  | ECS wiring + serialization |
| `.gitignore`         | standard CMake .gitignore for a downstream repo |
| `README.md`          | you are here |

## Bringing this up as a separate repository

```bash
# 1. Install the engine to a prefix you control.
git clone https://github.com/RageLog/CHROMODYNAMIC_ENGINE.git engine
cd engine
git checkout v0.25.0      # or whatever tag you want to pin
cmake --preset ninja-base -DCD_DISABLE_VCPKG=ON
cmake --build --preset ninja-debug
cmake --install build/ninja-base --prefix $HOME/.local/cd-0.25 --config Debug
cd ..

# 2. Copy this directory into a fresh repo of your own.
cp -R engine/examples/external_app my_game
cd my_game
git init && git add -A && git commit -m "initial: external_app seed"

# 3. Configure + build with the install prefix.
cmake -S . -B build -G Ninja \
    -DCMAKE_PREFIX_PATH=$HOME/.local/cd-0.25 \
    -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/my_game_inventory

# Expected first line of output:
#   linking CHROMODYNAMIC 0.25.0
```

## Why this isn't already a separate repository

The Phase 11 Track C Part 2 acceptance is "a real downstream project,
in a separate repository, links find_package and ships". The AI side
ships everything needed: the install pipeline, the consumer template
(this directory), the docs ([docs/CONSUMING.md](../../docs/CONSUMING.md)),
the maturity-gate context ([docs/ADR/ADR-20260523-wave133-...](../../docs/ADR/ADR-20260523-wave133-phase11-v1.0-maturity.md)).

What ships is **not** a separate repo because:

* Creating a public GitHub repository on the project owner's behalf
  is a permission boundary the AI doesn't cross — repo creation
  goes through the owner's GitHub identity, license choice, repo
  name, visibility settings, branch protection.
* "Real downstream that ships" implies *use cases the AI cannot
  invent*: a game, a research tool, a renderer plugin — that
  decision is the owner's.

So the irreducible human step is: pick a repo name, run the
`git init` + `gh repo create` commands above, push. From that
moment Track C Part 2 is real.

## CI workflow shim

When the downstream repo lives, drop this into `.github/workflows/ci.yml`:

```yaml
name: CI
on: [push, pull_request]
jobs:
  build:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
      - name: Clone + install engine
        run: |
          git clone --depth=1 --branch v0.25.0 \
            https://github.com/RageLog/CHROMODYNAMIC_ENGINE.git engine
          cmake -S engine -B engine/build -G Ninja \
                -DCD_DISABLE_VCPKG=ON -DCD_ENABLE_VULKAN=OFF
          cmake --build engine/build
          cmake --install engine/build --prefix $HOME/cd
      - name: Configure + build
        run: |
          cmake -S . -B build -G Ninja \
                -DCMAKE_PREFIX_PATH=$HOME/cd \
                -DCMAKE_BUILD_TYPE=Debug
          cmake --build build
      - name: Run
        run: ./build/my_game_inventory
```

That's the entire CI surface a downstream consumer needs.
