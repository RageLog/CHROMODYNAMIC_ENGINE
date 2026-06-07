// =============================================================================
// CHROMODYNAMIC — cd/game/anim_graph/AnimGraph.hpp
// Phase 481 — cd::game::anim_graph (G2.3: animation graph)
//
// Topology:
//   * AnimNode (abstract base) — tick(t, blackboard) -> Pose
//   * PlayClipNode             — wraps cd::anim::SkinnedClip
//   * Blend1DNode              — scalar parameter -> weighted blend of N inputs
//   * Blend2DNode              — cartesian (x,y) -> 4-corner bilinear blend
//   * StateMachineNode         — predicate-driven transitions between AnimNodes
//
// Ticking model:
//   * Every node implements `Pose tick(float t, const Blackboard&,
//     const Skeleton&)`. `t` is absolute graph time (seconds) — leaves
//     map it onto their owned clip via the clip's own time axis.
//   * The blackboard is a string -> float scratch store consulted by
//     blends (for parameter values) and state-machine predicates.
//   * The graph itself is a thin owner of a single root AnimNode that
//     gets ticked once per frame via `AnimGraph::evaluate`.
//
// Blending:
//   * Blend1D / Blend2D delegate per-joint LERP/NLERP to the existing
//     cd::anim::lerp_transform / cd::anim::blend_pose helpers (PoseBlend.hpp).
//   * Mismatched input pose sizes are handled gracefully — the shortest
//     prefix is blended, the remainder is left untouched (bind-pose
//     fallback once the graph initialises the output via Pose::bind_pose).
//
// References:
//   * Mecanim (Unity) animator graph + Blend Tree (1D / 2D Simple Directional /
//     2D Freeform Cartesian).
//   * production AnimGraph / AnimStateMachine (Persson, "Animation in production
//     engine", GDC 2010).
//   * Bevy bevy_animation_graph crate (BlendNode + AnimationClip drivers).
//
// Allocations:
//   * Children are owned via std::unique_ptr<AnimNode>; tree construction
//     is a one-time cost. tick() pose buffers are stack-local std::vector
//     instances — future hot-path work can replace them with a frame
//     allocator (see ADR-W7-S3 pose-arena draft).
//
// Threading:
//   * Not thread-safe. One graph per controller, ticked from the owning
//     thread (same contract as cd::anim::AnimStateMachine, cd::game::ai_bt).
//
// Dependencies (CLAUDE.md S7): cd::core, cd::math, cd::anim. The graph
// is strictly above cd::anim and strictly below cd::scene / sample tier.
// =============================================================================
#pragma once

