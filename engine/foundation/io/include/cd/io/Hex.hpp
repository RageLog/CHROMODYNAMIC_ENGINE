// =============================================================================
// CHROMODYNAMIC — cd/io/Hex.hpp
// Phase 88.B / Wave 256 — hex string ↔ byte buffer codec.
//
// Asset hash display, packet trace dumps, debug pretty-print: all
// want "AABBCC..." lowercase hex over a byte span.
//
//   * `to_hex(bytes)` → "aabbcc..." string.
//   * `from_hex(text, out)` → bool (false on malformed input).
//
// Lowercase output; either-case accepted on input.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace cd::io
{

[[nodiscard]] inline std::string to_hex(std::span<const std::byte> bytes)
{
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (auto b : bytes)
    {
        const auto u = static_cast<std::uint8_t>(b);
        out.push_back(kDigits[(u >> 4) & 0xFu]);
        out.push_back(kDigits[u & 0xFu]);
    }
    return out;
}

[[nodiscard]] inline bool from_hex(std::string_view text, std::vector<std::byte>& out)
{
    if ((text.size() % 2) != 0) return false;
    out.clear();
    out.reserve(text.size() / 2);
    auto digit = [](char c) -> int
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
    };
    for (std::size_t i = 0; i < text.size(); i += 2)
    {
        const int hi = digit(text[i]);
        const int lo = digit(text[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(static_cast<std::byte>((hi << 4) | lo));
    }
    return true;
}

}  // namespace cd::io
