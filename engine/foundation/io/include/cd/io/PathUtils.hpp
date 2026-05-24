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
//
// All slices into the caller's buffer; no allocation, no copies.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <string_view>

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

}  // namespace cd::io
