// =============================================================================
// CHROMODYNAMIC — cd/editor/EditHistory.hpp
// Phase 12.C / v0.28.0 — undo/redo command stack.
//
// Every mutation that happens through the editor flows through a
// command object. The command captures enough state to undo itself.
// Pushing a command runs its apply() once and remembers the inverse;
// undo() pops the most-recently-applied command and runs its
// revert(). redo() re-applies the topmost undone command.
//
// Design choices:
//   * Bounded depth. Default 128 entries — enough for a session of
//     fine-grained edits without ballooning memory. Older entries
//     are discarded oldest-first when the limit is hit.
//   * Bounded memory budget (default 4 MiB). Commands self-report
//     their footprint via `byte_size()`; the history evicts oldest
//     when the budget would overflow. Default is generous; tune
//     down for in-game console-style edits, up for asset cookers.
//   * Type-erased: commands inherit ICommand. The editor doesn't
//     need to know about every kind of edit at compile time —
//     gameplay tools / plugin code can ship their own command
//     types and push them.
//   * Single-threaded. Editor is single-threaded; the history is
//     not lock-free.
//
// What's NOT here (yet):
//   * Save / restore across sessions. The history is in-memory only.
//   * Cross-process replay. Commands could be serialized via
//     cd::asset_json — also deferred.
//
// Coalescing: push_coalescing(key, cmd) merges consecutive commands that
// share the same key into a single undo step. See EditHistory::push_coalescing
// and CoalescedCommand below.
// =============================================================================
#pragma once

#include <ranges>
#include <cd/core/Defines.hpp>

