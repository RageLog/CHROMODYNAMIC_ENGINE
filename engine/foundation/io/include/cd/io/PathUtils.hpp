// =============================================================================
// CHROMODYNAMIC — cd/io/PathUtils.hpp
// Phase 60.B / Wave 228 — string_view path manipulation helpers.
//
// VFS / asset / scene paths are normalized forward-slash strings:
//   "foo/bar/baz.txt", "shaders/standard/standard.vert"
//
// This header provides zero-allocation accessors over a `string_view`:
//   * `extension(path)`  — ".txt" or "" (no extension).
//   * `stem(path)`       — "baz" (file name without extension).
//   * `parent(path)`     — "foo/bar" (everything before the last /).
//   * `filename(path)`   — "baz.txt".
//   * `normalize(path)`  — canonical form: backslash→slash, collapse "//"
//                          and ".", resolve ".." segments, strip trailing
//                          slash. Returns owned std::string.
//   * `join(a, b)`       — join two path segments with exactly one '/'.
//                          Returns owned std::string.
//
// Accessors (extension/stem/parent/filename) slice the caller's buffer;
// no allocation, no copies. normalize() and join() return std::string.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace cd::io
{

[[nodiscard]] inline std::string_view filename(std::string_view path) noexcept
{
    const auto pos = path.find_last_of('/');
    return (pos == std::string_view::npos) ? path : path.substr(pos + 1);
}

[[nodiscard]] inline std::string_view extension(std::string_view path) noexcept
{
    const auto fn = filename(path);
    const auto dot = fn.find_last_of('.');
    if (dot == std::string_view::npos || dot == 0) return {};
    return fn.substr(dot);
}

[[nodiscard]] inline std::string_view stem(std::string_view path) noexcept
{
    const auto fn = filename(path);
    const auto dot = fn.find_last_of('.');
    if (dot == std::string_view::npos || dot == 0) return fn;
    return fn.substr(0, dot);
}

[[nodiscard]] inline std::string_view parent(std::string_view path) noexcept
{
    const auto pos = path.find_last_of('/');
    return (pos == std::string_view::npos) ? std::string_view {} : path.substr(0, pos);
}

/// Normalize a path to canonical form:
///   * Replace every '\\' with '/'.
///   * Collapse consecutive '/' into one.
///   * Remove '.' segments.
///   * Resolve '..' segments (consuming the preceding real segment; does not
///     travel above the root — extra ".." at the top are ignored).
///   * Strip trailing '/' (root "/" stays as "/").
///
/// Examples:
///   normalize("a/b/../c")         → "a/c"
///   normalize("a//b/./c/")        → "a/b/c"
///   normalize("../../foo")        → "foo"   (cannot go above root)
///   normalize("")                  → ""
[[nodiscard]] inline std::string normalize(std::string_view path)
{
    if (path.empty())
        return {};

    // 1. Replace backslashes, and tokenize on '/'.
    std::vector<std::string_view> segments;
    segments.reserve(8);

    // Walk the (possibly backslash-containing) input.
    // We build a unified view by scanning manually.
    bool rooted = (!path.empty() && (path[0] == '/' || path[0] == '\\'));
    std::size_t i = 0;
    while (i < path.size())
    {
        // skip separators
        while (i < path.size() && (path[i] == '/' || path[i] == '\\'))
            ++i;
        if (i >= path.size())
            break;
        // find end of segment
        std::size_t j = i;
        while (j < path.size() && path[j] != '/' && path[j] != '\\')
            ++j;
        const auto seg = path.substr(i, j - i);
        i = j;
        if (seg == ".")
            continue;
        if (seg == "..")
        {
            if (!segments.empty())
                segments.pop_back();
            // else: ignore — cannot go above root
            continue;
        }
        segments.push_back(seg);
    }

    std::string out;
    out.reserve(path.size());
    if (rooted)
        out += '/';
    for (std::size_t k = 0; k < segments.size(); ++k)
    {
        if (k > 0)
            out += '/';
        out += segments[k];
    }
    return out;
}

/// Join two path segments with exactly one '/'.
/// If `b` is empty the result is `a` (normalized).
/// If `a` is empty the result is `b` (normalized).
[[nodiscard]] inline std::string join(std::string_view a, std::string_view b)
{
    if (a.empty())
        return normalize(b);
    if (b.empty())
        return normalize(a);

    std::string out;
    out.reserve(a.size() + 1 + b.size());
    out.append(a);
    // Ensure exactly one separator between a and b.
    const bool a_sep = (a.back() == '/' || a.back() == '\\');
    const bool b_sep = (!b.empty() && (b.front() == '/' || b.front() == '\\'));
    if (!a_sep && !b_sep)
        out += '/';
    else if (a_sep && b_sep)
        out.pop_back();  // drop the trailing sep from a; b starts with one
    out.append(b);
    return normalize(out);
}

}  // namespace cd::io
