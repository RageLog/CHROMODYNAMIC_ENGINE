// =============================================================================
// CHROMODYNAMIC — cd/vfs/PathUtil.hpp
// ADR-017 P4 (Sprint S2.9) — VFS path normalisation utilities.
//
// normalize_path:
//   * Converts backslashes to forward slashes.
//   * Collapses duplicate separators.
//   * Resolves "." segments.
//   * Resolves ".." segments (clamped at root — no escape above "").
//   * Strips leading and trailing separators from the result.
//
// Examples:
//   "shaders/../textures/a.png"  ->  "textures/a.png"
//   "a/./b//c"                   ->  "a/b/c"
//   "/leading/slash"             ->  "leading/slash"
//   "trailing/"                  ->  "trailing"
//   "../escape"                  ->  "escape"    (clamped)
// =============================================================================
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace cd::vfs
{

/// Normalise a VFS-style path. Always returns a forward-slash-separated,
/// root-relative string with no leading/trailing separators and no
/// redundant `.` / `..` segments. Caller-owned std::string returned.
[[nodiscard]] inline std::string normalize_path(std::string_view raw)
{
    // --- tokenise on both / and \ ------------------------------------------
    std::vector<std::string_view> segments;
    segments.reserve(8);

    std::size_t i = 0;
    while (i < raw.size())
    {
        // skip separators
        while (i < raw.size() && (raw[i] == '/' || raw[i] == '\\'))
            ++i;

        const std::size_t start = i;
        while (i < raw.size() && raw[i] != '/' && raw[i] != '\\')
            ++i;

        if (i == start)
            break;

        std::string_view seg { raw.data() + start, i - start };

        if (seg == ".")
        {
            // skip
        }
        else if (seg == "..")
        {
            if (!segments.empty())
                segments.pop_back();
            // else: clamped at root, discard
        }
        else
        {
            segments.push_back(seg);
        }
    }

    // --- join --------------------------------------------------------------
    std::string out;
    out.reserve(raw.size());
    for (std::size_t k = 0; k < segments.size(); ++k)
    {
        if (k != 0)
            out += '/';
        out += segments[k];
    }
    return out;
}

}  // namespace cd::vfs
