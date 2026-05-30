// =============================================================================
// CHROMODYNAMIC — cd/game/anim_graph/AnimGraph.cpp
// Phase 481 — node tick implementations + graph evaluate entry point.
//
// References:
//   * Unity Animator + 1D / 2D Blend Tree weight formulas.
//   * production AnimGraph evaluator (Persson, GDC 2010).
//   * Mecanim 2D Freeform Cartesian (Mecanim docs + Mecanim animator manual).
//   * cd::anim::AnimStateMachine for the per-state elapsed-time bookkeeping
//     pattern reused below.
// =============================================================================
#include <cd/game/anim_graph/AnimGraph.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace cd::game::anim_graph
{

// -----------------------------------------------------------------------------
// PlayClipNode — sample the wrapped clip at the (possibly looped / clamped)
// graph time `t`. A null clip yields bind-pose so freshly-constructed
// nodes are always safe to tick.
// -----------------------------------------------------------------------------
cd::anim::Pose PlayClipNode::tick(float t,
                                  const Blackboard& /*bb*/,
                                  const cd::anim::Skeleton& skel)
{
    cd::anim::Pose out = cd::anim::Pose::bind_pose(skel);
    if (clip_ == nullptr) { return out; }

    const float scaled = t * speed_;
    float clip_t = scaled;

    const float dur = clip_->duration();
    if (dur > 0.0F)
    {
        if (loop_)
        {
            // std::fmod preserves sign; wrap into [0, dur).
            clip_t = std::fmod(scaled, dur);
            if (clip_t < 0.0F) { clip_t += dur; }
        }
        else
        {
            clip_t = std::clamp(scaled, 0.0F, dur);
        }
    }

    clip_->sample(clip_t, out);
    return out;
}

// -----------------------------------------------------------------------------
// Blend1DNode::add_input — enforce strict-ascending threshold order so
// the per-tick bracket search is a trivial linear scan (and so the
// weight expression is well-defined at the boundaries).
// -----------------------------------------------------------------------------
bool Blend1DNode::add_input(float threshold, std::unique_ptr<AnimNode> node)
{
    if (!node) { return false; }
    if (!inputs_.empty() && threshold <= inputs_.back().threshold)
    {
        // Reject out-of-order or duplicate thresholds — the graph stays
        // valid and the caller's add is ignored.
        return false;
    }
    inputs_.push_back(Input { threshold, std::move(node) });
    return true;
}

// -----------------------------------------------------------------------------
// Blend1DNode — clamp param to [first, last] threshold, find the
// bracketing pair, LERP via cd::anim::blend_pose. Single-input case
// collapses to a passthrough; empty case yields bind-pose.
// -----------------------------------------------------------------------------
cd::anim::Pose Blend1DNode::tick(float t,
                                 const Blackboard& bb,
                                 const cd::anim::Skeleton& skel)
{
    if (inputs_.empty())
    {
        return cd::anim::Pose::bind_pose(skel);
    }
    if (inputs_.size() == 1)
    {
        return inputs_[0].node->tick(t, bb, skel);
    }

    const float param = bb.get(param_key_, inputs_.front().threshold);

    // Boundary clamps — anything outside the range pins to the endpoint.
    if (param <= inputs_.front().threshold)
    {
        return inputs_.front().node->tick(t, bb, skel);
    }
    if (param >= inputs_.back().threshold)
    {
        return inputs_.back().node->tick(t, bb, skel);
    }

    // Find the bracket [i, i+1) such that thr[i] <= param < thr[i+1].
    std::size_t i = 0;
    for (; i + 1 < inputs_.size(); ++i)
    {
        if (param < inputs_[i + 1].threshold) { break; }
    }
    const std::size_t j = i + 1;

    const float t0 = inputs_[i].threshold;
    const float t1 = inputs_[j].threshold;
    const float denom = t1 - t0;
    const float w = denom > 0.0F ? (param - t0) / denom : 0.0F;

    cd::anim::Pose pa = inputs_[i].node->tick(t, bb, skel);
    cd::anim::Pose pb = inputs_[j].node->tick(t, bb, skel);
    cd::anim::Pose out = cd::anim::Pose::bind_pose(skel);
    cd::anim::blend_pose(pa, pb, w, out);
    return out;
}

// -----------------------------------------------------------------------------
// Blend2DNode::set_corner — index out of range is silently ignored
// (defensive; mirrors the Blend1DNode add-rejection pattern).
// -----------------------------------------------------------------------------
void Blend2DNode::set_corner(std::size_t i, std::unique_ptr<AnimNode> node)
{
    if (i < corners_.size())
    {
        corners_[i] = std::move(node);
    }
}

// -----------------------------------------------------------------------------
// Blend2DNode — bilinear blend via two y-axis 1D blends + one x-axis
// 1D blend. Null corners fall back to bind-pose. Saturated u, v keep
// the output well-defined outside the rectangle.
// -----------------------------------------------------------------------------
cd::anim::Pose Blend2DNode::tick(float t,
                                 const Blackboard& bb,
                                 const cd::anim::Skeleton& skel)
{
    const float x = bb.get(x_key_, min_x_);
    const float y = bb.get(y_key_, min_y_);

    const float dx = max_x_ - min_x_;
    const float dy = max_y_ - min_y_;
    const float u = dx > 0.0F ? std::clamp((x - min_x_) / dx, 0.0F, 1.0F) : 0.0F;
    const float v = dy > 0.0F ? std::clamp((y - min_y_) / dy, 0.0F, 1.0F) : 0.0F;

    auto tick_corner = [&](std::size_t i) {
        if (i < corners_.size() && corners_[i] != nullptr)
        {
            return corners_[i]->tick(t, bb, skel);
        }
        return cd::anim::Pose::bind_pose(skel);
    };

    // Corners: 0 = (min_x,min_y), 1 = (max_x,min_y),
    //          2 = (min_x,max_y), 3 = (max_x,max_y).
    const cd::anim::Pose p00 = tick_corner(0);
    const cd::anim::Pose p10 = tick_corner(1);
    const cd::anim::Pose p01 = tick_corner(2);
    const cd::anim::Pose p11 = tick_corner(3);

    // Blend along x at y=min_y (bottom row) and y=max_y (top row).
    cd::anim::Pose bottom = cd::anim::Pose::bind_pose(skel);
    cd::anim::Pose top    = cd::anim::Pose::bind_pose(skel);
    cd::anim::blend_pose(p00, p10, u, bottom);
    cd::anim::blend_pose(p01, p11, u, top);

    // Final blend along y.
    cd::anim::Pose out = cd::anim::Pose::bind_pose(skel);
    cd::anim::blend_pose(bottom, top, v, out);
    return out;
}

// -----------------------------------------------------------------------------
// StateMachineNode — register a state. First add seeds the initial state.
// -----------------------------------------------------------------------------
std::size_t StateMachineNode::add_state(std::string name, std::unique_ptr<AnimNode> node)
{
    states_.push_back(State { std::move(name), std::move(node) });
    if (!has_current_)
    {
        current_ = 0;
        has_current_ = true;
    }
    return states_.size() - 1;
}

// -----------------------------------------------------------------------------
// StateMachineNode — register a transition. Empty predicate / OOB
// indices are rejected so the per-tick loop can rely on the invariants.
// -----------------------------------------------------------------------------
bool StateMachineNode::add_transition(std::size_t from,
                                      std::size_t to,
                                      Predicate condition,
                                      float blend_duration)
{
    if (from >= states_.size() || to >= states_.size()) { return false; }
    if (!condition) { return false; }
    transitions_.push_back(Transition {
        from, to, std::move(condition), std::max(0.0F, blend_duration)
    });
    return true;
}

bool StateMachineNode::set_initial_state(std::size_t index)
{
    if (index >= states_.size()) { return false; }
    current_ = index;
    has_current_ = true;
    elapsed_in_state_ = 0.0F;
    blending_ = false;
    blend_time_ = 0.0F;
    time_seeded_ = false;
    return true;
}

// -----------------------------------------------------------------------------
// StateMachineNode — tick the FSM.
//
// dt is derived from successive `t` values supplied by the AnimGraph
// caller. `t` can scrub forward or backward — we honour the magnitude
// via std::fabs so a rewind still drives the blend / elapsed counters
// rather than going negative.
// -----------------------------------------------------------------------------
cd::anim::Pose StateMachineNode::tick(float t,
                                      const Blackboard& bb,
                                      const cd::anim::Skeleton& skel)
{
    if (states_.empty())
    {
        return cd::anim::Pose::bind_pose(skel);
    }

    // Compute dt from successive t values. The first tick seeds last_t_
    // without consuming dt so the first sample is reproducible.
    float dt = 0.0F;
    if (time_seeded_)
    {
        dt = std::fabs(t - last_t_);
    }
    last_t_ = t;
    time_seeded_ = true;

    elapsed_in_state_ += dt;

    // Sample the destination of the current frame BEFORE checking
    // transitions so we can capture `blend_from_pose_` from the
    // previous-state's last pose without re-ticking it. The order is:
    //   (1) Look at transitions; if one fires, snapshot the OUTGOING
    //       state's current pose as the blend source.
    //   (2) Tick the (possibly newly switched) current state to get the
    //       INCOMING pose.
    //   (3) If we're blending, lerp source -> incoming.

    if (!blending_)
    {
        for (const auto& tr : transitions_)
        {
            if (tr.from != current_) { continue; }
            if (!tr.condition) { continue; }
            if (tr.condition(bb, elapsed_in_state_))
            {
                // Snapshot the outgoing pose for the blend source.
                if (states_[current_].node)
                {
                    blend_from_pose_ = states_[current_].node->tick(t, bb, skel);
                }
                else
                {
                    blend_from_pose_ = cd::anim::Pose::bind_pose(skel);
                }
                current_ = tr.to;
                elapsed_in_state_ = 0.0F;
                blend_time_ = 0.0F;
                blend_duration_ = tr.blend_duration;
                blending_ = blend_duration_ > 0.0F;
                break;
            }
        }
    }

    // Tick the (possibly newly switched) current state.
    cd::anim::Pose incoming = states_[current_].node
                                  ? states_[current_].node->tick(t, bb, skel)
                                  : cd::anim::Pose::bind_pose(skel);

    if (!blending_)
    {
        return incoming;
    }

    blend_time_ += dt;
    const float w = blend_duration_ > 0.0F
                        ? std::clamp(blend_time_ / blend_duration_, 0.0F, 1.0F)
                        : 1.0F;

    cd::anim::Pose out = cd::anim::Pose::bind_pose(skel);
    cd::anim::blend_pose(blend_from_pose_, incoming, w, out);

    if (w >= 1.0F)
    {
        blending_ = false;
    }
    return out;
}

// -----------------------------------------------------------------------------
// AnimGraph::evaluate — entry point. Bind-pose fallback if the root is
// missing keeps callers crash-safe during asset hot-swap or graph
// construction.
// -----------------------------------------------------------------------------
cd::anim::Pose AnimGraph::evaluate(const cd::anim::Skeleton& skel, float t)
{
    if (!root_) { return cd::anim::Pose::bind_pose(skel); }
    return root_->tick(t, bb_, skel);
}

}  // namespace cd::game::anim_graph
