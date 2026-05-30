// =============================================================================
// CHROMODYNAMIC - cd/game/fsm/Fsm.hpp
// Phase 472 (G2.1) - Hierarchical Finite State Machine with regions + history.
//
// Design references:
//   * Harel, David. "Statecharts: A Visual Formalism for Complex Systems"
//     Science of Computer Programming 8 (3): 231-274, 1987.
//   * OMG UML 2.5.1 - State Machines chapter (regions, history, transitions).
//
// Key types:
//   * State<TContext>          : leaf or composite state with on_enter / on_exit
//                                / on_update callbacks (CRTP-free, pay-as-you-go
//                                std::function hooks so derivation is optional).
//   * StateMachine<TContext>   : flat FSM - add_state, add_transition, tick,
//                                current_state, set_state.
//   * HierarchicalFsm<TContext>: nested regions + Harel-style shallow / deep
//                                history pseudo-states. Re-entering a parent
//                                with shallow history restores the last active
//                                direct child; deep history restores the entire
//                                active sub-configuration.
//
// Library boundary (CLAUDE.md S7): single dependency on cd::core for
// Defines / NODISCARD; no global state; no I/O; no exceptions thrown by the
// FSM itself (predicates / callbacks own their own error policy).
// Header-only (templates) - .cpp exists only to anchor the translation unit
// for ABI symmetry with the other libraries.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cd::game::fsm
{

// -----------------------------------------------------------------------------
// StateId - opaque integer handle; unique within a single machine instance.
// Reserved value kInvalidStateId marks "no state" (e.g. before initial entry).
// -----------------------------------------------------------------------------
using StateId = std::uint32_t;

inline constexpr StateId kInvalidStateId = static_cast<StateId>(-1);

// -----------------------------------------------------------------------------
// HistoryKind - per-state history policy used by HierarchicalFsm.
//
//   kNone    : on re-entry the parent restarts at its declared initial child.
//   kShallow : on re-entry the parent resumes at the last active direct child;
//              that child itself starts from its own initial sub-state.
//   kDeep    : on re-entry the parent resumes at the deepest previously active
//              sub-state across all nested regions.
// -----------------------------------------------------------------------------
enum class HistoryKind : std::uint8_t
{
    kNone    = 0,
    kShallow = 1,
    kDeep    = 2,
};

// -----------------------------------------------------------------------------
// State<TContext>
//
// Leaf or composite state node. Hooks are stored as std::function so callers
// may use lambdas, free functions, or member functions without subclassing.
// Hooks are optional - any unset hook is a no-op.
//
// on_update receives the elapsed wall-clock dt (seconds, float) routed in from
// StateMachine::tick. Hooks must not call back into the owning machine's tick
// (re-entrant tick is UB); a hook MAY call set_state() to schedule an external
// jump after returning - the effect is immediate, not queued.
// -----------------------------------------------------------------------------
template <class TContext>
class State
{
public:
    using EnterFn  = std::function<void(TContext&)>;
    using ExitFn   = std::function<void(TContext&)>;
    using UpdateFn = std::function<void(TContext&, float /*dt*/)>;

    State() = default;

    explicit State(std::string name) noexcept : name_(std::move(name)) {}

    virtual ~State() = default;

    State(const State&)            = delete;
    State& operator=(const State&) = delete;
    State(State&&)                 = default;
    State& operator=(State&&)      = default;

    CD_NODISCARD const std::string& name() const noexcept { return name_; }

    void set_on_enter(EnterFn fn) { on_enter_ = std::move(fn); }
    void set_on_exit(ExitFn fn) { on_exit_ = std::move(fn); }
    void set_on_update(UpdateFn fn) { on_update_ = std::move(fn); }

    virtual void on_enter(TContext& ctx)
    {
        if (on_enter_)
        {
            on_enter_(ctx);
        }
    }

    virtual void on_exit(TContext& ctx)
    {
        if (on_exit_)
        {
            on_exit_(ctx);
        }
    }

    virtual void on_update(TContext& ctx, float dt)
    {
        if (on_update_)
        {
            on_update_(ctx, dt);
        }
    }

private:
    std::string name_;
    EnterFn     on_enter_ {};
    ExitFn      on_exit_ {};
    UpdateFn    on_update_ {};
};

// -----------------------------------------------------------------------------
// StateMachine<TContext>
//
// Flat FSM. Transitions are predicate-driven: at each tick the machine walks
// the transition list out of the current state and the first predicate that
// returns true fires the transition. Self-transitions are allowed - they run
// on_exit then on_enter on the same state.
//
// Lifecycle:
//   1. construct, add states + transitions; the first add_state is the default
//      initial state (override with set_initial_state).
//   2. start(ctx) - explicit initial on_enter dispatch. tick(ctx,dt) is a
//      no-op until start has been called.
//   3. tick(ctx, dt) - evaluate transitions; if one fires, on_exit / on_enter
//      run for the source / target; otherwise on_update of the current state.
//   4. set_state(ctx, id) - immediate jump (on_exit current, on_enter target);
//      bypasses predicates.
//
// Thread-safety: NOT thread-safe. One machine per owning thread; callers that
// need shared access wrap externally.
// -----------------------------------------------------------------------------
template <class TContext>
class StateMachine
{
public:
    using Predicate = std::function<bool(const TContext&)>;

    struct Transition
    {
        StateId   from {kInvalidStateId};
        StateId   to {kInvalidStateId};
        Predicate when {};
    };

    StateMachine() = default;

    StateMachine(const StateMachine&)            = delete;
    StateMachine& operator=(const StateMachine&) = delete;
    StateMachine(StateMachine&&)                 = default;
    StateMachine& operator=(StateMachine&&)      = default;

    virtual ~StateMachine() = default;

    // -- structure ----------------------------------------------------------
    StateId add_state(std::unique_ptr<State<TContext>> state, bool mark_initial = false)
    {
        const StateId id = static_cast<StateId>(states_.size());
        states_.push_back(std::move(state));
        if (mark_initial || initial_ == kInvalidStateId)
        {
            initial_ = id;
        }
        return id;
    }

    void add_transition(StateId from, StateId to, Predicate when)
    {
        transitions_.push_back(Transition {from, to, std::move(when)});
    }

    void set_initial_state(StateId id) noexcept { initial_ = id; }

    // -- runtime ------------------------------------------------------------
    void start(TContext& ctx)
    {
        if (started_)
        {
            return;
        }
        if (initial_ == kInvalidStateId)
        {
            started_ = true;
            return;
        }
        current_ = initial_;
        started_ = true;
        get(current_).on_enter(ctx);
    }

    // Evaluate predicates first, then on_update if none fired. Returns true
    // iff a transition fired this tick.
    bool tick(TContext& ctx, float dt)
    {
        if (!started_ || current_ == kInvalidStateId)
        {
            return false;
        }
        for (const auto& t : transitions_)
        {
            if (t.from != current_)
            {
                continue;
            }
            if (t.when && t.when(ctx))
            {
                fire_transition(ctx, t.to);
                return true;
            }
        }
        get(current_).on_update(ctx, dt);
        return false;
    }

    // External immediate jump - bypasses predicate evaluation.
    void set_state(TContext& ctx, StateId to)
    {
        if (!started_)
        {
            current_ = to;
            started_ = true;
            get(current_).on_enter(ctx);
            return;
        }
        fire_transition(ctx, to);
    }

    CD_NODISCARD StateId     current_state() const noexcept { return current_; }
    CD_NODISCARD StateId     initial_state() const noexcept { return initial_; }
    CD_NODISCARD std::size_t state_count() const noexcept { return states_.size(); }
    CD_NODISCARD bool        started() const noexcept { return started_; }

    CD_NODISCARD const State<TContext>& state(StateId id) const { return *states_.at(id); }
    CD_NODISCARD State<TContext>&       state(StateId id) { return *states_.at(id); }

protected:
    // Visible to HierarchicalFsm so it can intercept transitions.
    virtual void fire_transition(TContext& ctx, StateId to)
    {
        State<TContext>& cur = get(current_);
        cur.on_exit(ctx);
        current_ = to;
        get(current_).on_enter(ctx);
    }

    State<TContext>& get(StateId id) { return *states_.at(id); }

    // Used by HierarchicalFsm to mark a sub-region as "never started" after
    // its parent state exits - on next entry, start() will fire on_enter
    // again. Does NOT touch states_ / transitions_ / initial_.
    void runtime_reset_for_history() noexcept
    {
        started_ = false;
        current_ = kInvalidStateId;
    }

private:
    std::vector<std::unique_ptr<State<TContext>>> states_ {};
    std::vector<Transition>                       transitions_ {};
    StateId                                       initial_ {kInvalidStateId};
    StateId                                       current_ {kInvalidStateId};
    bool                                          started_ {false};
};

// -----------------------------------------------------------------------------
// HierarchicalFsm<TContext>
//
// Compositional state machine. Each state may own zero or more *regions*; a
// region is itself a HierarchicalFsm<TContext>, allowing arbitrary nesting.
// Per-state history policy controls how a region is re-entered:
//
//   kNone    : restart from the declared initial state of every nested region.
//   kShallow : resume at the last active direct child only; that child then
//              uses its own (kNone) reset behaviour for its grandchildren.
//   kDeep    : resume the entire previously active configuration recursively.
//
// All transitions in HierarchicalFsm are still predicate-driven. tick_h
// drives the outer machine and then recursively ticks each active region.
// -----------------------------------------------------------------------------
template <class TContext>
class HierarchicalFsm : public StateMachine<TContext>
{
public:
    using Base = StateMachine<TContext>;

    HierarchicalFsm() = default;
    ~HierarchicalFsm() override = default;

    HierarchicalFsm(const HierarchicalFsm&)            = delete;
    HierarchicalFsm& operator=(const HierarchicalFsm&) = delete;
    HierarchicalFsm(HierarchicalFsm&&)                 = default;
    HierarchicalFsm& operator=(HierarchicalFsm&&)      = default;

    // Attach a nested region to a parent state. A state may own multiple
    // orthogonal regions (UML "and-states"); each is ticked independently.
    // Returns a non-owning pointer for further configuration.
    HierarchicalFsm<TContext>* add_region(StateId parent,
                                          std::unique_ptr<HierarchicalFsm<TContext>> region)
    {
        auto& slot = regions_[parent];
        slot.push_back(std::move(region));
        return slot.back().get();
    }

    void set_history(StateId parent, HistoryKind kind) { history_[parent] = kind; }

    CD_NODISCARD HistoryKind history(StateId parent) const noexcept
    {
        const auto it = history_.find(parent);
        return it == history_.end() ? HistoryKind::kNone : it->second;
    }

    CD_NODISCARD bool has_regions(StateId parent) const noexcept
    {
        return regions_.find(parent) != regions_.end();
    }

    // Active leaf - deepest currently active state across the configuration.
    // For a flat machine equals current_state(); for nested, recurses into
    // the first active region of the active state.
    CD_NODISCARD StateId active_leaf() const noexcept
    {
        const StateId cur = this->current_state();
        if (cur == kInvalidStateId)
        {
            return kInvalidStateId;
        }
        const auto it = regions_.find(cur);
        if (it == regions_.end() || it->second.empty())
        {
            return cur;
        }
        return it->second.front()->active_leaf();
    }

    // Override of start - starts the outer machine, then enters regions of
    // the initial state.
    void start_h(TContext& ctx)
    {
        this->start(ctx);
        if (this->current_state() != kInvalidStateId)
        {
            enter_regions(ctx, this->current_state());
        }
    }

    // Override of tick - evaluates parent transitions first; if none fired,
    // recursively ticks active regions of the current state.
    bool tick_h(TContext& ctx, float dt)
    {
        const bool fired  = this->tick(ctx, dt);
        const StateId cur = this->current_state();
        if (!fired && cur != kInvalidStateId)
        {
            const auto it = regions_.find(cur);
            if (it != regions_.end())
            {
                for (const auto& region : it->second)
                {
                    region->tick_h(ctx, dt);
                }
            }
        }
        return fired;
    }

protected:
    // Intercept transitions to (a) tear down the old state's regions and
    // (b) bring up the new state's regions honouring history policy.
    void fire_transition(TContext& ctx, StateId to) override
    {
        const StateId from = this->current_state();
        if (from != kInvalidStateId)
        {
            snapshot_history(from);
            exit_regions(ctx, from);
        }
        Base::fire_transition(ctx, to);
        enter_regions(ctx, to);
    }

private:
    struct HistorySnapshot
    {
        StateId              last_direct {kInvalidStateId};
        std::vector<StateId> deep_path {};
    };

    // For each region under `parent`, save its current direct child + deep
    // path so a future re-entry with kShallow / kDeep can restore it.
    void snapshot_history(StateId parent)
    {
        const HistoryKind kind = history(parent);
        if (kind == HistoryKind::kNone)
        {
            history_snapshot_.erase(parent);
            return;
        }
        const auto it = regions_.find(parent);
        if (it == regions_.end())
        {
            return;
        }
        auto& snap = history_snapshot_[parent];
        snap.clear();
        snap.reserve(it->second.size());
        for (const auto& region : it->second)
        {
            HistorySnapshot s;
            s.last_direct = region->current_state();
            if (kind == HistoryKind::kDeep)
            {
                region->capture_deep(s.deep_path);
            }
            snap.push_back(std::move(s));
        }
    }

    // Recursively capture the active configuration (used by kDeep).
    void capture_deep(std::vector<StateId>& path) const
    {
        const StateId cur = this->current_state();
        if (cur == kInvalidStateId)
        {
            return;
        }
        path.push_back(cur);
        const auto it = regions_.find(cur);
        if (it == regions_.end() || it->second.empty())
        {
            return;
        }
        it->second.front()->capture_deep(path);
    }

    void exit_regions(TContext& ctx, StateId parent)
    {
        const auto it = regions_.find(parent);
        if (it == regions_.end())
        {
            return;
        }
        for (const auto& region : it->second)
        {
            region->exit_recursive(ctx);
        }
    }

    // Tear down the entire active sub-tree of this region and reset its
    // runtime so a future start()/start_at() will re-fire on_enter.
    void exit_recursive(TContext& ctx)
    {
        const StateId cur = this->current_state();
        if (cur == kInvalidStateId)
        {
            return;
        }
        const auto it = regions_.find(cur);
        if (it != regions_.end())
        {
            for (const auto& region : it->second)
            {
                region->exit_recursive(ctx);
            }
        }
        this->state(cur).on_exit(ctx);
        this->runtime_reset_for_history();
    }

    void enter_regions(TContext& ctx, StateId parent)
    {
        const auto it = regions_.find(parent);
        if (it == regions_.end())
        {
            return;
        }
        const HistoryKind kind = history(parent);
        const auto snap_it     = history_snapshot_.find(parent);
        const bool has_snap = (snap_it != history_snapshot_.end()
                               && snap_it->second.size() == it->second.size());

        for (std::size_t i = 0; i < it->second.size(); ++i)
        {
            HierarchicalFsm<TContext>& region = *it->second[i];
            if (has_snap && (kind == HistoryKind::kShallow || kind == HistoryKind::kDeep))
            {
                const HistorySnapshot& s = snap_it->second[i];
                if (kind == HistoryKind::kDeep && !s.deep_path.empty())
                {
                    region.enter_deep_path(ctx, s.deep_path, 0);
                }
                else if (s.last_direct != kInvalidStateId)
                {
                    region.start_at(ctx, s.last_direct);
                    region.enter_regions(ctx, region.current_state());
                }
                else
                {
                    region.start(ctx);
                    region.enter_regions(ctx, region.current_state());
                }
            }
            else
            {
                region.start(ctx);
                region.enter_regions(ctx, region.current_state());
            }
        }
    }

    // Start this region with `id` as its current state (used by history
    // restore - both shallow and deep).
    void start_at(TContext& ctx, StateId id)
    {
        this->set_initial_state(id);
        this->start(ctx);
    }

    // Walk `path` (a deep-history capture) and bring up every level. Only
    // the first orthogonal region at each level participates in the path;
    // other regions get a clean start.
    void enter_deep_path(TContext& ctx, const std::vector<StateId>& path, std::size_t idx)
    {
        if (idx >= path.size())
        {
            return;
        }
        start_at(ctx, path[idx]);
        const auto it = regions_.find(path[idx]);
        if (it == regions_.end() || it->second.empty())
        {
            return;
        }
        for (std::size_t r = 0; r < it->second.size(); ++r)
        {
            if (r == 0)
            {
                it->second[r]->enter_deep_path(ctx, path, idx + 1);
            }
            else
            {
                it->second[r]->start(ctx);
                it->second[r]->enter_regions(ctx, it->second[r]->current_state());
            }
        }
    }

    std::unordered_map<StateId, std::vector<std::unique_ptr<HierarchicalFsm<TContext>>>>
                                                              regions_ {};
    std::unordered_map<StateId, HistoryKind>                  history_ {};
    std::unordered_map<StateId, std::vector<HistorySnapshot>> history_snapshot_ {};
};

}  // namespace cd::game::fsm
