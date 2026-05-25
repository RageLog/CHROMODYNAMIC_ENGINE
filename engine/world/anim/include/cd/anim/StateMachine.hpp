// =============================================================================
// CHROMODYNAMIC — cd/anim/StateMachine.hpp
// Phase 156-extension / v0.99.83 — animation state machine on top of
// PoseBlend.
//
// Every character controller needs to map a continuous input
// (velocity magnitude, jump trigger, attack request, …) to a
// pose-per-frame. This header provides a minimal finite state
// machine where:
//
//   * Each state owns a SkinnedClip + a loop policy.
//   * Transitions between states blend the pose over a configurable
//     duration via PoseBlend.
//   * The machine drives itself per `tick(dt, blackboard)` —
//     callers don't poll, they just read `current_pose()`.
//
// Blackboard: a lightweight key/value store (string → float) that
// transition conditions consult. Keeping it stringly-typed instead
// of templated keeps the API surface tractable and matches how every
// production controller is built (production animator, production AnimBP,
// Bevy bevy_animation_graph).
//
// Header-only because the implementation is a small loop over the
// stored states + transitions vector.
// =============================================================================
#pragma once

#include <cd/anim/PoseBlend.hpp>
#include <cd/anim/Skeleton.hpp>
#include <cd/core/Defines.hpp>

#include <algorithm>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cd::anim
{

/// Per-state metadata.
struct AnimState
{
    std::string  name;
    const SkinnedClip* clip { nullptr };
    bool         looping { true };
    float        speed { 1.0F };
    float        time { 0.0F };  // local clip time (seconds)
};

/// Predicate that decides whether a transition fires. Receives the
/// blackboard + the elapsed time in the current state.
using TransitionPredicate = std::function<bool(
    const std::unordered_map<std::string, float>& blackboard,
    float elapsed_in_state)>;

/// State-to-state transition with a blend duration.
struct AnimTransition
{
    std::string         from;
    std::string         to;
    TransitionPredicate condition;
    float               blend_duration { 0.15F };
};

class AnimStateMachine
{
public:
    /// Register a state. Returns false if a state with this name
    /// already exists.
    bool add_state(AnimState state)
    {
        if (state_idx_.contains(state.name)) return false;
        state_idx_[state.name] = states_.size();
        states_.push_back(std::move(state));
        return true;
    }

    /// Register a transition between two existing states.
    bool add_transition(AnimTransition t)
    {
        if (!state_idx_.contains(t.from) || !state_idx_.contains(t.to)) return false;
        transitions_.push_back(std::move(t));
        return true;
    }

    /// Set the starting state (must be registered first).
    bool set_initial_state(std::string_view name)
    {
        const auto it = state_idx_.find(std::string { name });
        if (it == state_idx_.end()) return false;
        current_ = it->second;
        elapsed_in_state_ = 0.0F;
        blending_ = false;
        return true;
    }

    /// Update the blackboard. Stringly-typed for ergonomic transition
    /// predicates (`b["speed"] > 1.0F`).
    void set(std::string_view key, float value)
    {
        blackboard_[std::string { key }] = value;
    }

    [[nodiscard]] float get(std::string_view key, float fallback = 0.0F) const
    {
        const auto it = blackboard_.find(std::string { key });
        return it == blackboard_.end() ? fallback : it->second;
    }

    /// Advance one frame. Samples the current state's clip into
    /// `current_pose_`. If a transition is in flight, blends from the
    /// previous state's last sampled pose toward the new state's
    /// current sample. When the blend completes, the previous state
    /// is dropped and the machine settles into the new state.
    ///
    /// `skel` is consulted only for the initial pose size; pass the
    /// same skeleton on every tick.
    void tick(float dt, const Skeleton& skel)
    {
        if (states_.empty()) return;
        if (current_pose_.joint_locals.size() != skel.joint_count())
            current_pose_ = Pose::bind_pose(skel);

        elapsed_in_state_ += dt;

        // Check transitions out of the current state. First match wins.
        if (!blending_)
        {
            for (const auto& t : transitions_)
            {
                if (t.from != states_[current_].name) continue;
                if (!t.condition) continue;
                if (t.condition(blackboard_, elapsed_in_state_))
                {
                    blend_from_pose_ = current_pose_;
                    previous_ = current_;
                    current_ = state_idx_[t.to];
                    states_[current_].time = 0.0F;
                    elapsed_in_state_ = 0.0F;
                    blend_time_ = 0.0F;
                    blend_duration_ = std::max(0.001F, t.blend_duration);
                    blending_ = true;
                    break;
                }
            }
        }

        // Advance the current state's clock + sample.
        auto& s = states_[current_];
        s.time += dt * s.speed;
        Pose new_pose = Pose::bind_pose(skel);
        if (s.clip != nullptr)
        {
            float clip_t = s.time;
            if (s.looping)
            {
                const auto dur = s.clip->duration();
                if (dur > 0.0F) clip_t = std::fmod(s.time, dur);
            }
            s.clip->sample(clip_t, new_pose);
        }

        if (blending_)
        {
            blend_time_ += dt;
            const float w = std::clamp(blend_time_ / blend_duration_, 0.0F, 1.0F);
            blend_pose(blend_from_pose_, new_pose, w, current_pose_);
            if (w >= 1.0F) blending_ = false;
        }
        else
        {
            current_pose_ = new_pose;
        }
    }

    [[nodiscard]] const Pose& current_pose() const noexcept { return current_pose_; }

    [[nodiscard]] std::string_view current_state() const noexcept
    {
        return states_.empty() ? std::string_view {} : std::string_view { states_[current_].name };
    }

    [[nodiscard]] bool is_blending() const noexcept { return blending_; }

private:
    std::vector<AnimState> states_;
    std::vector<AnimTransition> transitions_;
    std::unordered_map<std::string, std::size_t> state_idx_;
    std::unordered_map<std::string, float> blackboard_;

    std::size_t current_  { 0 };
    std::size_t previous_ { 0 };
    float       elapsed_in_state_ { 0.0F };

    Pose        current_pose_;
    Pose        blend_from_pose_;
    bool        blending_ { false };
    float       blend_time_ { 0.0F };
    float       blend_duration_ { 0.15F };
};

}  // namespace cd::anim
