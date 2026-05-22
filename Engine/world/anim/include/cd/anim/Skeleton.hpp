// =============================================================================
// CHROMODYNAMIC — cd/anim/Skeleton.hpp
// Phase 5 / S4.2.b — skeletal animation primitives.
//
// Adds per-bone animation on top of the existing single-target
// `cd::anim::AnimationClip + AnimationPlayer`. The data layout:
//
//   Skeleton  : flat array of Joint { name, parent, bind-pose Mat4 }.
//   JointPose : flat array of Transformf, one per joint, same length.
//   SkinnedClip : keyframes addressed per-joint (sparse — only joints
//                 that animate carry tracks; missing joints stay at
//                 bind pose).
//
// Sampling: per joint, linear-interp position/scale + slerp rotation
// between adjacent keyframes; clamp/loop policy is per-clip just like
// the single-target AnimationClip.
//
// Output to the GPU: a `compute_skinning_matrices(skeleton, pose, out)`
// helper builds the per-bone (parent_world * local * inverse_bind)
// matrices that a vertex shader multiplies with influence weights.
// =============================================================================
#pragma once

#include <cd/anim/Animation.hpp>
#include <cd/core/Defines.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Transform.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cd::anim
{

struct Joint
{
    std::string name;
    /// Parent joint index, or -1 for the root.
    std::int32_t parent { -1 };
    /// Bind-pose local transform (rest position of this joint relative
    /// to its parent).
    cd::math::Transformf local_bind {};
    /// Inverse of the bind-pose WORLD matrix. Pre-computed at skeleton
    /// build time so the GPU pass becomes `world(t) · inverse_bind`.
    cd::math::Mat4f inverse_bind_matrix { cd::math::Mat4f::identity() };
};

class Skeleton
{
public:
    Skeleton() noexcept = default;

    /// Build a skeleton from an ordered joint list. The constructor
    /// validates that every non-root parent index points to an EARLIER
    /// joint (topological order). Inverse-bind matrices are computed
    /// here so the per-frame sampler doesn't pay for them.
    explicit Skeleton(std::vector<Joint> joints)
        : joints_ { std::move(joints) }
    {
        recompute_inverse_binds_();
        index_names_();
    }

    [[nodiscard]] std::size_t joint_count() const noexcept { return joints_.size(); }

    [[nodiscard]] const Joint& joint(std::size_t i) const { return joints_.at(i); }

    [[nodiscard]] const std::vector<Joint>& joints() const noexcept { return joints_; }

    /// Look up a joint by name. Returns -1 if missing.
    [[nodiscard]] std::int32_t find(std::string_view name) const
    {
        const auto it = name_to_index_.find(std::string { name });
        return it == name_to_index_.end() ? -1 : static_cast<std::int32_t>(it->second);
    }

    /// Joint indices in a topological order (root → leaf). Same as the
    /// flat `joints()` vector order because the constructor required it.
    [[nodiscard]] std::vector<std::size_t> topological_order() const
    {
        std::vector<std::size_t> out(joints_.size());
        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] = i;
        return out;
    }

private:
    void recompute_inverse_binds_()
    {
        std::vector<cd::math::Mat4f> world_binds(joints_.size(), cd::math::Mat4f::identity());
        for (std::size_t i = 0; i < joints_.size(); ++i)
        {
            const auto& j = joints_[i];
            const auto local = cd::math::to_mat4(j.local_bind);
            if (j.parent < 0)
                world_binds[i] = local;
            else
                world_binds[i] = world_binds[static_cast<std::size_t>(j.parent)] * local;
            joints_[i].inverse_bind_matrix = cd::math::inverse(world_binds[i]);
        }
    }

    void index_names_()
    {
        name_to_index_.clear();
        for (std::size_t i = 0; i < joints_.size(); ++i)
            name_to_index_[joints_[i].name] = i;
    }

    std::vector<Joint> joints_;
    std::unordered_map<std::string, std::size_t> name_to_index_;
};

