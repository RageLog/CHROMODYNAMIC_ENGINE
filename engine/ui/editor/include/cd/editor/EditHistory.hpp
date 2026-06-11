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
//   * Coalescing. Two consecutive translations of the same entity
//     could collapse into one entry; deferred.
//   * Save / restore across sessions. The history is in-memory only.
//   * Cross-process replay. Commands could be serialized via
//     cd::asset_json — also deferred.
// =============================================================================
#pragma once

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
        for (auto it = children_.rbegin(); it != children_.rend(); ++it)
            (*it)->revert();
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
    void push(std::unique_ptr<ICommand> cmd)
    {
        if (cmd == nullptr)
            return;
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
    bool undo()
    {
        if (undo_.empty())
            return false;
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

    /// Discard both stacks. Used when loading a fresh scene; the
    /// previous commands no longer reference live entities.
    void clear() noexcept
    {
        undo_.clear();
        redo_.clear();
        bytes_undo_ = 0;
        bytes_redo_ = 0;
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
};

}  // namespace cd::editor
