// =============================================================================
// CHROMODYNAMIC — cd/rhi/VertexLayoutBuilder.hpp
// Phase 60.A / Wave 228 — fluent builder for VertexAttribute lists.
//
// `VertexLayoutBuilder` accumulates `VertexAttribute` entries with
// auto-incremented `location` + per-attribute byte `offset`. The
// builder owns the underlying vector so the resulting view is stable
// across moves.
//
//   auto layout = VertexLayoutBuilder {}
//                 .add(Format::kRGB32Float)       // position  → loc 0, off 0
//                 .add(Format::kRGB32Float)       // normal    → loc 1, off 12
//                 .add(Format::kRG32Float)        // uv        → loc 2, off 24
//                 .build();
//
// `build()` returns the attribute vector; the caller stores it
// somewhere stable and passes a span to PipelineDesc.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Format.hpp>

#include <cstdint>
#include <vector>

namespace cd::rhi
{

[[nodiscard]] constexpr std::uint32_t bytes_of(Format f) noexcept
{
    switch (f)
    {
        case Format::kR32Float:    return 4;
        case Format::kRG32Float:   return 8;
        case Format::kRGB32Float:  return 12;
        case Format::kRGBA32Float: return 16;
        case Format::kR8Unorm:     return 1;
        case Format::kRG8Unorm:    return 2;
        case Format::kRGBA8Unorm:  return 4;
        case Format::kRGBA8Srgb:   return 4;
        default:                   return 4;
    }
}

class VertexLayoutBuilder
{
public:
    VertexLayoutBuilder& add(Format f) noexcept
    {
        attrs_.push_back(VertexAttribute {
            next_location_++, 0u, f, offset_
        });
        offset_ += bytes_of(f);
        return *this;
    }

    VertexLayoutBuilder& add(Format f, std::uint32_t binding) noexcept
    {
        attrs_.push_back(VertexAttribute {
            next_location_++, binding, f, offset_
        });
        offset_ += bytes_of(f);
        return *this;
    }

    [[nodiscard]] std::vector<VertexAttribute> build() noexcept
    {
        return std::move(attrs_);
    }

    [[nodiscard]] std::uint32_t stride() const noexcept { return offset_; }

private:
    std::vector<VertexAttribute> attrs_;
    std::uint32_t                next_location_ { 0 };
    std::uint32_t                offset_ { 0 };
};

}  // namespace cd::rhi