/// Live pose: one Transform per joint, matching the skeleton's joint
/// order. Default-constructed = identity for every joint (bind pose).
struct Pose
{
    std::vector<cd::math::Transformf> joint_locals;

    [[nodiscard]] static Pose bind_pose(const Skeleton& s)
    {
        Pose p;
        p.joint_locals.resize(s.joint_count());
        for (std::size_t i = 0; i < s.joint_count(); ++i)
            p.joint_locals[i] = s.joint(i).local_bind;
        return p;
    }
};

/// Per-joint animation tracks. `joint_tracks[j]` is the keyframe list for
/// joint j (empty list = joint stays at bind pose).
class SkinnedClip
{
public:
    SkinnedClip() noexcept = default;

    explicit SkinnedClip(std::size_t joint_count) { joint_tracks_.resize(joint_count); }

    [[nodiscard]] std::size_t joint_count() const noexcept { return joint_tracks_.size(); }

    /// Set the keyframe list for a specific joint. Frames must be
    /// sorted by time (constructor doesn't sort).
    void set_track(std::size_t joint_index, std::vector<Keyframe> frames)
    {
        if (joint_index < joint_tracks_.size())
            joint_tracks_[joint_index] = std::move(frames);
    }

    [[nodiscard]] const std::vector<Keyframe>& track(std::size_t joint_index) const
    {
        return joint_tracks_.at(joint_index);
    }

    /// Sample the clip at absolute time `t` (seconds, no looping
    /// applied — caller wraps). Writes the resulting Transform per
    /// joint into `out_pose`. Untracked joints stay at their existing
    /// value in `out_pose` (bind pose if `out_pose` started from
    /// `Pose::bind_pose(skel)`).
    void sample(float t, Pose& out_pose) const
    {
        const auto n = std::min(joint_tracks_.size(), out_pose.joint_locals.size());
        for (std::size_t j = 0; j < n; ++j)
        {
            const auto& frames = joint_tracks_[j];
            if (frames.empty())
                continue;
            out_pose.joint_locals[j] = sample_track_(frames, t);
        }
    }

    /// Total duration = max keyframe time across all joint tracks.
    [[nodiscard]] float duration() const noexcept
    {
        float d = 0.0F;
        for (const auto& track : joint_tracks_)
        {
            if (track.size() >= 2)
            {
                const float td = track.back().time - track.front().time;
                if (td > d)
                    d = td;
            }
        }
        return d;
    }

private:
    static cd::math::Transformf sample_track_(const std::vector<Keyframe>& frames, float t) noexcept;

    std::vector<std::vector<Keyframe>> joint_tracks_;
};

/// Build the GPU-ready skinning matrices: for each joint i,
///   out[i] = world(i, pose) · inverse_bind(i)
/// where world(i, pose) = parent_world * to_mat4(pose.joint_locals[i]).
/// The vertex shader multiplies vertex by Σ weight_k · out[joint_k].
inline void compute_skinning_matrices(const Skeleton& skel,
                                      const Pose& pose,
                                      std::vector<cd::math::Mat4f>& out)
{
    out.assign(skel.joint_count(), cd::math::Mat4f::identity());
    std::vector<cd::math::Mat4f> world(skel.joint_count(), cd::math::Mat4f::identity());
    for (std::size_t i = 0; i < skel.joint_count(); ++i)
    {
        const auto local = cd::math::to_mat4(pose.joint_locals.size() > i
                                                 ? pose.joint_locals[i]
                                                 : skel.joint(i).local_bind);
        const auto parent = skel.joint(i).parent;
        if (parent < 0)
            world[i] = local;
        else
            world[i] = world[static_cast<std::size_t>(parent)] * local;
        out[i] = world[i] * skel.joint(i).inverse_bind_matrix;
    }
}

}  // namespace cd::anim
