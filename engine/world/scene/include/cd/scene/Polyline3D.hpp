// =============================================================================
// CHROMODYNAMIC — cd/scene/Polyline3D.hpp
// Phase 78.A / Wave 246 — ordered list of 3D points + total length.
//
// Polyline is the simplest path primitive — connected line segments
// through N vertices. Used for:
//   * AI navigation paths (sequence of waypoints).
//   * Debug overlays (trajectory, line trail).
//   * CameraPath baking (a CameraPath sampled at fixed dt produces a
//     Polyline3D the renderer can draw).
//
// API:
//   * `add(point)` — append vertex.
//   * `length()` — total arc length (sum of segment lengths).
//   * `point_at(s)` — sample at arc-length `s ∈ [0, length()]`.
//   * `point_count()`.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <cmath>
#include <vector>

namespace cd::scene
{

class Polyline3D
{
public:
    void add(const cd::math::Vec3f& p)
    {
        if (!points_.empty())
        {
            const auto& prev = points_.back();
            const float dx = p.x - prev.x;
            const float dy = p.y - prev.y;
            const float dz = p.z - prev.z;
            length_ += std::sqrt(dx * dx + dy * dy + dz * dz);
        }
        points_.push_back(p);
    }

    [[nodiscard]] std::size_t point_count() const noexcept { return points_.size(); }
    [[nodiscard]] float       length()      const noexcept { return length_; }

    [[nodiscard]] const std::vector<cd::math::Vec3f>& points() const noexcept { return points_; }

    /// Sample the polyline at arc-length `s`. Clamped to [0, length()].
    /// Returns origin if the polyline is empty; returns the only
    /// vertex if it has one.
    [[nodiscard]] cd::math::Vec3f point_at(float s) const noexcept
    {
        if (points_.empty()) return cd::math::Vec3f {};
        if (points_.size() == 1 || s <= 0.0F) return points_.front();
        if (s >= length_) return points_.back();
        float walked = 0.0F;
        for (std::size_t i = 0; i + 1 < points_.size(); ++i)
        {
            const auto& a = points_[i];
            const auto& b = points_[i + 1];
            const float dx = b.x - a.x;
            const float dy = b.y - a.y;
            const float dz = b.z - a.z;
            const float seg = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (s <= walked + seg && seg > 0.0F)
            {
                const float u = (s - walked) / seg;
                return cd::math::Vec3f {
                    a.x + dx * u, a.y + dy * u, a.z + dz * u,
                };
            }
            walked += seg;
        }
        return points_.back();
    }

    void clear() noexcept
    {
        points_.clear();
        length_ = 0.0F;
    }

private:
    std::vector<cd::math::Vec3f> points_;
    float                        length_ { 0.0F };
};

}  // namespace cd::scene
