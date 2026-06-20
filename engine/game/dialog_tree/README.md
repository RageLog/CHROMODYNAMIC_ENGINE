# cd::game::dialog_tree

BG3-style **branching dialog graph** runtime. A designer authors a
`DialogTree` (POD: `tree_id` + `root_id` + `vector<DialogNode>`) and
the `DialogTreeRuntime` walks it driven by player choices and named
boolean condition variables — without recompiling.

The runtime is a pure state machine: `cd::core` is the only
dependency. Audio / UI / animation effects are wired by the caller
through node IDs (`Say` payload), never inside this library.

## Sibling — not duplicate — of `cd::game::dialogue`

| Library                 | Pattern                                                                                              | Origin           |
|-------------------------|------------------------------------------------------------------------------------------------------|------------------|
| `cd::game::dialogue`    | Ink / Yarn linear chain + predicate-gated choices + DSL                                              | Phase 484 / G4.1 |
| `cd::game::dialog_tree` | BG3 branching graph with NodeKind routing + transparent `kCondition` chaining + bool condition store | Phase 645        |

They coexist. A title that wants Ink-style linear authoring uses
`dialogue`; a title that wants BG3 branching uses `dialog_tree`.
Neither is a v2 of the other.

## Node kinds

```cpp
enum class NodeKind : uint8_t
{
    kSay,        // surface text to the player; auto-advances to next_ids[0]
    kChoice,     // surface player-choice options; advance(i) selects next_ids[i]
    kCondition,  // route based on a named bool; transparent (no UI surface)
    kEnd,        // terminate the conversation immediately
};
```

`kCondition` is **transparent** — it does not surface anything to
the player. The runtime evaluates the condition, picks the matching
branch, and recurses into the next node without an extra tick. Means
a designer can author cascading conditions (`has_quest_A && rep_high`)
as chained `kCondition` nodes without UI thrash.

Cycle protection: if a designer accidentally forms a kCondition cycle
(A->B->A), the runtime detects the depth ceiling (1024 hops) and
terminates the conversation gracefully instead of stack-overflowing.

## Data types

```cpp
namespace cd::game::dialog_tree {

// One vertex in the dialog graph.
struct DialogNode
{
    std::string              node_id       {};  // unique within the tree
    NodeKind                 kind          { NodeKind::kSay };
    std::string              text          {};  // spoken/displayed line
    std::vector<std::string> next_ids      {};  // successor node ids
    std::string              condition_var {};  // kCondition only: bool var name
};

// The complete designer-authored graph (plain data, no runtime state).
struct DialogTree
{
    std::string             tree_id {};
    std::string             root_id {};
    std::vector<DialogNode> nodes   {};
};

}
```

## Public surface

```cpp
namespace cd::game::dialog_tree {

class DialogTreeRuntime
{
public:
    // Index the graph; clears cursor + done state (NOT condition vars).
    void load(const DialogTree& tree);

    // Insert or overwrite a named boolean flag consulted by kCondition nodes.
    // May be called before or after load(), and between advances.
    void set_condition_var(std::string_view name, bool value);

    // Reset cursor to root_id. Must be called after load() and condition seeding.
    // Transparently chases kCondition nodes so current() always returns a
    // renderable node (kSay / kChoice) or nullptr.
    void start();

    // The active node, or nullptr before start() / after done.
    [[nodiscard]] const DialogNode* current() const noexcept;

    // Advance one step:
    //   kSay       : choice_index ignored; follows next_ids[0].
    //   kChoice    : selects next_ids[choice_index]; returns false when OOB.
    //   kCondition : choice_index ignored; branches on condition_var.
    //   kEnd       : no-op, returns false.
    // Returns true on successful advance, false on no-op / done / OOB index.
    [[nodiscard]] bool advance(std::size_t choice_index = 0);

    // True after kEnd or exhausted next_ids, or before start().
    [[nodiscard]] bool is_done() const noexcept;
};

}
```

## Lifecycle

```text
  load(tree)              — copy nodes, build O(1) hash index, reset cursor.
  set_condition_var(k,v)  — seed boolean flags (before or after load).
  start()                 — jump to root, chase kCondition transparently.
  current()               — read active node (kSay or kChoice only).
  advance(i)              — for kSay: i=0; for kChoice: i = player selection.
  is_done()               — true when conversation has ended.
```

## Typical game loop

```cpp
DialogTreeRuntime rt;
rt.set_condition_var("has_key",  player.has_key);
rt.set_condition_var("rep_high", faction.rep > 80);
rt.load(tree);
rt.start();

while (!rt.is_done())
{
    const DialogNode* n = rt.current();
    if (n->kind == NodeKind::kChoice)
    {
        const std::size_t pick = ui.show_choices(n->next_ids);
        rt.advance(pick);
    }
    else
    {
        ui.show_line(n->text);
        rt.advance(0);
    }
}
```

## kCondition chaining (BG3 "script check" pattern)

```text
  [quest_check]  kCondition("quest_done")  -> [rep_check] / [deny]
  [rep_check]    kCondition("rep_high")    -> [reward]    / [deny]
  [reward]       kSay  "You have earned my trust."
  [deny]         kSay  "Come back when you are worthy."
```

Both gates fire transparently in a single `start()` or `advance()` call.
The player never sees the condition nodes; they jump directly to `reward`
or `deny`.

## Edge cases

| Scenario | Behaviour |
| --- | --- |
| `condition_var` undefined in store | `false` branch taken (safe default) |
| `next_ids[1]` absent on kCondition and condition is false | conversation ends |
| Broken `next_ids` link (typo) | conversation ends gracefully |
| kChoice with empty `next_ids` | `advance(i)` always returns `false` (OOB) |
| Cyclic kCondition graph | depth ceiling fires, conversation ends |
| Duplicate `node_id` in `nodes` | last entry in vector wins (last-writer-wins) |
| Empty tree / empty `root_id` | `start()` is a no-op; `is_done()` true |

## Dependencies

Only `cd::core`. No allocator, no math, no I/O. Designer authoring is
done in a separate tool (or hand-edited JSON via an external
`cd::asset_json` adapter); the runtime is hot-reload-friendly because
`load()` resets all cursor state.
