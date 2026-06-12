// =============================================================================
// CHROMODYNAMIC — cd/game/ai_bt/BehaviorTree.cpp
// Phase 473 — Composite / Decorator / Tree tick implementations.
//
// References:
//   * Champandard, "Behavior Trees for Next-Gen Game AI", AIGameDev 2008.
//   * Marzinotto et al., "Towards a Unified Behavior Trees Framework for
//     Robot Control", ICRA 2014.
//   * Colledanchise & Ögren, "Behavior Trees in Robotics and AI", CRC Press
//     2018 (esp. §2 for Sequence / Selector / Parallel semantics).
// =============================================================================
#include <cd/game/ai_bt/BehaviorTree.hpp>

namespace cd::game::ai_bt
{

// -----------------------------------------------------------------------------
// SequenceNode — tick children in cursor order, short-circuiting on the first
// kFailure / kRunning result. Cursor advances past every kSuccess so that the
// next tick resumes from the still-running (or next) child.
// -----------------------------------------------------------------------------
Status SequenceNode::tick(Blackboard& bb)
{
    while (cursor_ < children_.size())
    {
        const Status s = children_[cursor_]->tick(bb);
        if (s == Status::kRunning) { return Status::kRunning; }
        if (s == Status::kFailure)
        {
            cursor_ = 0;
            // Reset every child so a re-entry starts cleanly (e.g. a
            // running grandchild's cursor must drop).
            for (auto& c : children_) { (*c).reset(); }
            return Status::kFailure;
        }
        // kSuccess: advance and try the next child.
        ++cursor_;
    }
    // Every child succeeded.
    cursor_ = 0;
    for (auto& c : children_) { (*c).reset(); }
    return Status::kSuccess;
}

// -----------------------------------------------------------------------------
// SelectorNode — dual of SequenceNode: short-circuit on first kSuccess, fall
// through on kFailure, propagate kRunning.
// -----------------------------------------------------------------------------
Status SelectorNode::tick(Blackboard& bb)
{
    while (cursor_ < children_.size())
    {
        const Status s = children_[cursor_]->tick(bb);
        if (s == Status::kRunning) { return Status::kRunning; }
        if (s == Status::kSuccess)
        {
            cursor_ = 0;
            for (auto& c : children_) { (*c).reset(); }
            return Status::kSuccess;
        }
        // kFailure: try the next alternative.
        ++cursor_;
    }
    // Every child failed.
    cursor_ = 0;
    for (auto& c : children_) { (*c).reset(); }
    return Status::kFailure;
}

// -----------------------------------------------------------------------------
// ParallelNode — tick every child every call; aggregate via thresholds.
//
// `success_threshold_ == 0` means "all children" (Colledanchise & Ögren's
// "Parallel-Success-All" policy). `failure_threshold_` defaults to 1 so the
// first failure aborts (the standard "Parallel-AND" policy).
// -----------------------------------------------------------------------------
Status ParallelNode::tick(Blackboard& bb)
{
    if (children_.empty())
    {
        // An empty parallel is vacuously successful per the cited reference;
        // matches the convention used by BehaviorTree.CPP and py_trees.
        return Status::kSuccess;
    }

    std::size_t success_count = 0;
    std::size_t failure_count = 0;

    for (auto& child : children_)
    {
        const Status s = child->tick(bb);
        if (s == Status::kSuccess) { ++success_count; }
        else if (s == Status::kFailure) { ++failure_count; }
    }

    const std::size_t success_target =
        success_threshold_ == 0 ? children_.size() : success_threshold_;

    if (failure_count >= failure_threshold_)
    {
        for (auto& c : children_) { (*c).reset(); }
        return Status::kFailure;
    }
    if (success_count >= success_target)
    {
        for (auto& c : children_) { (*c).reset(); }
        return Status::kSuccess;
    }
    return Status::kRunning;
}

// -----------------------------------------------------------------------------
// InverterNode — flips terminal status; kRunning passes through.
// -----------------------------------------------------------------------------
Status InverterNode::tick(Blackboard& bb)
{
    if (!child_) { return Status::kFailure; }
    switch (child_->tick(bb))
    {
        case Status::kSuccess: return Status::kFailure;
        case Status::kFailure: return Status::kSuccess;
        case Status::kRunning: return Status::kRunning;
    }
    return Status::kFailure;
}

// -----------------------------------------------------------------------------
// RepeaterNode — tick the child up to `count_` times per *invocation*.
//
// We loop synchronously inside one tick because the canonical "repeat N
// times" decorator is a control-flow construct, not a time-spanning
// behaviour. If a child reports kRunning, we propagate immediately so the
// child's own time-spanning state is preserved; on the next tick we resume
// from the same iteration count.
// -----------------------------------------------------------------------------
Status RepeaterNode::tick(Blackboard& bb)
{
    if (!child_) { return Status::kFailure; }

    while (count_ == 0 || iter_ < count_)
    {
        const Status s = child_->tick(bb);
        if (s == Status::kRunning) { return Status::kRunning; }
        last_ = s;
        // Reset the child between iterations so it starts fresh.
        (*child_).reset();
        ++iter_;
        // Guard against infinite tick-time loops when count_==0: a
        // forever-repeater must yield after each finished iteration so
        // sibling branches in a Parallel get airtime.
        if (count_ == 0) { return Status::kRunning; }
    }

    const Status result = last_;
    iter_ = 0;
    last_ = Status::kFailure;
    return result;
}

// -----------------------------------------------------------------------------
// UntilSuccessNode — re-tick on kFailure; report kSuccess the moment the
// child succeeds. Propagates kRunning so other parallel branches breathe.
// -----------------------------------------------------------------------------
Status UntilSuccessNode::tick(Blackboard& bb)
{
    if (!child_) { return Status::kFailure; }
    const Status s = child_->tick(bb);
    if (s == Status::kSuccess) { return Status::kSuccess; }
    if (s == Status::kRunning) { return Status::kRunning; }
    // kFailure: reset and report kRunning so the caller re-enters next tick.
    (*child_).reset();
    return Status::kRunning;
}

// -----------------------------------------------------------------------------
// BehaviorTree — publish `dt` into the blackboard then delegate to root.
// -----------------------------------------------------------------------------
Status BehaviorTree::tick(Blackboard& bb, float dt)
{
    bb.set_float("dt", dt);
    if (!root_) { return Status::kFailure; }
    return root_->tick(bb);
}

}  // namespace cd::game::ai_bt
