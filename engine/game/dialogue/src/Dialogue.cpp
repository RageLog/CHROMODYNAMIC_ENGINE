// =============================================================================
// CHROMODYNAMIC - cd/game/dialogue/Dialogue.cpp
// Phase 484 - cd::game::dialogue (G4.1: Branching Dialogue VM)
//
// Implementation notes:
//
// * load_tree validates the graph in one pass and either commits the whole
//   tree or rolls back to empty. Forward references in `next_node` are
//   permitted (resolved at select_choice time) - that mirrors how Ink and
//   Yarn handle "knot stubs" during incremental authoring.
//
// * select_choice resolves the active choice by id (linear scan -- choice
//   lists are tiny by construction), optionally re-runs its Condition, then
//   looks up the target node by hash. Any breakage between the choice's
//   advertised `next_node` and the tree's actual node set yields a
//   `kBrokenLink` tag rather than a crash, matching the brief's "unknown
//   choice id = no-op" tolerance to authoring drift.
//
// * The parse_tree DSL is intentionally minimal: NODE / TEXT / CHOICE / END.
//   Conditions are NOT expressible in the DSL by design (predicates are code,
//   not strings); callers walk the parsed tree and bind Condition lambdas to
//   the choices that need them. That keeps the parser side-effect-free and
//   the tree fully round-trippable through plain text.
// =============================================================================
#include <cd/game/dialogue/Dialogue.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace cd::game::dialogue
{

// =============================================================================
// DialogueVM - lookup helpers
// =============================================================================

const DialogueNode* DialogueVM::find_node(const std::string& id) const noexcept
{
    const auto it = index_.find(id);
    if (it == index_.end())
    {
        return nullptr;
    }
    return &nodes_.at(it->second);
}

const DialogueChoice* DialogueVM::find_choice(const DialogueNode& node,
                                              std::string_view    id) const noexcept
{
    for (const auto& c : node.choices)
    {
        if (c.id == id)
        {
            return &c;
        }
    }
    return nullptr;
}

// =============================================================================
// DialogueVM - load_tree
// =============================================================================

LoadResult DialogueVM::load_tree(std::vector<DialogueNode> nodes, std::string start_id)
{
    // Always reset to empty first so a failed load leaves no half-state.
    nodes_.clear();
    index_.clear();
    start_id_.clear();
    current_id_.clear();

    if (nodes.empty())
    {
        return LoadResult::kEmptyTree;
    }

    // Pre-build the id index and validate uniqueness / non-emptiness as we go.
    std::unordered_map<std::string, std::size_t> new_index;
    new_index.reserve(nodes.size());

    for (std::size_t i = 0; i < nodes.size(); ++i)
    {
        const DialogueNode& node = nodes[i];

        if (node.id.empty())
        {
            return LoadResult::kEmptyNodeId;
        }
        const auto [it, inserted] = new_index.emplace(node.id, i);
        (void)it;
        if (!inserted)
        {
            return LoadResult::kDuplicateNodeId;
        }

        // Validate choice ids are unique within this node and non-empty.
        // Linear-scan dedup: choice lists are tiny (UI rarely shows >4-6).
        for (std::size_t a = 0; a < node.choices.size(); ++a)
        {
            if (node.choices[a].id.empty())
            {
                return LoadResult::kEmptyChoiceId;
            }
            for (std::size_t b = a + 1; b < node.choices.size(); ++b)
            {
                if (node.choices[a].id == node.choices[b].id)
                {
                    return LoadResult::kDuplicateChoiceId;
                }
            }
        }
    }

    // Resolve the start node: explicit override, or first entry by default.
    std::string resolved_start = !start_id.empty() ? std::move(start_id) : nodes.front().id;
    if (new_index.find(resolved_start) == new_index.end())
    {
        return LoadResult::kMissingStartNode;
    }

    // Commit.
    nodes_       = std::move(nodes);
    index_       = std::move(new_index);
    start_id_    = std::move(resolved_start);
    current_id_  = start_id_;
    return LoadResult::kOk;
}

// =============================================================================
// DialogueVM - queries
// =============================================================================

const DialogueNode* DialogueVM::current_node() const noexcept
{
    if (current_id_.empty())
    {
        return nullptr;
    }
    return find_node(current_id_);
}

std::string_view DialogueVM::current_speaker() const noexcept
{
    const auto* n = current_node();
    if (n == nullptr)
    {
        return {};
    }
    return n->speaker;
}

bool DialogueVM::is_at_end() const noexcept
{
    const auto* n = current_node();
    return n != nullptr && n->choices.empty();
}

std::vector<const DialogueChoice*>
DialogueVM::available_choices(const Blackboard& bb) const
{
    std::vector<const DialogueChoice*> out;
    const auto* n = current_node();
    if (n == nullptr)
    {
        return out;
    }
    out.reserve(n->choices.size());
    for (const auto& c : n->choices)
    {
        // Empty Condition = always visible (Ink/Yarn parity).
        if (!c.condition || c.condition(bb))
        {
            out.push_back(&c);
        }
    }
    return out;
}

std::vector<const DialogueChoice*> DialogueVM::all_choices() const
{
    std::vector<const DialogueChoice*> out;
    const auto* n = current_node();
    if (n == nullptr)
    {
        return out;
    }
    out.reserve(n->choices.size());
    for (const auto& c : n->choices)
    {
        out.push_back(&c);
    }
    return out;
}

// =============================================================================
// DialogueVM - mutations
// =============================================================================

SelectResult DialogueVM::select_choice(std::string_view choice_id, const Blackboard* bb)
{
    if (current_id_.empty() || nodes_.empty())
    {
        return SelectResult::kNotStarted;
    }
    const auto* n = current_node();
    if (n == nullptr)
    {
        // Should not happen post-load but guard anyway -- defensive.
        return SelectResult::kNotStarted;
    }
    if (n->choices.empty())
    {
        return SelectResult::kAtEnd;
    }

    const DialogueChoice* picked = find_choice(*n, choice_id);
    if (picked == nullptr)
    {
        return SelectResult::kNoOp;
    }

    // Optional predicate re-check.  When bb is null we skip -- the UI may have
    // already filtered against `available_choices` and re-running the predicate
    // would force the caller to keep the blackboard around for every click.
    if (bb != nullptr && picked->condition && !picked->condition(*bb))
    {
        return SelectResult::kConditionFailed;
    }

    // Resolve next_node.  Empty next_node intentionally terminates the
    // conversation: the choice stays valid but takes us to no successor.
    if (picked->next_node.empty())
    {
        // Treat as terminal: clear current so subsequent calls report kAtEnd
        // via the kAtEnd path (current_node() will then return nullptr).
        current_id_.clear();
        return SelectResult::kAdvanced;
    }

    const auto it = index_.find(picked->next_node);
    if (it == index_.end())
    {
        return SelectResult::kBrokenLink;
    }
    current_id_ = picked->next_node;
    return SelectResult::kAdvanced;
}

void DialogueVM::reset() noexcept
{
    if (nodes_.empty())
    {
        return;
    }
    current_id_ = start_id_;
}

// =============================================================================
// parse_tree - tiny line-oriented DSL
// =============================================================================

namespace
{

// Trim ASCII whitespace from both ends of a string_view.
std::string_view trim(std::string_view s) noexcept
{
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && (std::isspace(static_cast<unsigned char>(s[a])) != 0))
    {
        ++a;
    }
    while (b > a && (std::isspace(static_cast<unsigned char>(s[b - 1])) != 0))
    {
        --b;
    }
    return s.substr(a, b - a);
}

// Strip an inline '#' comment (everything from the first unquoted '#' onward).
// The DSL has no string-literal grammar, so the '#' is always a comment
// marker -- callers needing a literal '#' put it inside TEXT bodies after a
// non-'#' first character, which the grammar permits trivially.
std::string_view strip_comment(std::string_view s) noexcept
{
    const auto pos = s.find('#');
    if (pos == std::string_view::npos)
    {
        return s;
    }
    return s.substr(0, pos);
}

// Case-insensitive prefix match for the keyword token at the start of `s`.
// On success returns the remainder after the keyword + at least one space.
// On failure returns nullopt-equivalent (empty string_view + false).
struct KwMatch
{
    bool             ok {false};
    std::string_view rest {};
};

KwMatch match_kw(std::string_view s, std::string_view kw) noexcept
{
    if (s.size() < kw.size())
    {
        return {};
    }
    for (std::size_t i = 0; i < kw.size(); ++i)
    {
        const auto a = static_cast<char>(std::toupper(static_cast<unsigned char>(s[i])));
        const auto b = static_cast<char>(std::toupper(static_cast<unsigned char>(kw[i])));
        if (a != b)
        {
            return {};
        }
    }
    if (s.size() == kw.size())
    {
        return {true, std::string_view {}};
    }
    if (std::isspace(static_cast<unsigned char>(s[kw.size()])) == 0)
    {
        return {};  // kw must be followed by whitespace or end-of-line
    }
    return {true, trim(s.substr(kw.size()))};
}

}  // namespace