#include <cstddef>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cd::editor
{

/// Base type for every editor command. apply() / revert() must be
/// pure inverses of one another so undo+redo are idempotent.
class ICommand
{
public:
    ICommand() = default;
    virtual ~ICommand() = default;
    ICommand(const ICommand&) = delete;
    ICommand& operator=(const ICommand&) = delete;
    ICommand(ICommand&&) = delete;
    ICommand& operator=(ICommand&&) = delete;

    /// Run the forward operation. Must be deterministic — running it
    /// twice (without a matching revert in between) is undefined.
    virtual void apply() = 0;

    /// Inverse of apply(). After revert() the target's observable
    /// state must equal the pre-apply state byte-for-byte.
    virtual void revert() = 0;

    /// Short user-facing label, "Translate cube", "Rename actor".
    /// Drives the UI's "Undo X" / "Redo X" menu strings.
    [[nodiscard]] virtual std::string_view label() const noexcept = 0;

    /// Approximate footprint of the command instance + any captured
    /// state. Used by the history's memory-budget enforcement. The
    /// default returns sizeof(*this) — override when you capture
    /// large state (e.g. an entire serialized scene).
    [[nodiscard]] virtual std::size_t byte_size() const noexcept = 0;
};

/// phase1091 — groups N child commands into ONE undo step (the
/// multi-select "move 7 entities" case: one Ctrl+Z reverts all 7).
/// apply() runs children in insertion order, revert() in REVERSE
/// order so overlapping targets unwind correctly. Children must be
/// un-applied when added — EditHistory::push() runs the first
/// apply(), exactly like any other command.
class CompositeCommand final : public ICommand
{
public:
    explicit CompositeCommand(std::string label = "Composite")
        : label_ { std::move(label) }
    {
    }

    /// Add a child. Pre-push only; the composite owns it.
    void add(std::unique_ptr<ICommand> cmd)
    {
        if (cmd != nullptr)
            children_.push_back(std::move(cmd));
    }

    [[nodiscard]] std::size_t size() const noexcept { return children_.size(); }
    [[nodiscard]] bool empty() const noexcept { return children_.empty(); }

    void apply() override
    {
        for (auto& c : children_)
            c->apply();
    }

    void revert() override
    {
        for (const auto& child : children_ | std::views::reverse)
            child->revert();
    }

    [[nodiscard]] std::string_view label() const noexcept override { return label_; }

    [[nodiscard]] std::size_t byte_size() const noexcept override
    {
        std::size_t total = sizeof(*this) + label_.capacity();
        for (const auto& c : children_)
            total += c->byte_size();
        return total;
    }

private:
    std::vector<std::unique_ptr<ICommand>> children_;
    std::string label_;
};

// ---------------------------------------------------------------------------
// CoalescedCommand — internal wrapper used by EditHistory::push_coalescing
// to represent a merged pair of commands. The "original" entry is the one
// that was on the undo stack before merging; "replacement" is the new
// command that replaces it. On undo we run replacement.revert() then
// original.revert(); on redo we run both in forward order. Label comes
// from the replacement (the newer command).
// ---------------------------------------------------------------------------
class CoalescedCommand final : public ICommand
{
public:
    CoalescedCommand(std::unique_ptr<ICommand> original,
                     std::unique_ptr<ICommand> replacement,
                     std::string               coalesce_key)
        : original_     { std::move(original) }
        , replacement_  { std::move(replacement) }
        , coalesce_key_ { std::move(coalesce_key) }
    {}

    void apply() override
    {
        original_->apply();
        replacement_->apply();
    }

    void revert() override
    {
        replacement_->revert();
        original_->revert();
    }

    [[nodiscard]] std::string_view label() const noexcept override
    {
        return replacement_ ? replacement_->label() : std::string_view {};
    }

    [[nodiscard]] std::size_t byte_size() const noexcept override
    {
        std::size_t total = sizeof(*this) + coalesce_key_.capacity();
        if (original_)    total += original_->byte_size();
        if (replacement_) total += replacement_->byte_size();
        return total;
    }

    [[nodiscard]] std::string_view coalesce_key() const noexcept
    {
        return coalesce_key_;
    }

private:
    std::unique_ptr<ICommand> original_;
    std::unique_ptr<ICommand> replacement_;
    std::string               coalesce_key_;
};

/// Bounded undo/redo stack. Single-threaded. Owns the command
/// objects (unique_ptr) so callers can build a command, push it,
/// and forget about lifetime.
class EditHistory
{
public:
    struct Config
    {
        /// Maximum number of commands retained on the undo stack.
        /// Once exceeded, the oldest entries are evicted FIFO.
        std::size_t max_entries { 128 };

        /// Maximum total `byte_size()` retained on the undo stack.
        /// 4 MiB default. Older entries evict oldest-first when an
        /// incoming push would overflow.
        std::size_t max_bytes { std::size_t { 4U } * 1024U * 1024U };
    };

    EditHistory() = default;
    explicit EditHistory(const Config& cfg) noexcept : config_(cfg) { }

    [[nodiscard]] const Config& config() const noexcept { return config_; }

    /// Push a command. The command's `apply()` is called immediately.
    /// Pushing a new command after one or more undo()s discards the
    /// redo stack — classic linear-history behaviour (no branching).
    /// Also resets the coalescing key so the next push_coalescing() call
    /// starts a fresh entry.
    void push(std::unique_ptr<ICommand> cmd)
    {
        if (cmd == nullptr)
            return;
        last_coalesce_key_.clear();
        cmd->apply();
        // New action invalidates the redo trail.
        bytes_redo_ = 0;
        redo_.clear();
        const std::size_t cost = cmd->byte_size();
        undo_.push_back(std::move(cmd));
        bytes_undo_ += cost;
        evict_to_budget_();
    }

    /// Undo the most-recently applied command. Returns true on
    /// success, false when the undo stack is empty.
    /// Resets the coalescing key so the next push_coalescing() call
    /// starts a fresh entry.
    bool undo()
    {
        if (undo_.empty())
            return false;
        last_coalesce_key_.clear();
        auto& top = undo_.back();
        top->revert();
        const std::size_t cost = top->byte_size();
        bytes_undo_ -= std::min(bytes_undo_, cost);
        redo_.push_back(std::move(top));
        bytes_redo_ += cost;
        undo_.pop_back();
        return true;
    }

    /// Re-apply the most-recently undone command. Returns true on
    /// success, false when the redo stack is empty.
    bool redo()
    {
        if (redo_.empty())
            return false;
        auto& top = redo_.back();
        top->apply();
        const std::size_t cost = top->byte_size();
        bytes_redo_ -= std::min(bytes_redo_, cost);
        undo_.push_back(std::move(top));
        bytes_undo_ += cost;
        redo_.pop_back();
        return true;
    }

    // ---- Coalescing push ---------------------------------------------------
    //
    // When a user drags a slider in real-time the inspector emits dozens of
    // small delta commands per second. Without coalescing, Ctrl+Z would step
    // through each micro-delta individually instead of reverting the whole
    // drag session.
    //
    // `push_coalescing(key, cmd)` merges the new command with the previous
    // undo entry when their coalesce keys match. The previous entry's
    // `revert()` undoes its own effect so the committed state stays
    // consistent; then the new command's `apply()` is called. The two are
    // wrapped in a CoalescedPair so that a single Ctrl+Z reverts both.
    //
    // Canonical usage: the `key` encodes target entity id + property name,
    // e.g. "entity:42/pos.x". Consecutive drags on the same property merge;
    // switching property or entity starts a new entry.
    //
    // NOTE: coalescing ALWAYS clears the redo stack (same as plain push).

    /// Coalescing push. Returns true when the new command was merged with
    /// the previous entry; false when it was pushed as a fresh entry.
    ///
    /// Merge logic: given that the previous entry's apply() is already live
    /// in the state, we just call cmd->apply() on top, then create a
    /// CoalescedCommand(previous, cmd). Its revert() calls replacement.revert()
    /// then original.revert() so a single Ctrl+Z undoes the entire merged
    /// drag session.
    bool push_coalescing(std::string_view coalesce_key, std::unique_ptr<ICommand> cmd)
    {
        if (cmd == nullptr) return false;

        // Clear redo trail for any push.
        bytes_redo_ = 0;
        redo_.clear();

        // If there is a previous entry with a matching key, merge.
        if (!undo_.empty() && !last_coalesce_key_.empty() &&
            last_coalesce_key_ == coalesce_key)
        {
            // Current state already has original's effect applied.
            // Apply the new command on top.
            cmd->apply();

            // Account for old entry's footprint being removed.
            bytes_undo_ -= std::min(bytes_undo_, undo_.back()->byte_size());

            // Replace the top with a CoalescedCommand that wraps both.
            // CoalescedCommand::revert() runs replacement.revert() then
            // original.revert() — unwinds both in reverse order.
            undo_.back() = std::make_unique<CoalescedCommand>(
                std::move(undo_.back()), std::move(cmd), std::string { coalesce_key });
            bytes_undo_ += undo_.back()->byte_size();

            evict_to_budget_();
            return true;  // merged
        }

        // No match: fresh push (same as plain push but updates key).
        last_coalesce_key_ = std::string { coalesce_key };
        cmd->apply();
        const std::size_t cost = cmd->byte_size();
        undo_.push_back(std::move(cmd));
        bytes_undo_ += cost;
        evict_to_budget_();
        return false;
    }

    /// Discard both stacks. Used when loading a fresh scene; the
    /// previous commands no longer reference live entities.
    void clear() noexcept
    {
        undo_.clear();
        redo_.clear();
        bytes_undo_ = 0;
        bytes_redo_ = 0;
        last_coalesce_key_.clear();
    }

    [[nodiscard]] bool can_undo() const noexcept { return !undo_.empty(); }
    [[nodiscard]] bool can_redo() const noexcept { return !redo_.empty(); }

    [[nodiscard]] std::size_t undo_depth() const noexcept { return undo_.size(); }
    [[nodiscard]] std::size_t redo_depth() const noexcept { return redo_.size(); }

    [[nodiscard]] std::size_t bytes_in_use() const noexcept { return bytes_undo_ + bytes_redo_; }

    /// User-facing labels for "Undo X" / "Redo X" menu items.
    /// Returns empty when the corresponding stack is empty.
    [[nodiscard]] std::string_view next_undo_label() const noexcept
    {
        return undo_.empty() ? std::string_view {} : undo_.back()->label();
    }
    [[nodiscard]] std::string_view next_redo_label() const noexcept
    {
        return redo_.empty() ? std::string_view {} : redo_.back()->label();
    }

private:
    void evict_to_budget_() noexcept
    {
        // Entry cap: drop the oldest commands until under max_entries.
        while (undo_.size() > config_.max_entries)
        {
            auto& front = undo_.front();
            bytes_undo_ -= std::min(bytes_undo_, front->byte_size());
            undo_.pop_front();
        }
        // Byte cap: same — oldest-first. The redo stack is left alone
        // because the user can already see "redo budget X bytes" via
        // bytes_in_use(); discarding redo entries would surprise them
        // with disappearing "Redo" menu items.
        while (bytes_undo_ > config_.max_bytes && !undo_.empty())
        {
            auto& front = undo_.front();
            bytes_undo_ -= std::min(bytes_undo_, front->byte_size());
            undo_.pop_front();
        }
    }

    Config config_ {};
    std::deque<std::unique_ptr<ICommand>> undo_;
    std::deque<std::unique_ptr<ICommand>> redo_;
    std::size_t bytes_undo_ { 0 };
    std::size_t bytes_redo_ { 0 };
    /// Key of the last push_coalescing() call (cleared on plain push / undo / clear).
    std::string last_coalesce_key_;
};

}  // namespace cd::editor
