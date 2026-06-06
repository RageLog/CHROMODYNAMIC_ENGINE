# cd::game::dialog_tree

BG3-style **branching dialog graph** runtime. A designer authors a
`DialogTree` (POD: `tree_id` + `root_id` + `vector<DialogNode>`) and
the `DialogTreeRuntime` walks it driven by player choices and named
boolean condition variables — without recompiling.

The runtime is a pure state machine: `cd::core` is the only
dependency. Audio / UI / animation effects are wired by the caller
through node IDs (`Say` payload), never inside this library.

## Sibling — not duplicate — of `cd::game::dialogue`

| Library                  | Pattern              | Origin    |
|--------------------------|----------------------|-----------|
| `cd::game::dialogue`     | Ink / Yarn linear chain + predicate-gated choices + DSL | Phase 484 / G4.1 |
| `cd::game::dialog_tree`  | BG3 branching graph with NodeKind routing + transparent `kCondition` chaining + bool condition store | Phase 645 |

They coexist. A title that wants Ink-style linear authoring uses
`dialogue`; a title that wants BG3 branching uses `dialog_tree`.
Neither is a v2 of the other.

## Node kinds

```cpp
enum class NodeKind : uint8_t
{
    kSay,        // surface text to the player; payload = line ID
    kChoice,     // surface player choice options; payload = list of (id, text)
    kCondition,  // route based on a named bool; transparent (no UI surface)
    kEnd,        // terminate the conversation
};
```

`kCondition` is **transparent** — it does not surface anything to
the player. The runtime evaluates the condition, picks the matching
branch, and recurses into the next node without an extra tick. Means
a designer can author cascading conditions (`has_quest_A && rep_high`)
as chained `kCondition` nodes without UI thrash.

## Public surface

```cpp
namespace cd::game::dialog_tree {

struct DialogNode
{
    uint32_t     id;
    NodeKind     kind;
    std::string  payload;           // Say: line id; Choice: serialised options
    std::string  condition_var;     // kCondition only
    uint32_t     next_if_true;      // kCondition only
    uint32_t     next_if_false;     // kCondition only
    std::vector<uint32_t> children; // kSay / kChoice
};

struct DialogTree
{
    std::string             tree_id;
    uint32_t                root_id;
    std::vector<DialogNode> nodes;
};

class DialogTreeRuntime
{
public:
    void                                play(DialogTree);
    void                                stop();
    void                                set_condition(std::string name, bool value);
    void                                choose(uint32_t child_index);   // for kChoice
    void                                advance();                      // for kSay
    [[nodiscard]] const DialogNode*     current() const noexcept;
    [[nodiscard]] bool                  finished() const noexcept;
};

}
```

## Lifecycle

```
  play()       — load tree, position cursor at root, transparently
                 chase kCondition nodes until a UI-relevant node.
  choose(i)    — only valid when current()->kind == kChoice; jumps to
                 children[i] and chases kCondition.
  advance()    — only valid when current()->kind == kSay; jumps to
                 children[0] (linear continuation) and chases kCondition.
  set_condition — caller updates the named bool store before play()
                 or at any point in between; the runtime reads it on
                 every kCondition encounter.
  finished()   — true after the cursor lands on kEnd.
```

## Dependencies

Only `cd::core`. No allocator, no math, no I/O. Designer authoring is
done in a separate tool (or hand-edited JSON via an external
`cd::asset_json` adapter); the runtime is hot-reload-friendly because
`play()` resets all state.
