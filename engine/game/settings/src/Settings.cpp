// =============================================================================
// CHROMODYNAMIC — cd/game/settings/Settings.cpp
// Phase 471 — cd::game::settings::Settings implementation.
//
// Disk format reminder (kept stable for Phase G1):
//   * One key=value per line.
//   * '#' starts a line comment; preserved verbatim across save.
//   * Blank lines preserved verbatim across save.
//   * Whitespace around '=' is trimmed on read; emitted as `key=value`
//     (no padding) on write — deliberately minimal so diffs are clean.
//   * Value parser is greedy + heuristic:
//       "true"/"false" (case-sensitive)               -> bool
//       integer-only digits (with optional '-' sign)  -> int
//       digits with a '.' or scientific 'e'           -> float
//       anything else                                 -> string
//     This matches the lowest-common-denominator INI semantics seen in
//     UE GameUserSettings's per-section helpers and is intentionally
//     conservative — string-typed values are NEVER ambiguous because
//     strings are *whatever's left*; quoted strings would add a parser
//     escaping layer we don't need at the G1 contract.
// =============================================================================
#include <cd/game/settings/Settings.hpp>

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace cd::game::settings
{

namespace
{

// -----------------------------------------------------------------------------
// Trim ASCII whitespace from both ends of a string_view. Returns a view
// into the input — caller is responsible for the underlying storage.
// -----------------------------------------------------------------------------
[[nodiscard]] std::string_view trim(std::string_view s) noexcept
{
    const auto is_ws = [](char c) noexcept {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    while (!s.empty() && is_ws(s.front())) s.remove_prefix(1);
    while (!s.empty() && is_ws(s.back()))  s.remove_suffix(1);
    return s;
}

// -----------------------------------------------------------------------------
// Is the trimmed token a signed-integer literal? (No leading '+', no
// underscores, no hex prefix — the on-disk format is intentionally
// boring.)
// -----------------------------------------------------------------------------
[[nodiscard]] bool looks_like_int(std::string_view s) noexcept
{
    if (s.empty()) return false;
    std::size_t i = 0;
    if (s[0] == '-')
    {
        if (s.size() == 1) return false;
        i = 1;
    }
    for (; i < s.size(); ++i)
    {
        if (s[i] < '0' || s[i] > '9') return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
// Is the trimmed token a floating-point literal? Accepts `1.0`, `-2.5`,
// `1e3`, `2.7e-4`. Rejects strings without either a '.' or an 'e' so a
// pure integer is parsed as int (the looks_like_int check is run
// first).
// -----------------------------------------------------------------------------
[[nodiscard]] bool looks_like_float(std::string_view s) noexcept
{
    if (s.empty()) return false;
    bool has_dot       = false;
    bool has_exp       = false;
    bool has_any_digit = false;
    std::size_t i = 0;
    if (s[0] == '-' || s[0] == '+') i = 1;
    if (i >= s.size()) return false;
    for (; i < s.size(); ++i)
    {
        const char c = s[i];
        if (c >= '0' && c <= '9')
        {
            has_any_digit = true;
        }
        else if (c == '.')
        {
            if (has_dot || has_exp) return false;
            has_dot = true;
        }
        else if (c == 'e' || c == 'E')
        {
            if (has_exp || !has_any_digit) return false;
            has_exp = true;
            if (i + 1 < s.size() && (s[i + 1] == '+' || s[i + 1] == '-'))
            {
                ++i;
            }
        }
        else
        {
            return false;
        }
    }
    return has_any_digit && (has_dot || has_exp);
}

}  // namespace

// -----------------------------------------------------------------------------
// parse_value — type-deduce from textual representation. The order is
// load-bearing: bool first (cheapest reject), int next, then float, then
// fall through to string. See the comment block at the top of the file
// for the contract this enforces.
// -----------------------------------------------------------------------------
Value Settings::parse_value(std::string_view raw)
{
    const auto t = trim(raw);

    if (t == "true")  return Value{ true };
    if (t == "false") return Value{ false };

    if (looks_like_int(t))
    {
        int      out  = 0;
        const auto* first = t.data();
        const auto* last  = t.data() + t.size();
        const auto  res   = std::from_chars(first, last, out);
        if (res.ec == std::errc{} && res.ptr == last)
        {
            return Value{ out };
        }
        // Overflow / partial parse falls through to string so the user's
        // raw value is preserved verbatim and the type-error is surfaced
        // on get<int>() returning nullopt.
    }

    if (looks_like_float(t))
    {
        // C++23: std::from_chars for float is fully supported on MSVC
        // (VS 2019 16.4+) and Clang (14+). Use it for locale-independent,
        // round-trip-correct parsing — avoids istringstream overhead and
        // the prior risk of locale drift from the classic imbue approach.
        float           f     = 0.0F;
        const auto*     first = t.data();
        const auto*     last  = t.data() + t.size();
        const auto      res   = std::from_chars(first, last, f);
        if (res.ec == std::errc{} && res.ptr == last)
        {
            return Value{ f };
        }
        // Overflow / underflow / partial parse falls through to string so
        // the raw token is preserved and the error surfaces at get<float>().
    }

    return Value{ std::string{ t } };
}

// -----------------------------------------------------------------------------
// format_value — textual rendering for save(). Bool emits "true"/"false",
// numerics use the default streaming format under the C locale, strings
// emit verbatim (no quoting — see load-side rationale).
// -----------------------------------------------------------------------------
std::string Settings::format_value(const Value& v)
{
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    std::visit([&oss](const auto& alt) {
        using T = std::decay_t<decltype(alt)>;
        if constexpr (std::is_same_v<T, bool>)
        {
            oss << (alt ? "true" : "false");
        }
        else if constexpr (std::is_same_v<T, float>)
        {
            // max_digits10 (9 for float) guarantees round-trip precision:
            // parse_value( format_value( v ) ) == v exactly.
            oss << std::setprecision(std::numeric_limits<float>::max_digits10)
                << alt;
        }
        else if constexpr (std::is_same_v<T, std::string>)
        {
            oss << alt;
        }
        else
        {
            // int (and any future numeric alternative).
            oss << alt;
        }
    }, v);
    return oss.str();
}

// -----------------------------------------------------------------------------
// load — clear current state, then walk the file once. Layout (comments
// + blank lines + key order) is captured into layout_ so save() can
// round-trip user-authored config files without reordering.
// -----------------------------------------------------------------------------
bool Settings::load(const std::string& path)
{
    std::ifstream in{ path };
    if (!in)
    {
        return false;
    }

    values_.clear();
    layout_.clear();

    std::string line;
    while (std::getline(in, line))
    {
        // Strip a trailing '\r' if the file was authored on Windows and
        // we're reading on POSIX (or vice versa). std::getline already
        // ate the '\n' delimiter.
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        const auto trimmed = trim(line);

        if (trimmed.empty())
        {
            layout_.push_back(PreservedLine{ LineKind::kBlank, {} });
            continue;
        }
        if (trimmed.front() == '#')
        {
            // Store the original (untrimmed) line so leading indentation in
            // multi-line comment blocks is preserved across save.
            layout_.push_back(PreservedLine{ LineKind::kComment, line });
            continue;
        }

        const auto eq = trimmed.find('=');
        if (eq == std::string_view::npos)
        {
            // Malformed line — preserve it as a comment-equivalent so we
            // don't silently drop user content. (A stricter parser
            // returning false here is a deliberate Phase G5 candidate
            // when schema-versioning is introduced.)
            layout_.push_back(PreservedLine{ LineKind::kComment, line });
            continue;
        }

        const auto key_v = trim(trimmed.substr(0, eq));
        const auto val_v = trim(trimmed.substr(eq + 1));
        std::string key_str{ key_v };

        values_.insert_or_assign(key_str, parse_value(val_v));
        layout_.push_back(PreservedLine{ LineKind::kKey, key_str });

        // Fire observers for the loaded value so subsystems that were
        // configured before load() pick up the on-disk override
        // immediately. broadcast() is keyed on the canonical std::string.
        auto it = values_.find(key_str);
        if (it != values_.end())
        {
            broadcast(it->first, it->second);
        }
    }

    return true;
}

// -----------------------------------------------------------------------------
// save — emit preserved layout first, then any keys set after the most
// recent load that don't yet appear in layout_. The append phase
// ensures `Settings{}; set("foo", 1); save("p")` produces `foo=1\n`
// even though layout_ was empty.
// -----------------------------------------------------------------------------
bool Settings::save(const std::string& path) const
{
    std::ofstream out{ path, std::ios::binary };
    if (!out)
    {
        return false;
    }

    // 1. Emit layout lines in order. For kKey lines, look up the current
    //    value (which may have been mutated since load).
    for (const auto& line : layout_)
    {
        switch (line.kind)
        {
            case LineKind::kBlank:
            {
                out << '\n';
                break;
            }
            case LineKind::kComment:
            {
                out << line.text << '\n';
                break;
            }
            case LineKind::kKey:
            {
                const auto it = values_.find(line.text);
                if (it == values_.end())
                {
                    // Key removed between load and save — emit as a
                    // commented-out line so user-visible history is
                    // preserved without changing the active config.
                    out << "# (removed) " << line.text << '\n';
                }
                else
                {
                    out << line.text << '=' << format_value(it->second) << '\n';
                }
                break;
            }
        }
    }

    // 2. Append keys that weren't in the original layout (or were added
    //    by post-load set() calls). Unordered_map iteration is
    //    deterministic enough for the test suite's purposes here —
    //    callers needing stable ordering should set keys at construction
    //    time (which lands them in layout_ on the first save+load roundtrip).
    for (const auto& [k, v] : values_)
    {
        const bool already_emitted =
            std::ranges::any_of(layout_,
                                [&k](const PreservedLine& l) {
                                    return l.kind == LineKind::kKey && l.text == k;
                                });
        if (!already_emitted)
        {
            out << k << '=' << format_value(v) << '\n';
        }
    }

    return static_cast<bool>(out);
}

// -----------------------------------------------------------------------------
// on_change — append the callback under the key. Returns a monotonically-
// increasing SubscriptionId that can be passed to off_change() later.
// Null callbacks are rejected (return 0).
// -----------------------------------------------------------------------------
SubscriptionId Settings::on_change(std::string_view key, ChangeCallback callback)
{
    if (!callback)
    {
        return 0;
    }
    const SubscriptionId id = next_id_++;
    std::string key_str{ key };
    observers_[key_str].push_back(ObserverSlot{ id, std::move(callback) });
    id_to_key_.emplace(id, std::move(key_str));
    return id;
}

// -----------------------------------------------------------------------------
// off_change — remove the observer with the given id. Uses id_to_key_ for
// O(1) bucket lookup then erases the matching slot in O(n_observers_per_key).
// Safe to call with an unknown or already-removed id (no-op).
// -----------------------------------------------------------------------------
void Settings::off_change(SubscriptionId id) noexcept
{
    const auto key_it = id_to_key_.find(id);
    if (key_it == id_to_key_.end())
    {
        return;
    }
    const std::string& key = key_it->second;
    const auto obs_it = observers_.find(key);
    if (obs_it != observers_.end())
    {
        auto& slots = obs_it->second;
        slots.erase(std::ranges::remove_if(slots,
                        [id](const ObserverSlot& s) { return s.id == id; })
                        .begin(),
                    slots.end());
    }
    id_to_key_.erase(key_it);
}

void Settings::broadcast(const std::string& key, const Value& new_value)
{
    const auto it = observers_.find(key);
    if (it == observers_.end())
    {
        return;
    }
    // Copy the slot list before iterating — a callback may call off_change()
    // (one-shot pattern) which would mutate observers_ mid-iteration.
    const auto slots = it->second;
    for (const auto& slot : slots)
    {
        if (slot.callback) slot.callback(new_value);
    }
}

bool Settings::has_key(std::string_view key) const noexcept
{
    return values_.contains(std::string{ key });
}

std::size_t Settings::observer_count(std::string_view key) const noexcept
{
    const auto it = observers_.find(std::string{ key });
    return it == observers_.end() ? 0U : it->second.size();
}

void Settings::clear() noexcept
{
    values_.clear();
    observers_.clear();
    id_to_key_.clear();
    layout_.clear();
    next_id_ = 1;
}

}  // namespace cd::game::settings
