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
        // std::from_chars for float is C++17/20 in spec but library
        // support is uneven across GCC <11; fall back to std::stof if
        // from_chars is unavailable at the chosen toolchain. We use
        // istringstream as a portable middle-ground that respects the
        // 'C' locale we've already imbued globally in cd::core boot.
        std::istringstream iss{ std::string{ t } };
        iss.imbue(std::locale::classic());
        float f = 0.0F;
        iss >> f;
        if (iss && iss.eof())
        {
            return Value{ f };
        }
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
        else if constexpr (std::is_same_v<T, std::string>)
        {
            oss << alt;
        }
        else
        {
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
            std::any_of(layout_.begin(), layout_.end(),
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
// on_change — append the callback under the key. The current
// implementation does not deduplicate registrations and returns a
// monotonically-increasing id so callers can identify their subscription
// in future off_change calls (deferred).
// -----------------------------------------------------------------------------
SubscriptionId Settings::on_change(std::string_view key, ChangeCallback callback)
{
    if (!callback)
    {
        return 0;
    }
    observers_[std::string{ key }].push_back(std::move(callback));
    return next_id_++;
}

void Settings::broadcast(const std::string& key, const Value& new_value)
{
    const auto it = observers_.find(key);
    if (it == observers_.end())
    {
        return;
    }
    // Copy the list before iterating — callbacks may legally mutate
    // observers_ (e.g. a one-shot subscriber that unregisters itself in
    // a future off_change). Copying keeps iteration safe.
    const auto callbacks = it->second;
    for (const auto& cb : callbacks)
    {
        if (cb) cb(new_value);
    }
}

bool Settings::has_key(std::string_view key) const noexcept
{
    return values_.find(std::string{ key }) != values_.end();
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
    layout_.clear();
    next_id_ = 1;
}

}  // namespace cd::game::settings
