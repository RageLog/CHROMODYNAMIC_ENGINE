// =============================================================================
// CHROMODYNAMIC — cd/game/settings/Settings.hpp
// Phase 471 — cd::game::settings::Settings (ADR-20260530 G1.4)
//
// Persistent user-preferences container with three responsibilities:
//
//   1. Typed key/value storage. Values are a small `std::variant` over
//      { bool, int, float, std::string } so the typical "graphics /
//      audio / controls / accessibility" surface of an engine is
//      expressible without per-section bespoke containers (cf. UE
//      `GameUserSettings`, Unity `PlayerPrefs`, Godot `ConfigFile`).
//
//   2. Disk persistence in a simple human-editable INI-flavoured format:
//      one `key=value` line per entry, '#' line comments preserved
//      verbatim across save, blank lines preserved. The on-disk format is
//      intentionally **trivial** (no sections, no nesting) — this is the
//      Phase G1 contract; the Phase G5 migration revision will introduce
//      schema versioning + TOML upgrade (see ADR §2.2 G-09 and §3.7).
//
//   3. Live observation. Subscribers register a callback per key; the
//      callback fires exactly once per *changed* value — `set()` with
//      the previous value is a no-op for observers (matches the
//      Cinemachine / Unity Settings rationale: a frame-by-frame
//      "graphics quality" slider should not re-allocate render targets
//      every tick if the slider stops on the same value). Comparison is
//      `==` on the variant; type-equal + value-equal both required.
//
// Threading: not thread-safe. The library is intended to be driven from
// the engine main thread that owns the settings UI; worker threads
// observing settings should marshal change events through the engine's
// event bus instead of subscribing directly. The library does not embed
// a mutex specifically so that the cost of `get()` (the dominant
// operation by far in production) stays at one unordered_map lookup +
// one variant-get.
//
// Dependencies (CLAUDE.md §7): cd::core only at the header level.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

namespace cd::game::settings
{

// -----------------------------------------------------------------------------
// Value — the four canonical leaf types of a user preference.
//
// Order matters: index 0 = bool, 1 = int, 2 = float, 3 = std::string. The
// loader uses the variant index to pick a parser; the saver picks a
// formatter. Adding a new alternative is a one-line change in three
// places (this typedef + the parser dispatch in Settings::load + the
// formatter dispatch in Settings::save) but is a *schema-breaking*
// change for existing config files and must be matched by a
// migration entry (Phase G5).
// -----------------------------------------------------------------------------
using Value = std::variant<bool, int, float, std::string>;

// -----------------------------------------------------------------------------
// ChangeCallback — fired when an observed key's stored value transitions
// from one value to another. The new value is passed by const reference;
// the previous value is NOT passed (subscribers that need it should
// capture it from their last invocation — keeps the callback surface
// minimal and matches Unity/UE settings observer ergonomics).
// -----------------------------------------------------------------------------
using ChangeCallback = std::function<void(const Value& new_value)>;

// -----------------------------------------------------------------------------
// SubscriptionId — opaque handle returned by `on_change`. Currently only
// used as a stable identifier so subscribers can be associated with their
// owning subsystem in diagnostics; a future `off_change(id)` overload
// would consume it. Kept opaque so the storage can evolve without
// breaking the public API.
// -----------------------------------------------------------------------------
using SubscriptionId = std::uint64_t;

// -----------------------------------------------------------------------------
// Settings — the container.
//
// All operations are O(1) average (hash map). `set<T>` and `on_change`
// allocate; `get<T>` does not. `load` and `save` are linear in the
// number of stored keys.
// -----------------------------------------------------------------------------
class Settings
{
public:
    Settings()  = default;
    ~Settings() = default;

    Settings(const Settings&)            = delete;
    Settings& operator=(const Settings&) = delete;
    Settings(Settings&&)                 = default;
    Settings& operator=(Settings&&)      = default;

    // -------------------------------------------------------------------------
    // Typed accessors.
    //
    // `get<T>(key)` returns the stored value cast to T iff:
    //   (a) the key exists, and
    //   (b) the stored variant alternative is T.
    // Otherwise returns std::nullopt. This is intentional: callers asking
    // for `get<int>("user.name")` (where "user.name" is a string) get
    // nullopt rather than a silent zero, mirroring the contract of
    // std::get_if. Numeric coercion (int -> float, etc.) is NOT done
    // here; it would mask user-config typos.
    // -------------------------------------------------------------------------
    template <typename T>
    [[nodiscard]] std::optional<T> get(std::string_view key) const
    {
        static_assert(std::is_same_v<T, bool>
                   || std::is_same_v<T, int>
                   || std::is_same_v<T, float>
                   || std::is_same_v<T, std::string>,
                      "cd::game::settings::Settings::get<T>: T must be one of "
                      "{ bool, int, float, std::string }");

        const auto it = values_.find(std::string{ key });
        if (it == values_.end())
        {
            return std::nullopt;
        }
        if (const auto* ptr = std::get_if<T>(&it->second))
        {
            return *ptr;
        }
        return std::nullopt;
    }