std::variant<ParsedTree, ParseError> parse_tree(std::string_view source)
{
    ParsedTree result;

    DialogueNode current_node;
    bool         have_current = false;
    std::size_t  line_no      = 0;

    auto commit_node = [&]() {
        if (have_current)
        {
            result.nodes.push_back(std::move(current_node));
            current_node  = DialogueNode {};
            have_current  = false;
        }
    };

    std::size_t pos = 0;
    while (pos <= source.size())
    {
        // Slice one logical line (delimited by '\n' or end-of-source).
        const std::size_t nl  = source.find('\n', pos);
        const std::size_t end = (nl == std::string_view::npos) ? source.size() : nl;
        std::string_view  raw = source.substr(pos, end - pos);

        // Strip CR for CRLF sources; trim + drop '#' comment.
        if (!raw.empty() && raw.back() == '\r')
        {
            raw.remove_suffix(1);
        }
        ++line_no;

        const std::string_view content = trim(strip_comment(raw));

        // Loop bookkeeping: advance past this line for next iteration. We do
        // it up here so the early-continues below stay clean.
        if (nl == std::string_view::npos)
        {
            pos = source.size() + 1;  // forces loop exit after processing
        }
        else
        {
            pos = nl + 1;
        }

        if (content.empty())
        {
            continue;
        }

        // ---- NODE <id> [SPEAKER <name>] ----------------------------------
        if (auto m = match_kw(content, "NODE"); m.ok)
        {
            commit_node();
            std::string_view rest = m.rest;
            if (rest.empty())
            {
                return ParseError {line_no, "NODE requires an id"};
            }
            // Split id and optional SPEAKER suffix.
            const auto sp = rest.find_first_of(" \t");
            std::string id;
            std::string speaker;
            if (sp == std::string_view::npos)
            {
                id.assign(rest);
            }
            else
            {
                id.assign(rest.substr(0, sp));
                const std::string_view after = trim(rest.substr(sp));
                if (auto sm = match_kw(after, "SPEAKER"); sm.ok)
                {
                    if (sm.rest.empty())
                    {
                        return ParseError {line_no, "SPEAKER requires a name"};
                    }
                    speaker.assign(sm.rest);
                }
                else if (!after.empty())
                {
                    return ParseError {line_no, "expected SPEAKER after NODE id"};
                }
            }
            current_node    = DialogueNode {};
            current_node.id = std::move(id);
            current_node.speaker = std::move(speaker);
            have_current    = true;
            continue;
        }

        // ---- TEXT <body...> ----------------------------------------------
        if (auto m = match_kw(content, "TEXT"); m.ok)
        {
            if (!have_current)
            {
                return ParseError {line_no, "TEXT outside of NODE"};
            }
            if (!current_node.text.empty())
            {
                current_node.text.push_back('\n');
            }
            current_node.text.append(m.rest);
            continue;
        }

        // ---- CHOICE <id> -> <next> : <prompt> ---------------------------
        if (auto m = match_kw(content, "CHOICE"); m.ok)
        {
            if (!have_current)
            {
                return ParseError {line_no, "CHOICE outside of NODE"};
            }
            // Grammar: <id> -> <next> : <prompt>
            const auto arrow = m.rest.find("->");
            const auto colon = m.rest.find(':');
            if (arrow == std::string_view::npos || colon == std::string_view::npos
                || colon < arrow)
            {
                return ParseError {line_no,
                                   "CHOICE requires '<id> -> <next> : <prompt>'"};
            }
            DialogueChoice ch;
            ch.id.assign(trim(m.rest.substr(0, arrow)));
            ch.next_node.assign(trim(m.rest.substr(arrow + 2, colon - (arrow + 2))));
            ch.text.assign(trim(m.rest.substr(colon + 1)));
            if (ch.id.empty())
            {
                return ParseError {line_no, "CHOICE id empty"};
            }
            current_node.choices.push_back(std::move(ch));
            continue;
        }

        // ---- END ---------------------------------------------------------
        if (auto m = match_kw(content, "END"); m.ok)
        {
            if (!m.rest.empty())
            {
                return ParseError {line_no, "END takes no arguments"};
            }
            commit_node();
            continue;
        }

        // Anything else is a syntax error -- DSL is intentionally strict so
        // typos surface immediately rather than silently becoming TEXT.
        return ParseError {line_no, "unrecognised statement"};
    }

    commit_node();
    return result;
}

}  // namespace cd::game::dialogue
