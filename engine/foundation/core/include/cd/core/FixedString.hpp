// =============================================================================
// CHROMODYNAMIC — cd/core/FixedString.hpp
// Phase 22.E / Wave 186 — fixed-capacity, non-allocating string.
//
// Owns N bytes inline; never heap-allocates. Used in compile-time
// label / asset-tag contexts where std::string's allocator dependency
// is undesirable (constexpr code, tight loops, no-allocate budgets).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <array>
#include <cstddef>
#include <cstring>
#include <string_view>

namespace cd::core
{

template <std::size_t N>
class FixedString
{
public:
    FixedString() = default;

    FixedString(std::string_view sv) noexcept
    {
        assign(sv);
    }

    void assign(std::string_view sv) noexcept
    {
        const std::size_t n = sv.size() < N - 1 ? sv.size() : N - 1;
        for (std::size_t i = 0; i < n; ++i) buf_[i] = sv[i];
        buf_[n] = '\0';
        size_ = n;
    }

    [[nodiscard]] const char* c_str() const noexcept { return buf_.data(); }
    [[nodiscard]] std::string_view view() const noexcept
    {
        return std::string_view { buf_.data(), size_ };
    }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return N - 1; }
    void clear() noexcept { buf_[0] = '\0'; size_ = 0; }

    bool operator==(const FixedString& o) const noexcept
    {
        return view() == o.view();
    }

private:
    std::array<char, N> buf_ { '\0' };
    std::size_t size_ { 0 };
};

}  // namespace cd::core
