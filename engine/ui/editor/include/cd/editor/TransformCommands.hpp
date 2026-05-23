// =============================================================================
// CHROMODYNAMIC — cd/editor/TransformCommands.hpp
// Phase 12.C / v0.28.0 — concrete edit commands for cd::scene transforms.
//
// Three commands, one per axis-aligned mutation an inspector or gizmo
// would routinely apply:
//
//   * TranslateCommand — adds a delta to LocalTransform::position.
//   * ScaleCommand     — multiplies LocalTransform::scale component-wise.
//   * RotateCommand    — stores a quaternion replacement (overwrite,
//                        not compose; the inspector edits xyzw directly
//                        in most UIs and the gizmo writes the new pose
//                        in one shot).
//
// All three implement ICommand. apply() / revert() are pure inverses:
// revert() restores the captured pre-apply value byte-for-byte.
//
// The commands hold a raw pointer to cd::scene::Scene and an
// cd::ecs::Entity handle. Lifetime contract: the Scene + Entity must
// outlive the command. The EditHistory's `clear()` is the right place
// to drop these when a new scene is loaded (the generation counter
// inside Entity would also reject lookups against the old World, but
// we don't want the command to silently no-op — clear() is explicit).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ecs/Entity.hpp>
#include <cd/editor/EditHistory.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Vector.hpp>
#include <cd/scene/Scene.hpp>

#include <string>

namespace cd::editor
{

class TranslateCommand final : public ICommand
{
public:
    TranslateCommand(cd::scene::Scene& scene, cd::ecs::Entity target, cd::math::Vec3f delta)
        : scene_(scene), target_(target), delta_(delta)
    {
        label_ = "Translate";
    }

    void apply() override
    {
        if (auto* lt = scene_.local(target_); lt != nullptr)
        {
            lt->value.position.x += delta_.x;
            lt->value.position.y += delta_.y;
            lt->value.position.z += delta_.z;
        }
    }

    void revert() override
    {
        if (auto* lt = scene_.local(target_); lt != nullptr)
        {
            lt->value.position.x -= delta_.x;
            lt->value.position.y -= delta_.y;
            lt->value.position.z -= delta_.z;
        }
    }

    [[nodiscard]] std::string_view label() const noexcept override { return label_; }
    [[nodiscard]] std::size_t byte_size() const noexcept override { return sizeof(*this); }

private:
    cd::scene::Scene& scene_;
    cd::ecs::Entity target_;
    cd::math::Vec3f delta_;
    std::string label_;
};

class ScaleCommand final : public ICommand
{
public:
    ScaleCommand(cd::scene::Scene& scene, cd::ecs::Entity target, cd::math::Vec3f factor)
        : scene_(scene), target_(target), factor_(factor)
    {
        label_ = "Scale";
    }

    void apply() override
    {
        if (auto* lt = scene_.local(target_); lt != nullptr)
        {
            previous_ = lt->value.scale;
            lt->value.scale.x *= factor_.x;
            lt->value.scale.y *= factor_.y;
            lt->value.scale.z *= factor_.z;
        }
    }

    void revert() override
    {
        if (auto* lt = scene_.local(target_); lt != nullptr)
            lt->value.scale = previous_;
    }

    [[nodiscard]] std::string_view label() const noexcept override { return label_; }
    [[nodiscard]] std::size_t byte_size() const noexcept override { return sizeof(*this); }

private:
    cd::scene::Scene& scene_;
    cd::ecs::Entity target_;
    cd::math::Vec3f factor_;
    cd::math::Vec3f previous_ { 1.0F, 1.0F, 1.0F };
    std::string label_;
};

class RotateCommand final : public ICommand
{
public:
    RotateCommand(cd::scene::Scene& scene, cd::ecs::Entity target, cd::math::Quatf new_rotation)
        : scene_(scene), target_(target), new_rotation_(new_rotation)
    {
        label_ = "Rotate";
    }

    void apply() override
    {
        if (auto* lt = scene_.local(target_); lt != nullptr)
        {
            previous_ = lt->value.rotation;
            lt->value.rotation = new_rotation_;
        }
    }

    void revert() override
    {
        if (auto* lt = scene_.local(target_); lt != nullptr)
            lt->value.rotation = previous_;
    }

    [[nodiscard]] std::string_view label() const noexcept override { return label_; }
    [[nodiscard]] std::size_t byte_size() const noexcept override { return sizeof(*this); }

private:
    cd::scene::Scene& scene_;
    cd::ecs::Entity target_;
    cd::math::Quatf new_rotation_;
    cd::math::Quatf previous_ { 0.0F, 0.0F, 0.0F, 1.0F };
    std::string label_;
};

}  // namespace cd::editor
