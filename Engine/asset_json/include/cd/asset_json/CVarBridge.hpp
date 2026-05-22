// =============================================================================
// CHROMODYNAMIC — cd/asset_json/CVarBridge.hpp
//
// JSON ↔ cd::core::CVarRegistry adapter. Keeps the JSON format human-
// readable: the registry serializes as a top-level object whose keys are
// CVar names and whose values are JSON-native scalars.
//
//   { "render.vsync": true,
//     "render.msaa": 4,
//     "audio.gain": 0.85,
//     "user.locale": "en-US" }
//
// Type mapping:
//   CVarValue bool        ↔ JSON bool
//   CVarValue int64_t     ↔ JSON number (integer fast-path emission)
//   CVarValue double      ↔ JSON number
//   CVarValue string      ↔ JSON string
//
// Round-trip caveat:
//   JSON has only one number type (double in our AST), so a freshly
//   loaded i64 vs double cannot be distinguished from the JSON alone.
//   When the registry already has a CVar with a known type, `from_json`
//   coerces the JSON number into THAT type. For unknown keys, JSON
//   numbers become `double` and the consumer can cast.
// =============================================================================
#pragma once

#include <cd/asset_json/Json.hpp>
#include <cd/core/CVar.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <ios>
#include <string>
#include <utility>

namespace cd::asset_json
{

/// Serialize every CVar in `registry` into a JSON object Value.
/// Keys are emitted in lexicographic order (deterministic).
[[nodiscard]] inline Value to_json(const cd::core::CVarRegistry& registry)
{
    Object obj;
    auto entries = registry.snapshot();
    // The registry's snapshot is implementation-defined order; sort here
    // so the JSON is git-friendly.
    std::sort(
        entries.begin(),
        entries.end(),
        [](const auto& a, const auto& b)
        {
            return a.first < b.first;
        }
    );
    for (const auto& [key, value] : entries)
    {
        std::visit(
            [&](const auto& v)
            {
                using V = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<V, bool>)
                {
                    obj[key] = Value { v };
                }
                else if constexpr (std::is_same_v<V, std::int64_t>)
                {
                    obj[key] = Value { static_cast<double>(v) };
                }
                else if constexpr (std::is_same_v<V, double>)
                {
                    obj[key] = Value { v };
                }
                else if constexpr (std::is_same_v<V, std::string>)
                {
                    obj[key] = Value { v };
                }
            },
            value
        );
    }
    return Value { std::move(obj) };
}

/// Apply a JSON object Value to `registry`. Each key updates (or
/// creates) a CVar. If the registry already has the key with a typed
/// CVar, the JSON value is coerced into that type. Returns kTypeMismatch
/// if the root Value is not an object.
[[nodiscard]] inline cd::core::Result<void> from_json(const Value& json_root, cd::core::CVarRegistry& registry)
{
    if (!json_root.is_object())
    {
        return std::unexpected(json_errors::make(json_errors::Code::kTypeMismatch, "from_json: root is not an object"));
    }
    for (const auto& [key, val] : json_root.as_object())
    {
        // If the CVar exists, coerce into its current type. Otherwise
        // pick a sensible default per JSON kind.
        auto existing = registry.get(key);
        if (val.is_bool())
        {
            registry.set(key, cd::core::CVarValue { val.as_bool() });
        }
        else if (val.is_number())
        {
            const double d = val.as_number();
            if (existing.has_value() && std::holds_alternative<std::int64_t>(*existing))
            {
                registry.set(key, cd::core::CVarValue { static_cast<std::int64_t>(d) });
            }
            else
            {
                registry.set(key, cd::core::CVarValue { d });
            }
        }
        else if (val.is_string())
        {
            registry.set(key, cd::core::CVarValue { val.as_string() });
        }
        else if (val.is_null())
        {
            // Treat null as "leave unchanged" — keep existing CVar, no-op.
            continue;
        }
        else
        {
            // Arrays / nested objects are not CVar-shaped; bail.
            return std::unexpected(
                json_errors::make(
                    json_errors::Code::kTypeMismatch,
                    std::string { "from_json: value at '" } + key + "' is not a scalar (bool/number/string/null)"
                )
            );
        }
    }
    return {};
}

/// Convenience: parse a JSON file from disk and apply it to `registry`.
/// Equivalent to `from_json(parse(file_text), registry)` but propagates
/// the read / parse error categories cleanly.
[[nodiscard]] inline cd::core::Result<void>
load_into(std::string_view path, cd::core::CVarRegistry& registry, std::uint32_t max_depth = 64)
{
    auto v = load(path, max_depth);
    if (!v.has_value())
        return std::unexpected(v.error());
    return from_json(*v, registry);
}

/// Convenience: serialize `registry` to a JSON file on disk.
[[nodiscard]] inline cd::core::Result<void>
save_to(std::string_view path, const cd::core::CVarRegistry& registry, bool pretty = true)
{
    const auto v = to_json(registry);
    const auto text = serialize(v, pretty);
    const std::string path_s { path };
    std::ofstream f { path_s, std::ios::binary | std::ios::trunc };
    if (!f)
        return std::unexpected(
            json_errors::make(json_errors::Code::kIoError, std::string { "save_to: cannot open " } + path_s)
        );
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!f)
        return std::unexpected(
            json_errors::make(json_errors::Code::kIoError, std::string { "save_to: write failed for " } + path_s)
        );
    return {};
}

}  // namespace cd::asset_json
