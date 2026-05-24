// =============================================================================
// CHROMODYNAMIC — cd/core/StringSplit.hpp
// Phase 41.B / Wave 209 — string_view splitting helpers.
//
// CSV parser, command-palette argument tokenizer, asset URI path
// component walker — all of them want "split this view at every
// delimiter and call me with each non-empty (or all) part." The C++
// standard library has `<ranges>::split_view` but it's awkward enough
// that hand-rolled split functions dominate every C++ codebase.
//
// `split(view, delim, fn)` invokes `fn(part)` for each delimited part,
// including empty parts when the caller wants them. `split_nonempty`
// is the same with empty parts filtered out.
//
// No allocation — strict string_view slices into the caller's buffer.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstddef>
#include <string_view>

namespace cd::core
{

template <class F>
inline void split(std::string_view src, char delim, F&& fn)
{
    std::size_t start = 0;
    for (std::size_t i = 0; i < src.size(); ++i)
    {
        if (src[i] == delim)
        {
            fn(src.substr(start, i - start));
            start = i + 1;
        }
    }
    fn(src.substr(start));
}

template <class F>
inline void split_nonempty(std::string_view src, char delim, F&& fn)
{
    split(src, delim, [&](std::string_view part)
    {
        if (!part.empty()) fn(part);
    });
}

[[nodiscard]] inline std::size_t count_parts(std::string_view src, char delim) noexcept
{
    if (src.empty()) return 0;
    std::size_t n = 1;
    for (char c : src) if (c == delim) ++n;
    return n;
}

}  // namespace cd::core
