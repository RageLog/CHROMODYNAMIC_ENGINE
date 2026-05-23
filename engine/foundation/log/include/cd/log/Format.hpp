// =============================================================================
// CHROMODYNAMIC — cd/log/Format.hpp
// ADR-017 P1 (DfH ilogger.hpp formatBraces salvage, modernized)
//
// Brace-format `"{}"` placeholders + variadic to_log_string converter using
// C++23 concepts. Mirrors fmt's `"hello {}!"` syntax without an fmt dependency.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <concepts>
#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace cd::log::detail
{

template <class T>
concept has_string_method = requires(const T& v) {
    { v.string() } -> std::convertible_to<std::string>;
};

template <class T>
concept stream_insertable = requires(std::ostringstream& out, const T& v) { out << v; };

template <class T>
[[nodiscard]] inline std::string to_log_string(T&& value)
{
    using V = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<V, std::string>)
    {
        return value;
    }
    else if constexpr (std::is_same_v<V, std::string_view>)
    {
        return std::string { value };
    }
    else if constexpr (std::is_same_v<V, const char*> || std::is_same_v<V, char*>)
    {
        return value ? std::string { value } : std::string { "(null)" };
    }
    else if constexpr (std::is_same_v<V, bool>)
    {
        return value ? "true" : "false";
    }
    else if constexpr (std::is_enum_v<V>)
    {
        return std::to_string(static_cast<std::underlying_type_t<V>>(value));
    }
    else if constexpr (std::is_integral_v<V>)
    {
        return std::to_string(value);
    }
    else if constexpr (std::is_floating_point_v<V>)
    {
        std::ostringstream out;
        out << value;
        return out.str();
    }
    else if constexpr (has_string_method<V>)
    {
        return value.string();
    }
    else if constexpr (stream_insertable<V>)
    {
        std::ostringstream out;
        out << value;
        return out.str();
    }
    else
    {
        return "[unsupported]";
    }
}

template <class... Args>
[[nodiscard]] inline std::string format_braces(std::string_view format, Args&&... args)
{
    std::vector<std::string> values;
    values.reserve(sizeof...(Args));
    (values.emplace_back(to_log_string(std::forward<Args>(args))), ...);

    std::string out;
    out.reserve(format.size() + values.size() * 8);

    std::size_t arg = 0;
    bool err = false;
    for (std::size_t i = 0; i < format.size(); ++i)
    {
        const char c = format[i];
        if (c == '{')
        {
            if (i + 1 < format.size() && format[i + 1] == '{')
            {
                out.push_back('{');
                ++i;
                continue;
            }
            if (i + 1 < format.size() && format[i + 1] == '}')
            {
                if (arg < values.size())
                {
                    out += values[arg++];
                }
                else
                {
                    err = true;
                    out += "{}";
                }
                ++i;
                continue;
            }
            err = true;
            out.push_back(c);
            continue;
        }
        if (c == '}')
        {
            if (i + 1 < format.size() && format[i + 1] == '}')
            {
                out.push_back('}');
                ++i;
                continue;
            }
            err = true;
        }
        out.push_back(c);
    }
    if (arg != values.size())
    {
        err = true;
    }
    if (err)
    {
        out += " [format_error]";
    }
    return out;
}

}  // namespace cd::log::detail
