// =============================================================================
// CHROMODYNAMIC — cd/editor/Bookmark.hpp
// Phase 66.B / Wave 234 — camera bookmarks for the editor viewport.
//
// A `Bookmark` is a named camera state snapshot the editor can jump
// to: "Front", "Top", "Inside-cockpit", "Best-shot". Designers cycle
// through them with hotkeys (Numpad 1-9) during level work.
//
// State is camera-payload-agnostic — we store eye + target + up. FOV /
// near / far stay on the active Camera (Phase 38 CameraPath is the
// timeline-driven sibling; Bookmark is the snap-jump variant).
// =============================================================================
#pragma once

#include <cd/camera/Camera.hpp>
#include <cd/core/Defines.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace cd::editor
{

struct Bookmark
{
    std::string         name;
    cd::math::Vec3f     eye    {};
    cd::math::Vec3f     target {};
    cd::math::Vec3f     up     { 0.0F, 1.0F, 0.0F };
};

class BookmarkSet
{
public:
    void add(std::string name, const cd::camera::Camera& cam)
    {
        marks_.push_back(Bookmark { std::move(name), cam.eye, cam.target, cam.up });
    }

    void remove(std::size_t index) noexcept
    {
        if (index < marks_.size()) marks_.erase(marks_.begin() + static_cast<std::ptrdiff_t>(index));
    }

    [[nodiscard]] std::size_t size() const noexcept { return marks_.size(); }

    [[nodiscard]] const Bookmark* at(std::size_t i) const noexcept
    {
        return (i < marks_.size()) ? &marks_[i] : nullptr;
    }

    /// Apply bookmark `i` to `cam` (eye/target/up only). Returns true
    /// on success.
    bool apply(std::size_t i, cd::camera::Camera& cam) const noexcept
    {
        if (i >= marks_.size()) return false;
        const auto& b = marks_[i];
        cam.eye    = b.eye;
        cam.target = b.target;
        cam.up     = b.up;
        return true;
    }

    void clear() noexcept { marks_.clear(); }

private:
    std::vector<Bookmark> marks_;
};

}  // namespace cd::editor
