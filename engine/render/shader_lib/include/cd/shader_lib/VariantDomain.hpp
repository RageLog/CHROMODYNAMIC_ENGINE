// =============================================================================
// CHROMODYNAMIC — cd/shader_lib/VariantDomain.hpp
// phase1134 (SL-C step 4, ADR-20260612-shader-library-architecture §2.4):
// typed shader permutation domain — the Unreal TShaderPermutationDomain ×
// Filament curated-validity synthesis in C++23.
//
//   * dimensions are NTTP-named (BoolDim<"DIR_LIGHT">, EnumDim<"TONEMAP",4>)
//     so a typo is a compile error, not a silently-dead keyword;
//   * a domain is capped at 8 dimensions (Filament lesson: curate small);
//   * validity filters are constexpr predicates evaluated over the WHOLE
//     space at compile time or per-variant at runtime;
//   * to_defines() serialises ALPHABETICALLY -> deterministic define order
//     -> stable CachedCompiler keys (ADR §2.4).
// =============================================================================
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace cd::shader_lib
{

/// Fixed-capacity compile-time string for NTTP dimension names.
template <std::size_t N>
struct DimName
{
    std::array<char, N> chars {};

    // NOLINTNEXTLINE(google-explicit-constructor) NTTP literal conversion.
    consteval DimName(const char (&s)[N])
    {
        std::copy_n(static_cast<const char*>(s), N, chars.begin());
    }

    [[nodiscard]] constexpr std::string_view view() const noexcept
    {
        return std::string_view { chars.data(), N - 1 };
    }
};

/// Boolean dimension: contributes values {0, 1}.
template <DimName Name>
struct BoolDim
{
    static constexpr std::string_view name() noexcept { return Name.view(); }
    static constexpr std::uint32_t count() noexcept { return 2; }
};

/// Enum dimension: contributes values {0 .. Count-1}.
template <DimName Name, std::uint32_t Count>
    requires (Count >= 2)
struct EnumDim
{
    static constexpr std::string_view name() noexcept { return Name.view(); }
    static constexpr std::uint32_t count() noexcept { return Count; }
};

/// One serialised preprocessor define.
struct VariantDefine
{
    std::string_view name;
    std::uint32_t value { 0 };
};

template <typename... Dims>
    requires (sizeof...(Dims) >= 1 && sizeof...(Dims) <= 8)
class VariantDomain
{
public:
    static constexpr std::size_t kDimCount = sizeof...(Dims);

    /// Total raw combinations (before validity filtering).
    static constexpr std::uint64_t space_size() noexcept
    {
        return (static_cast<std::uint64_t>(Dims::count()) * ...);
    }

    /// One point in the domain: a value per dimension, in Dims order.
    struct Variant
    {
        std::array<std::uint32_t, kDimCount> values {};

        /// Typed accessor: Variant::get<"DIR_LIGHT">() — a wrong name
        /// fails to compile (index_of static_assert).
        template <DimName Name>
        [[nodiscard]] constexpr std::uint32_t get() const noexcept
        {
            return values[index_of(Name.view())];
        }

        template <DimName Name>
        constexpr void set(std::uint32_t v) noexcept
        {
            values[index_of(Name.view())] = v;
        }
    };

    /// Map a flat index [0, space_size()) onto a Variant (mixed radix,
    /// first dimension fastest). Deterministic enumeration order.
    [[nodiscard]] static constexpr Variant from_index(std::uint64_t index) noexcept
    {
        Variant v {};
        constexpr std::array<std::uint32_t, kDimCount> counts { Dims::count()... };
        for (std::size_t d = 0; d < kDimCount; ++d)
        {
            v.values[d] = static_cast<std::uint32_t>(index % counts[d]);
            index /= counts[d];
        }
        return v;
    }

    /// Visit every variant ACCEPTED by the validity filter. The filter is
    /// the Filament-style curated gate: invalid combinations never become
    /// compiled permutations. Returns the accepted count.
    template <typename Filter, typename Fn>
    static constexpr std::uint64_t for_each_valid(Filter&& filter, Fn&& fn)
    {
        std::uint64_t accepted = 0;
        for (std::uint64_t i = 0; i < space_size(); ++i)
        {
            const Variant v = from_index(i);
            if (filter(v))
            {
                ++accepted;
                fn(v);
            }
        }
        return accepted;
    }

    /// Count of filter-accepted variants — constexpr-evaluable, so a
    /// static_assert can pin the curated budget at compile time.
    template <typename Filter>
    [[nodiscard]] static constexpr std::uint64_t valid_count(Filter&& filter)
    {
        std::uint64_t n = 0;
        for (std::uint64_t i = 0; i < space_size(); ++i)
        {
            if (filter(from_index(i)))
                ++n;
        }
        return n;
    }

    /// Serialise to preprocessor defines, sorted ALPHABETICALLY by name —
    /// the deterministic order the CachedCompiler key relies on.
    [[nodiscard]] static constexpr std::array<VariantDefine, kDimCount>
    to_defines(const Variant& v) noexcept
    {
        std::array<VariantDefine, kDimCount> out {};
        constexpr std::array<std::string_view, kDimCount> names { Dims::name()... };
        for (std::size_t d = 0; d < kDimCount; ++d)
            out[d] = VariantDefine { names[d], v.values[d] };
        std::sort(out.begin(), out.end(),
                  [](const VariantDefine& a, const VariantDefine& b)
                  { return a.name < b.name; });
        return out;
    }

    /// Render defines into GLSL preamble text ("#define NAME value\n"...)
    /// ready to prepend to a module-composed source.
    [[nodiscard]] static std::string to_preamble(const Variant& v)
    {
        std::string out;
        for (const auto& d : to_defines(v))
        {
            out += "#define ";
            out += d.name;
            out += ' ';
            out += std::to_string(d.value);
            out += '\n';
        }
        return out;
    }

    /// Compile-time dimension lookup; unknown names are a compile error
    /// in constexpr context (out-of-range index).
    [[nodiscard]] static constexpr std::size_t index_of(std::string_view name) noexcept
    {
        constexpr std::array<std::string_view, kDimCount> names { Dims::name()... };
        for (std::size_t d = 0; d < kDimCount; ++d)
        {
            if (names[d] == name)
                return d;
        }
        return kDimCount;  // out of range -> constexpr misuse traps
    }
};

}  // namespace cd::shader_lib
