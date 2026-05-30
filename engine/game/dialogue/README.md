# cd::game_dialogue

## Purpose
Branching dialogue virtual machine for narrative-driven gameplay. Models a
dialogue as a directed graph of `DialogueNode`s keyed by stable string ids,
with predicate-gated `DialogueChoice` edges. The runtime walks the graph one
node at a time, exposes the visible choices given a typed `Blackboard`, and
advances on a chosen choice.

Phase 484 / G4.1 in the engine roadmap (Phase-G gameplay tier).

## Namespace
`cd::game::dialogue`

## Public headers
- `include/cd/game/dialogue/Dialogue.hpp` — `Blackboard`, `Condition`,
  `DialogueChoice`, `DialogueNode`, `LoadResult` / `SelectResult` status
  enums, `DialogueVM`, plus the data-driven `parse_tree` DSL helper
  (`ParsedTree` / `ParseError`).

## Primary types
| Type             | Role                                                                   |
|------------------|------------------------------------------------------------------------|
| `Blackboard`     | `string -> variant<bool,int,float,string>` typed scratch store.        |
| `Condition`      | `std::function<bool(const Blackboard&)>` choice-visibility predicate.  |
| `DialogueChoice` | One option on a node: `id`, `text`, `condition`, `next_node`.          |
| `DialogueNode`   | One graph node: `id`, `speaker`, `text`, `choices`.                    |
| `DialogueVM`     | Owns a tree; tracks current node; commits choices to advance.          |
| `LoadResult`     | `kOk` / `kEmptyTree` / `kDuplicateNodeId` / ... validation tags.       |
| `SelectResult`   | `kAdvanced` / `kNoOp` / `kConditionFailed` / `kAtEnd` / `kBrokenLink`. |
| `parse_tree`     | Tiny line-oriented DSL parser (NODE / TEXT / CHOICE / END).            |

## Semantics
- **Predicate-gated visibility**: `available_choices(bb)` returns only the
  choices whose `Condition` accepts `bb` (or has no condition). The unfiltered
  set is available via `all_choices()` for editor / debug panels.
- **Terminal nodes**: a node with an empty `choices` vector is terminal;
  `is_at_end()` reports it and further `select_choice` calls return `kAtEnd`.
- **Empty `next_node`**: selecting a choice whose `next_node` is empty
  terminates the conversation cleanly (`kAdvanced` + `current_node()` becomes
  `nullptr`). This lets authors mark a choice as "end the conversation"
  without authoring a sentinel node.
- **Tolerant of authoring drift**: unknown choice id returns `kNoOp`, broken
  `next_node` returns `kBrokenLink`, neither crashes nor mutates state.
- **Reset semantics**: `reset()` jumps current_node back to the configured
  start; the tree itself stays loaded.
- **VM independence**: multiple `DialogueVM` instances each own their tree
  and state; ticking one never disturbs another.
- **DSL is opt-in**: predicates are *code*, not strings. `parse_tree` is for
  the unconditional-flow case; callers wire `Condition` lambdas onto the
  parsed nodes afterwards.

## Usage example
```cpp
#include <cd/game/dialogue/Dialogue.hpp>

using namespace cd::game::dialogue;

std::vector<DialogueNode> nodes;
{
    DialogueNode n;
    n.id      = "intro";
    n.speaker = "Guide";
    n.text    = "What do you do?";
    DialogueChoice ask;
    ask.id        = "ask";
    ask.text      = "Ask about the rumour.";
    ask.next_node = "rumour";
    DialogueChoice secret;
    secret.id        = "secret";
    secret.text      = "Tell me the secret password.";
    secret.next_node = "treasure";
    secret.condition = [](const Blackboard& bb) {
        return bb.get_bool("knows_password");
    };
    n.choices.push_back(std::move(ask));
    n.choices.push_back(std::move(secret));
    nodes.push_back(std::move(n));
}
nodes.push_back(DialogueNode {"rumour",   "Guide", "A dragon lives east.", {}});
nodes.push_back(DialogueNode {"treasure", "Guide", "The vault is under the well.", {}});

DialogueVM vm;
vm.load_tree(std::move(nodes));

Blackboard bb;
bb.set_bool("knows_password", true);

for (const auto* c : vm.available_choices(bb))
{
    // ... render c->text in UI ...
}
vm.select_choice("secret", bb);   // -> kAdvanced, current_node() = "treasure"
```

## Build / Test
```bash
cmake --build --preset ninja-debug --target cd_game_dialogue
ctest --preset ninja-debug -R game_dialogue --output-on-failure
```

## Dependencies
- `cd::core` — `Defines.hpp` (header-level dependency only). No allocator,
  no math, no I/O beyond the in-memory DSL parser.

## References
- Ingold, Joseph. *Ink: A Narrative Scripting Language for Games*, inkle
  Ltd. (<https://www.inklestudios.com/ink/>), accessed 2026.
- Secret Lab. *Yarn Spinner Specification* v2.4
  (<https://docs.yarnspinner.dev/getting-started/writing-in-yarn>),
  accessed 2026.
- Mateas, Michael & Stern, Andrew. *Facade: An Experiment in Building a
  Fully-Realized Interactive Drama.* Game Developers Conference, 2003.
- Crawford, Chris. *Chris Crawford on Interactive Storytelling.* New
  Riders, 2nd ed., 2012 — story-graph design vocabulary.

## Notes
- Single translation unit (`src/Dialogue.cpp`) carries the VM implementation
  and the line-oriented DSL parser.
- Thread-safety: not thread-safe by design. One VM per owning thread.
- The DSL deliberately has no condition syntax: predicates are code, not
  strings. Callers post-process the parsed tree to bind `condition` lambdas.
- Future work (out of scope for G4.1): localisation hooks, scripted side
  effects on selection, save/restore of `current_node_id` for persistent
  conversations, JSON / YAML loaders, voice-line ids.
