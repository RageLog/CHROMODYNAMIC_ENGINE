// =============================================================================
// CHROMODYNAMIC — cd/render/DrawBucket.hpp
// Phase 108 / Wave 280 — SortKey-driven draw-call bucket.
//
// Pairs Phase 36's `cd::render::SortKey` packed key with a deferred-
// draw callback, so caller code can stuff a frame's draws into a flat
// vector in any order, ask the bucket to sort them, then replay them
// against a command buffer in sorted order. The standard pattern
// described by Christer Ericson (2008).
//
// Why deferred callbacks rather than raw GPU state structs:
//   * Callers already have their own per-material / per-mesh closures
//     that know how to push the right vertex buffer + push constants.
//     Boxing that into "PSO id + VB id + IB id + push block bytes"
//     would duplicate that knowledge.
//   * The callback executes inside the bucket's `emit_all` loop, so
//     hot-state bookkeeping (last bound pipeline, last bound VB)
//     could be added later inside the bucket without changing call
//     sites.
//
// Trade-off: std::function ≈ 32 B + a possible heap alloc per draw.
// Acceptable at typical scene sizes (hundreds-to-thousands of
// draws); for AAA-scale scenes a more compact draw struct with a
// PSO/material index would replace this.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/render/SortKey.hpp>
#include <cd/rhi/ICommandBuffer.hpp>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace cd::render
{

struct DrawItem
{
    SortKey                                          key;
    std::function<void(cd::rhi::ICommandBuffer&)>    emit;
};

class DrawBucket
{
public:
    void reserve(std::size_t n) { items_.reserve(n); }

    /// Stage a single draw. `emit` is run later when the bucket is
    /// replayed against a command buffer.
    void add(SortKey key, std::function<void(cd::rhi::ICommandBuffer&)> emit)
    {
        items_.push_back(DrawItem { key, std::move(emit) });
        sorted_ = false;
    }

    /// Stable sort by SortKey (ascending — opaque front-to-back, then
    /// transparent back-to-front because callers pre-flip transparent
    /// depth bits). Idempotent.
    void sort()
    {
        if (sorted_) return;
        std::ranges::stable_sort(items_,
            [](const DrawItem& a, const DrawItem& b) noexcept {
                return a.key.value < b.key.value;
            });
        sorted_ = true;
    }

    /// Replay every staged draw against `cmd` in sorted order. Calls
    /// `sort()` first if the bucket has been mutated since the last
    /// sort. Does NOT clear after replay — caller decides whether to
    /// reuse or `clear()` for the next frame.
    void emit_all(cd::rhi::ICommandBuffer& cmd)
    {
        sort();
        for (const auto& it : items_) it.emit(cmd);
    }

    /// Drop every staged draw. Capacity preserved for reuse.
    void clear() noexcept
    {
        items_.clear();
        sorted_ = true;  // empty bucket is trivially sorted
    }

    [[nodiscard]] std::size_t size() const noexcept { return items_.size(); }
    [[nodiscard]] bool        empty() const noexcept { return items_.empty(); }
    [[nodiscard]] bool        is_sorted_cached() const noexcept { return sorted_; }

    [[nodiscard]] const std::vector<DrawItem>& items() const noexcept { return items_; }

private:
    std::vector<DrawItem> items_;
    bool                  sorted_ { true };
};

}  // namespace cd::render