    // -------------------------------------------------------------------------
    // `set<T>(key, value)` — store or overwrite. If the previous value
    // was the same variant alternative AND `==` to the new value, no
    // observers fire (the contract of "live broadcast on change only").
    // If the variant alternative changes (e.g. previously int, now
    // string under the same key) observers DO fire — the change is real
    // from the subscriber's perspective.
    //
    // The first time a key is set, observers registered before the first
    // `set` will fire on the create transition (nullopt -> value).
    // -------------------------------------------------------------------------
    template <typename T>
    void set(std::string_view key, T value)
    {
        static_assert(std::is_same_v<T, bool>
                   || std::is_same_v<T, int>
                   || std::is_same_v<T, float>
                   || std::is_same_v<T, std::string>,
                      "cd::game::settings::Settings::set<T>: T must be one of "
                      "{ bool, int, float, std::string }");

        Value       new_value{ std::move(value) };
        std::string key_str{ key };

        const auto it = values_.find(key_str);
        if (it != values_.end())
        {
            if (it->second == new_value)
            {
                return;  // No-op: same alternative + same value.
            }
            it->second = std::move(new_value);
            broadcast(key_str, it->second);
            return;
        }

        auto [inserted_it, ok] = values_.emplace(std::move(key_str),
                                                 std::move(new_value));
        (void) ok;  // emplace into hash_map with a fresh key always succeeds.
        broadcast(inserted_it->first, inserted_it->second);
    }

    // -------------------------------------------------------------------------
    // Disk I/O.
    //
    // `load(path)` reads the file at `path`, parses each line, and
    // overwrites in-memory state. Returns true on success (file opened
    // and every line parsed). Comments (`#` prefix) and blank lines are
    // recorded so `save()` can emit them in the same order. Existing
    // observers fire as values are loaded — this is intentional and
    // documented: loading a config file at runtime should look
    // identical to the user clicking through the settings UI.
    //
    // `save(path)` writes the current state to `path`, preserving the
    // ordering of any comments/blanks captured by the most recent
    // `load`. Keys set via `set()` after load are appended at the end
    // in insertion order. Returns true on successful write.
    // -------------------------------------------------------------------------
    [[nodiscard]] bool load(const std::string& path);
    [[nodiscard]] bool save(const std::string& path) const;

    // -------------------------------------------------------------------------
    // `on_change(key, callback)` — register a live observer. The same
    // callback is NOT deduplicated; registering twice fires twice (this
    // matches Qt signal/slot and Unity UnityEvent semantics — caller
    // owns dedup if needed). Returns an opaque SubscriptionId; the
    // current implementation does not yet expose `off_change`, but the
    // id is stable across the Settings' lifetime so it may be stored
    // alongside the subsystem owning the subscription for future cleanup.
    // -------------------------------------------------------------------------
    SubscriptionId on_change(std::string_view key, ChangeCallback callback);

    // -------------------------------------------------------------------------
    // Diagnostics / introspection — not on the hot path; used by the
    // settings UI and tests.
    // -------------------------------------------------------------------------
    [[nodiscard]] std::size_t key_count()        const noexcept { return values_.size(); }
    [[nodiscard]] std::size_t observer_count(std::string_view key) const noexcept;
    [[nodiscard]] bool        has_key(std::string_view key)        const noexcept;

    // -------------------------------------------------------------------------
    // Drop every stored value, every preserved comment, and every
    // observer. Used by tests and by the settings-UI "reset to defaults"
    // path (after which the caller would set() the defaults explicitly).
    // -------------------------------------------------------------------------
    void clear() noexcept;

private:
    // -------------------------------------------------------------------------
    // PreservedLine — the loader records the on-disk *layout* so save()
    // can emit comments and blank lines back in the same place. Three
    // kinds:
    //   * kComment — a '#' line, the raw text is stored verbatim
    //     (including the leading '#').
    //   * kBlank   — an empty line.
    //   * kKey     — a key/value line; the key references values_ for
    //     the current value at save time (NOT the value at load time —
    //     this is what makes "load, mutate, save" round-trip correctly).
    // -------------------------------------------------------------------------
    enum class LineKind : std::uint8_t
    {
        kComment = 0,
        kBlank   = 1,
        kKey     = 2,
    };

    struct PreservedLine
    {
        LineKind    kind;
        std::string text;  ///< For kComment: raw line. For kKey: the key name.
    };

    // -------------------------------------------------------------------------
    // Notify every observer registered on `key`. Walk a *copy* of the
    // observer list — a callback may re-enter set() (e.g. resolution
    // change observer also normalises window position) and we must not
    // invalidate the vector mid-iteration.
    // -------------------------------------------------------------------------
    void broadcast(const std::string& key, const Value& new_value);

    // -------------------------------------------------------------------------
    // Format/parse helpers — small enough to live in the .cpp; declared
    // here only because the parser feeds load() directly.
    // -------------------------------------------------------------------------
    static Value          parse_value(std::string_view raw);
    static std::string    format_value(const Value& v);

    std::unordered_map<std::string, Value>                          values_     {};
    std::unordered_map<std::string, std::vector<ChangeCallback>>    observers_  {};
    std::vector<PreservedLine>                                      layout_     {};
    SubscriptionId                                                  next_id_    { 1 };
};

}  // namespace cd::game::settings
