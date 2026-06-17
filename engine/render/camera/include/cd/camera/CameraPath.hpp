// =============================================================================
// CHROMODYNAMIC — cd/camera/CameraPath.hpp
// Phase 37.A / Wave 205 — keyframed camera path with Catmull-Rom interpolation.
//
// A CameraPath is a sorted list of `Keyframe { t, eye, target }`. The
// `sample(t)` function:
//   * Clamps `t` to the [first.t, last.t] interval.
//   * Locates the surrounding two keyframes via lower_bound.
//   * Catmull-Rom interpolates eye and target separately (using the
//     two outer keyframes for tangent estimation; clamps at endpoints
//     so the first/last segment falls back to LERP).
//
// The `up` vector is held constant on the Camera between samples —
// this matches the cinema convention that ROT-XY is a tilt move and
// ROT-Z is post-process. If you need barrel-roll, sample `up` from
// an external orientation track.
//
// Use case: scripted cutscene cameras in the editor preview, replay
// playback, screenshot turntables. Heavy enough authoring tooling
// belongs elsewhere; this header is the runtime sampling primitive.
// =============================================================================
#pragma once

#include <cd/camera/Camera.hpp>
#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <vector>

namespace cd::camera
{

struct CameraKey
{
    float           t {};
    cd::math::Vec3f eye {};
    cd::math::Vec3f target {};
};

class CameraPath
{
public:
    void add_key(const CameraKey& k)
    {
        // NOLINTNEXTLINE(modernize-use-ranges): heterogeneous comparator (CameraKey vs float key) has no clean ranges-projection form.
        auto it = std::lower_bound(keys_.begin(), keys_.end(), k.t,
            [](const CameraKey& a, float v) { return a.t < v; });
        keys_.insert(it, k);
    }

    [[nodiscard]] std::size_t size() const noexcept { return keys_.size(); }

    [[nodiscard]] const std::vector<CameraKey>& keys() const noexcept { return keys_; }

    /// Sample `(eye, target)` at time t. Empty path returns identity-ish
    /// origin/forward.
    [[nodiscard]] CameraKey sample(float t) const
    {
        if (keys_.empty()) return CameraKey { 0.0F, {}, { 0, 0, -1 } };
        if (t <= keys_.front().t) return keys_.front();
        if (t >= keys_.back().t)  return keys_.back();

        // NOLINTNEXTLINE(modernize-use-ranges): heterogeneous comparator (CameraKey vs float key) has no clean ranges-projection form.
        const auto it = std::lower_bound(keys_.begin(), keys_.end(), t,
            [](const CameraKey& a, float v) { return a.t < v; });
        const std::size_t i = static_cast<std::size_t>(it - keys_.begin());
        const auto& p1 = keys_[i - 1];
        const auto& p2 = keys_[i];
        const auto& p0 = (i >= 2) ? keys_[i - 2] : p1;
        const auto& p3 = (i + 1 < keys_.size()) ? keys_[i + 1] : p2;

        const float seg = (p2.t - p1.t);
        const float u = (seg > 0.0F) ? ((t - p1.t) / seg) : 0.0F;

        CameraKey out;
        out.t = t;
        out.eye    = catmull_rom(p0.eye,    p1.eye,    p2.eye,    p3.eye,    u);
        out.target = catmull_rom(p0.target, p1.target, p2.target, p3.target, u);
        return out;
    }

    /// Apply a sampled key to a `Camera`. Leaves up / fov / clip planes alone.
    static void apply(Camera& cam, const CameraKey& k) noexcept
    {
        cam.eye = k.eye;
        cam.target = k.target;
    }

private:
    [[nodiscard]] static cd::math::Vec3f catmull_rom(
        const cd::math::Vec3f& p0,
        const cd::math::Vec3f& p1,
        const cd::math::Vec3f& p2,
        const cd::math::Vec3f& p3,
        float u) noexcept
    {
        const float u2 = u * u;
        const float u3 = u2 * u;
        cd::math::Vec3f r;
        r.x = 0.5F * ((2.0F * p1.x) +
                      (-p0.x + p2.x) * u +
                      (2.0F * p0.x - 5.0F * p1.x + 4.0F * p2.x - p3.x) * u2 +
                      (-p0.x + 3.0F * p1.x - 3.0F * p2.x + p3.x) * u3);
        r.y = 0.5F * ((2.0F * p1.y) +
                      (-p0.y + p2.y) * u +
                      (2.0F * p0.y - 5.0F * p1.y + 4.0F * p2.y - p3.y) * u2 +
                      (-p0.y + 3.0F * p1.y - 3.0F * p2.y + p3.y) * u3);
        r.z = 0.5F * ((2.0F * p1.z) +
                      (-p0.z + p2.z) * u +
                      (2.0F * p0.z - 5.0F * p1.z + 4.0F * p2.z - p3.z) * u2 +
                      (-p0.z + 3.0F * p1.z - 3.0F * p2.z + p3.z) * u3);
        return r;
    }

    std::vector<CameraKey> keys_;
};

}  // namespace cd::camera
