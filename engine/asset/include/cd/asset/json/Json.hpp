// =============================================================================
// CHROMODYNAMIC — cd/asset/json/Json.hpp
//
// Tiny hand-rolled JSON parser. The shape mirrors json.org §1:
//   value  := null | true | false | number | string | array | object
//   array  := '[' value (',' value)* ']'
//   object := '{' string ':' value (',' string ':' value)* '}'
//
// Out of scope (v1):
//   * Streaming / SAX-style parsing
//   * Schema / typed reflection
//   * Comments (JSON5)
//   * NaN / Infinity (per RFC 8259)
//   * Duplicate-key policy beyond "last wins"
//   * Number precision beyond IEEE 754 double (i64 promoted to double
//     when stored, so > 2^53 loses precision — we flag this as a
//     known limitation, not a bug)
//
// The AST is `cd::asset::json::Value` — a sum type implemented as a
// std::variant. Accessor convenience: `is_object()`, `as_string()`,
// `at("key")`, `at(idx)`. Mutation lives on the variant directly.
//
// Why hand-roll instead of nlohmann_json:
//   * The engine's policy (CLAUDE.md §6) limits transitive deps; nlohmann
//     is already pulled by tinygltf but `cd::asset_gltf` doesn't expose
//     it. We don't want to make `cd::asset_json` depend on
//     `cd::asset_gltf`'s internal include path.
//   * The hand-rolled parser keeps cd::asset_json's binary cost in the
//     single-digit KB range and matches the cd::asset_obj / cd::asset_cdmesh
//     style (no third-party).
//   * Performance is not a goal here — the engine's hot path doesn't
//     parse JSON; this is for config, save games, scene serialization.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace cd::asset::json
{

namespace json_errors
{
inline constexpr std::uint32_t kDomain = 0x0015;

enum class Code : std::uint32_t
{
    kOk = 0,
    kFileNotFound = 1,
    kIoError = 2,
    kUnexpectedToken = 3,
    kUnterminatedString = 4,
    kBadEscape = 5,
    kBadNumber = 6,
    kDepthLimit = 7,
    kKeyNotFound = 8,
    kTypeMismatch = 9,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}
}  // namespace json_errors

class Value;
using Array = std::vector<Value>;
using Object = std::map<std::string, Value>;  ///< ordered by key (deterministic output)

struct Null
{
    [[nodiscard]] constexpr bool operator==(const Null&) const noexcept
    {
        return true;
    }
};

class Value
{
public:
    using Storage = std::variant<Null, bool, double, std::string, Array, Object>;

    Value() noexcept = default;  // null

    // NOLINTBEGIN(google-explicit-constructor) — implicit converting ctors
    // are the DESIGN of a JSON value type (`Value v = 5;`,
    // `obj["k"] = "text"`); mirrors nlohmann::json and std::variant's
    // converting constructor.
    Value(Null) noexcept
    {
    }

    Value(bool b) noexcept
        : storage_ { b }
    {
    }

    Value(double d) noexcept
        : storage_ { d }
    {
    }

    Value(int i) noexcept
        : storage_ { static_cast<double>(i) }
    {
    }

    Value(std::int64_t i) noexcept
        : storage_ { static_cast<double>(i) }
    {
    }

    Value(const char* s)
        : storage_ { std::string { s } }
    {
    }

    Value(std::string s) noexcept
        : storage_ { std::move(s) }
    {
    }

    Value(Array a) noexcept
        : storage_ { std::move(a) }
    {
    }

    Value(Object o) noexcept
        : storage_ { std::move(o) }
    {
    }
    // NOLINTEND(google-explicit-constructor)

    [[nodiscard]] bool is_null() const noexcept
    {
        return std::holds_alternative<Null>(storage_);
    }

    [[nodiscard]] bool is_bool() const noexcept
    {
        return std::holds_alternative<bool>(storage_);
    }

    [[nodiscard]] bool is_number() const noexcept
    {
        return std::holds_alternative<double>(storage_);
    }

    [[nodiscard]] bool is_string() const noexcept
    {
        return std::holds_alternative<std::string>(storage_);
    }

    [[nodiscard]] bool is_array() const noexcept
    {
        return std::holds_alternative<Array>(storage_);
    }

    [[nodiscard]] bool is_object() const noexcept
    {
        return std::holds_alternative<Object>(storage_);
    }

    [[nodiscard]] bool as_bool() const
    {
        return std::get<bool>(storage_);
    }

    [[nodiscard]] double as_number() const
    {
        return std::get<double>(storage_);
    }

    [[nodiscard]] const std::string& as_string() const
    {
        return std::get<std::string>(storage_);
    }

    [[nodiscard]] const Array& as_array() const
    {
        return std::get<Array>(storage_);
    }

    [[nodiscard]] const Object& as_object() const
    {
        return std::get<Object>(storage_);
    }

    [[nodiscard]] Array& as_array_mut()
    {
        return std::get<Array>(storage_);
    }

    [[nodiscard]] Object& as_object_mut()
    {
        return std::get<Object>(storage_);
    }

    /// Lookup in an object. Returns kKeyNotFound if missing or kTypeMismatch
    /// if `*this` is not an object.
    [[nodiscard]] cd::core::Result<const Value*> at(std::string_view key) const;

    /// Lookup in an array. Returns kKeyNotFound for out-of-range index or
    /// kTypeMismatch if `*this` is not an array.
    [[nodiscard]] cd::core::Result<const Value*> at(std::size_t index) const;

    /// Equality is structural (variant default).
    [[nodiscard]] bool operator==(const Value&) const = default;

    [[nodiscard]] const Storage& storage() const noexcept
    {
        return storage_;
    }

private:
    Storage storage_;
};

/// Parse a JSON text. `max_depth` is a safety limit against pathological
/// inputs; defaults to 64 which is well past anything reasonable.
[[nodiscard]] cd::core::Result<Value> parse(std::string_view text, std::uint32_t max_depth = 64);

/// Load a JSON file from disk and parse.
[[nodiscard]] cd::core::Result<Value> load(std::string_view path, std::uint32_t max_depth = 64);

/// Serialize a Value back to a JSON string. `pretty=true` emits 2-space
/// indented output; `false` emits a single-line compact form.
[[nodiscard]] std::string serialize(const Value& v, bool pretty = false);

}  // namespace cd::asset::json
