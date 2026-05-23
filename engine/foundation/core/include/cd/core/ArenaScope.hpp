// =============================================================================
// CHROMODYNAMIC — cd/core/ArenaScope.hpp
// Phase 36.B / Wave 204 — RAII bracket around FrameAllocator.
//
// `FrameAllocator::reset()` is correct at frame boundaries but easy to
// forget at finer granularities — a function that spawns N temporary
// scratch buffers wants to reclaim that space when it returns even
// if outer code still has live data in the arena.
//
// `ArenaScope` snapshots the bump pointer at construction and rewinds
// to that snapshot on destruction. Nested scopes pop in LIFO order.
//
//   void render_pass(FrameAllocator& arena) {
//       cd::core::ArenaScope local { arena };
//       auto* scratch = arena.allocate(8192, alignof(float));
//       // ... use scratch ...
//   }  // arena bump pointer restored to pre-call value
//
// Pairs with `FrameAllocator::rewind(mark)` (added in this wave).
// Trivially-destructible payloads only — the arena does NOT call
// destructors when it rewinds, same contract as `reset()`.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/FrameAllocator.hpp>

#include <cstddef>

namespace cd::core
{

class ArenaScope
{
public:
    explicit ArenaScope(FrameAllocator& a) noexcept
        : arena_ { &a }, snapshot_ { a.used() }
    {
    }

    ArenaScope(const ArenaScope&) = delete;
    ArenaScope& operator=(const ArenaScope&) = delete;

    ~ArenaScope() noexcept
    {
        if (arena_) arena_->rewind(snapshot_);
    }

    [[nodiscard]] std::size_t snapshot() const noexcept { return snapshot_; }

private:
    FrameAllocator* arena_;
    std::size_t     snapshot_;
};

}  // namespace cd::core
