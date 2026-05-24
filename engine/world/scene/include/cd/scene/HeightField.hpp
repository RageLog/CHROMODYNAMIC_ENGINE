// =============================================================================
// CHROMODYNAMIC — cd/scene/HeightField.hpp
// Phase 57.A / Wave 225 — 2D heightmap with bilinear sampling.
//
// Stores a width×height grid of float heights + a world-space cell
// size and origin. `sample(world_x, world_z)` returns the bilinearly
// interpolated height at world coordinates. Out-of-bounds returns the
// nearest edge sample (clamp-to-edge), matching texture sampler
// convention.
//
// Use as the simplest terrain backend stand-in until a quad-tree or
// chunked streaming version is justified.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cd::scene
{

class HeightField
{
public:
    HeightField(std::uint32_t width, std::uint32_t height,
                float cell_size = 1.0F, float origin_x = 0.0F, float origin_z = 0.0F)
        : width_ { width }, height_ { height },
          cell_size_ { cell_size > 0.0F ? cell_size : 1.0F },
          origin_x_ { origin_x }, origin_z_ { origin_z },
          heights_(static_cast<std::size_t>(width) * height, 0.0F)
    {
    }

    void set(std::uint32_t x, std::uint32_t z, float h) noexcept
    {
        if (x >= width_ || z >= height_) return;
        heights_[static_cast<std::size_t>(z) * width_ + x] = h;
    }

    [[nodiscard]] float get(std::uint32_t x, std::uint32_t z) const noexcept
    {
        if (x >= width_ || z >= height_) return 0.0F;
        return heights_[static_cast<std::size_t>(z) * width_ + x];
    }

    [[nodiscard]] float sample(float world_x, float world_z) const noexcept
    {
        const float fx = (world_x - origin_x_) / cell_size_;
        const float fz = (world_z - origin_z_) / cell_size_;
        // Clamp to [0, w-1] × [0, h-1].
        const float cx = std::clamp(fx, 0.0F, static_cast<float>(width_ - 1));
        const float cz = std::clamp(fz, 0.0F, static_cast<float>(height_ - 1));
        const auto x0 = static_cast<std::uint32_t>(cx);
        const auto z0 = static_cast<std::uint32_t>(cz);
        const auto x1 = (x0 + 1 < width_) ? (x0 + 1) : x0;
        const auto z1 = (z0 + 1 < height_) ? (z0 + 1) : z0;
        const float u = cx - static_cast<float>(x0);
        const float v = cz - static_cast<float>(z0);
        const float h00 = heights_[static_cast<std::size_t>(z0) * width_ + x0];
        const float h10 = heights_[static_cast<std::size_t>(z0) * width_ + x1];
        const float h01 = heights_[static_cast<std::size_t>(z1) * width_ + x0];
        const float h11 = heights_[static_cast<std::size_t>(z1) * width_ + x1];
        const float row0 = h00 + (h10 - h00) * u;
        const float row1 = h01 + (h11 - h01) * u;
        return row0 + (row1 - row0) * v;
    }

    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] float         cell_size() const noexcept { return cell_size_; }

private:
    std::uint32_t       width_;
    std::uint32_t       height_;
    float               cell_size_;
    float               origin_x_;
    float               origin_z_;
    std::vector<float>  heights_;
};

}  // namespace cd::scene