#include <cd/anim/PoseBlend.hpp>
#include <cd/anim/Skeleton.hpp>
#include <cd/core/Defines.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cd::game::anim_graph
{

// -----------------------------------------------------------------------------
// Blackboard — string -> float scratch store.
//
// Kept deliberately narrow (float-only) because every graph parameter
// in practice is a scalar: locomotion speed, aim pitch, attack-charge
// magnitude, etc. Boolean predicates (state-machine transitions) read
// floats via comparison ("speed > 0.1F"); identifiers are not part of
// the graph's contract — they live on the entity.
// -----------------------------------------------------------------------------
class Blackboard
{
public:
    Blackboard() = default;

    /// Set or overwrite the value under `key`.
    void set(std::string_view key, float value)
    {
        values_[std::string { key }] = value;
    }

    /// Read the value under `key`; returns `fallback` if absent.
    [[nodiscard]] float get(std::string_view key, float fallback = 0.0F) const
    {
        const auto it = values_.find(std::string { key });
        return it == values_.end() ? fallback : it->second;
    }

    [[nodiscard]] bool has(std::string_view key) const
    {
        return values_.contains(std::string { key });
    }

    bool erase(std::string_view key)
    {
        return values_.erase(std::string { key }) != 0U;
    }

    [[nodiscard]] std::size_t size() const noexcept { return values_.size(); }

    void clear() noexcept { values_.clear(); }

private:
    std::unordered_map<std::string, float> values_ {};
};

// -----------------------------------------------------------------------------
// AnimNode — abstract base.
//
// Subclasses override `tick(t, bb, skel)`. The skeleton is forwarded so
// leaves can size their output `Pose` against `bind_pose(skel)` and so
// blends can detect mismatched pose sizes deterministically.
// -----------------------------------------------------------------------------
class AnimNode
{
public:
    AnimNode() = default;
    virtual ~AnimNode() = default;

    AnimNode(const AnimNode&)            = delete;
    AnimNode& operator=(const AnimNode&) = delete;
    AnimNode(AnimNode&&)                 = delete;
    AnimNode& operator=(AnimNode&&)      = delete;

    /// Sample this node at absolute graph time `t` (seconds). Returns a
    /// fresh Pose; callers may move-from the result. The bind-pose of
    /// `skel` is the default fallback when a node has no clip wired in.
    [[nodiscard]] virtual cd::anim::Pose tick(float t,
                                              const Blackboard& bb,
                                              const cd::anim::Skeleton& skel) = 0;

    /// Optional debug / introspection name. Empty by default; subclasses
    /// may set it via `set_name`. Not consulted by the tick loop.
    [[nodiscard]] std::string_view name() const noexcept { return name_; }

    void set_name(std::string n) { name_ = std::move(n); }

private:
    std::string name_ {};
};

// -----------------------------------------------------------------------------
// PlayClipNode — wraps a single cd::anim::SkinnedClip.
//
// `loop` controls how the clip's local time is derived from the graph
// time `t`:
//   * loop=true  -> clip_t = fmod(t * speed, clip.duration())
//   * loop=false -> clip_t = clamp(t * speed, 0, clip.duration())
//
// A null clip yields bind-pose (so a freshly-constructed graph node is
// safe to tick before assets land).
// -----------------------------------------------------------------------------
class PlayClipNode final : public AnimNode
{
public:
    PlayClipNode() = default;

    explicit PlayClipNode(const cd::anim::SkinnedClip* clip,
                          bool loop = true,
                          float speed = 1.0F) noexcept
        : clip_(clip)
        , loop_(loop)
        , speed_(speed)
    {
    }

    void set_clip(const cd::anim::SkinnedClip* clip) noexcept { clip_ = clip; }
    [[nodiscard]] const cd::anim::SkinnedClip* clip() const noexcept { return clip_; }

    void set_loop(bool loop) noexcept { loop_ = loop; }
    [[nodiscard]] bool loop() const noexcept { return loop_; }

    void set_speed(float s) noexcept { speed_ = s; }
    [[nodiscard]] float speed() const noexcept { return speed_; }

    [[nodiscard]] cd::anim::Pose tick(float t,
                                      const Blackboard& bb,
                                      const cd::anim::Skeleton& skel) override;

private:
    const cd::anim::SkinnedClip* clip_ { nullptr };
    bool  loop_ { true };
    float speed_ { 1.0F };
};

// -----------------------------------------------------------------------------
// Blend1DNode — N inputs sorted by ascending threshold; param picks the
// bracketing pair and the LERP weight.
//
// Reads a single named parameter from the blackboard. Inputs MUST be
// inserted in ascending threshold order; `add_input` checks the
// invariant in debug builds (asserts via boolean return).
//
// Semantics (matches Mecanim 1D Blend Tree):
//   * param <= first.threshold  -> first input
//   * param >= last.threshold   -> last input
//   * otherwise                 -> LERP between bracketing pair
// -----------------------------------------------------------------------------
class Blend1DNode final : public AnimNode
{
public:
    /// Per-input entry. Sorted ascending by `threshold` (caller's job).
    struct Input
    {
        float threshold { 0.0F };
        std::unique_ptr<AnimNode> node;
    };

    Blend1DNode() = default;

    explicit Blend1DNode(std::string param_key) noexcept
        : param_key_(std::move(param_key))
    {
    }

    void set_param_key(std::string key) noexcept { param_key_ = std::move(key); }
    [[nodiscard]] std::string_view param_key() const noexcept { return param_key_; }

    /// Append an input. Returns false if `threshold` is not strictly
    /// greater than the last entry's threshold (graph stays valid even
    /// on rejection — the caller's add is simply ignored).
    bool add_input(float threshold, std::unique_ptr<AnimNode> node);

    [[nodiscard]] std::size_t input_count() const noexcept { return inputs_.size(); }

    [[nodiscard]] cd::anim::Pose tick(float t,
                                      const Blackboard& bb,
                                      const cd::anim::Skeleton& skel) override;

private:
    std::string param_key_ {};
    std::vector<Input> inputs_ {};
};

// -----------------------------------------------------------------------------
// Blend2DNode — Cartesian 4-corner bilinear blend.
//
// Reads two named parameters (x, y) from the blackboard. The four
// inputs occupy the corners of a unit-square parameter region defined
// by [min_x, max_x] x [min_y, max_y]. The blend weights are computed
// via standard bilinear interpolation:
//
//   u = saturate((x - min_x) / (max_x - min_x))
//   v = saturate((y - min_y) / (max_y - min_y))
//   P = (1-u)(1-v)*P00 + u(1-v)*P10 + (1-u)v*P01 + u*v*P11
//
// Internally implemented as two 1D blends along y followed by a 1D blend
// along x — keeps the pose-allocation count to 4 (vs 4 simultaneous
// blends) and is bitwise-equivalent to the closed-form weight sum.
//
// Mirrors Mecanim's "2D Freeform Cartesian" with the corners pinned to
// a rectangular grid (the most common use case: speed-on-x, turn-on-y).
// -----------------------------------------------------------------------------
class Blend2DNode final : public AnimNode
{
public:
    Blend2DNode() = default;

    Blend2DNode(std::string x_key, std::string y_key) noexcept
        : x_key_(std::move(x_key))
        , y_key_(std::move(y_key))
    {
    }

    /// Bounds of the parameter rectangle. Defaults to [0,1] x [0,1].
    void set_bounds(float min_x, float max_x, float min_y, float max_y) noexcept
    {
        min_x_ = min_x; max_x_ = max_x;
        min_y_ = min_y; max_y_ = max_y;
    }

    void set_x_key(std::string k) noexcept { x_key_ = std::move(k); }
    void set_y_key(std::string k) noexcept { y_key_ = std::move(k); }

    [[nodiscard]] std::string_view x_key() const noexcept { return x_key_; }
    [[nodiscard]] std::string_view y_key() const noexcept { return y_key_; }

    /// Set one of the four corners (0 -> (min_x,min_y), 1 -> (max_x,min_y),
    /// 2 -> (min_x,max_y), 3 -> (max_x,max_y)). Setting a corner to
    /// nullptr is legal (yields bind-pose for that corner).
    void set_corner(std::size_t i, std::unique_ptr<AnimNode> node);

    [[nodiscard]] AnimNode* corner(std::size_t i) const noexcept
    {
        return i < 4 ? corners_[i].get() : nullptr;
    }

    [[nodiscard]] cd::anim::Pose tick(float t,
                                      const Blackboard& bb,
                                      const cd::anim::Skeleton& skel) override;

private:
    std::string x_key_ {};
    std::string y_key_ {};
    float min_x_ { 0.0F }, max_x_ { 1.0F };
    float min_y_ { 0.0F }, max_y_ { 1.0F };
    std::array<std::unique_ptr<AnimNode>, 4> corners_ {};
};

// -----------------------------------------------------------------------------
// StateMachineNode — predicate-driven transitions between owned child
// AnimNodes.
//
// Each state references a child node by index. Transitions carry a
// predicate `bool(const Blackboard&, float elapsed_in_state)`; the
// first matching predicate wins per tick. Blending is OPTIONAL — when
// `blend_duration > 0` the FSM lerps the previous state's last sampled
// pose toward the new state's current sample over that many seconds.
//
// Compared to cd::anim::AnimStateMachine (which already does state ->
// clip blending end-to-end), this node is one rung higher: it can
// transition between ANY AnimNode subtree, including nested blend
// trees and other FSMs. That nesting is what makes a graph a graph.
// -----------------------------------------------------------------------------
class StateMachineNode final : public AnimNode
{
public:
    using Predicate = std::function<bool(const Blackboard&, float elapsed_in_state)>;

    struct State
    {
        std::string name;
        std::unique_ptr<AnimNode> node;
    };

    struct Transition
    {
        std::size_t from { 0 };
        std::size_t to   { 0 };
        Predicate   condition {};
        float       blend_duration { 0.0F };
    };

    StateMachineNode() = default;

    /// Register a state. Returns the state index; the first state
    /// added is automatically set as the initial state.
    std::size_t add_state(std::string name, std::unique_ptr<AnimNode> node);

    /// Register a transition. Returns false if either index is out of
    /// range or if `condition` is empty (a transition without a
    /// predicate would fire every tick, which is a programming error).
    bool add_transition(std::size_t from,
                        std::size_t to,
                        Predicate condition,
                        float blend_duration = 0.0F);

    /// Override the starting state. Returns false if `index` is out of
    /// range. The pose is reset to whatever the new state samples on
    /// the next tick.
    bool set_initial_state(std::size_t index);

    [[nodiscard]] std::size_t state_count() const noexcept { return states_.size(); }

    [[nodiscard]] std::size_t current_state_index() const noexcept { return current_; }

    [[nodiscard]] std::string_view current_state_name() const
    {
        return states_.empty() ? std::string_view {}
                               : std::string_view { states_[current_].name };
    }

    [[nodiscard]] bool is_blending() const noexcept { return blending_; }

    [[nodiscard]] cd::anim::Pose tick(float t,
                                      const Blackboard& bb,
                                      const cd::anim::Skeleton& skel) override;

private:
    std::vector<State>      states_ {};
    std::vector<Transition> transitions_ {};

    std::size_t current_ { 0 };
    bool        has_current_ { false };

    // Per-state elapsed time, advanced from the graph time delta.
    float       elapsed_in_state_ { 0.0F };
    float       last_t_ { 0.0F };
    bool        time_seeded_ { false };

    // Blend bookkeeping.
    bool        blending_ { false };
    float       blend_time_ { 0.0F };
    float       blend_duration_ { 0.0F };
    cd::anim::Pose blend_from_pose_ {};
};

// -----------------------------------------------------------------------------
// AnimGraph — wrapper owning a single root AnimNode + the parameter
// blackboard.
//
// `evaluate(skel, t)` is the per-frame entry point. The graph does not
// remember `t`; callers drive the time axis (typically with the engine's
// monotonic clock or per-character local time). The blackboard persists
// across calls so parameter values set during one tick remain visible
// to the next.
// -----------------------------------------------------------------------------
class AnimGraph
{
public:
    AnimGraph() = default;

    explicit AnimGraph(std::unique_ptr<AnimNode> root) noexcept
        : root_(std::move(root))
    {
    }

    void set_root(std::unique_ptr<AnimNode> root) noexcept { root_ = std::move(root); }

    [[nodiscard]] AnimNode* root() const noexcept { return root_.get(); }

    /// Set / get / has / clear blackboard parameters.
    void  set_param(std::string_view key, float value)            { bb_.set(key, value); }
    [[nodiscard]] float get_param(std::string_view key, float fallback = 0.0F) const
    {
        return bb_.get(key, fallback);
    }
    [[nodiscard]] bool  has_param(std::string_view key) const     { return bb_.has(key); }
    void  clear_params() noexcept                                  { bb_.clear(); }

    /// Direct (mutable) blackboard access for callers that want to set
    /// many parameters in a tight loop.
    [[nodiscard]] Blackboard&       blackboard() noexcept       { return bb_; }
    [[nodiscard]] const Blackboard& blackboard() const noexcept { return bb_; }

    /// Sample the root at graph time `t`. Returns bind-pose if the
    /// root is null (graceful default).
    [[nodiscard]] cd::anim::Pose evaluate(const cd::anim::Skeleton& skel, float t);

private:
    std::unique_ptr<AnimNode> root_ {};
    Blackboard bb_ {};
};

}  // namespace cd::game::anim_graph
