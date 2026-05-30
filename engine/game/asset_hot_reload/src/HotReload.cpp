// =============================================================================
// CHROMODYNAMIC — cd/game/asset_hot_reload/HotReload.cpp
// Phase 502 — HotReloadBus implementation.
//
// Dispatch loop in tick(now):
//   1. Poll the internal FileWatcher (fires raw mtime-advance callbacks into
//      the bus, which mark the corresponding PathEntry "pending").
//   2. Independently stat every registered path so we can detect
//      a) creation (file_present_last_poll = false  -> exists now), and
//      b) deletion (file_present_last_poll = true   -> missing now).
//      FileWatcher itself only tracks mtime advance, so these transitions
//      need their own probe. The probe runs once per path per tick.
//   3. Flush every PathEntry whose pending state has aged past
//      throttle_window_, firing one ReloadEvent per subscriber.
//
// The two-stage design (note-then-flush) is what gives us the "rapid writes
// coalesce to one callback" guarantee — a burst of mtime events in the same
// tick simply repaints the pending state; only one flush happens at end of
// tick, and a second tick within the window keeps the pending state alive
// without firing a duplicate.
// =============================================================================
#include <cd/game/asset_hot_reload/HotReload.hpp>

#include <cd/asset/FileWatcher.hpp>

#include <filesystem>
#include <system_error>
#include <utility>

namespace cd::game::asset_hot_reload
{

namespace
{

[[nodiscard]] bool path_exists_on_disk(const std::string& path) noexcept
{
    std::error_code ec;
    return std::filesystem::exists(path, ec) && !ec;
}

}  // namespace

// -----------------------------------------------------------------------------
// Construction / destruction
// -----------------------------------------------------------------------------
HotReloadBus::HotReloadBus()
    : HotReloadBus(Duration { 100 })
{
}

HotReloadBus::HotReloadBus(Duration throttle_window)
    : throttle_window_(throttle_window)
    , watcher_(std::make_unique<cd::asset::FileWatcher>())
{
}

HotReloadBus::~HotReloadBus() = default;

// -----------------------------------------------------------------------------
// watch
// -----------------------------------------------------------------------------
SubscriptionId HotReloadBus::watch(std::string   path,
                                   AssetCategory category,
                                   ReloadCallback on_change)
{
    if (path.empty() || !on_change)
    {
        return SubscriptionId { 0 };
    }

    const SubscriptionId id { next_id_++ };

    auto [it, inserted] = paths_.try_emplace(path);
    PathEntry& entry = it->second;
    entry.subscribers.push_back(Subscriber { id, category, std::move(on_change) });

    if (inserted)
    {
        // First subscriber for this path — register with FileWatcher.
        // The FileWatcher's first-poll-doesn't-fire contract is exactly
        // what we want for mtime advances. Creation / deletion transitions
        // are detected by our own stat probe in tick().
        entry.file_present_last_poll = path_exists_on_disk(path);

        // Capture by stable key from the map (string lives as long as the
        // entry does, which is at least as long as the FileWatcher entry —
        // FileWatcher itself stores its own copy of the path string so this
        // closure does not even need the back-reference, but capturing the
        // path string by value keeps the callback side-effect-free against
        // future cd::asset::FileWatcher internals).
        std::string cb_path = path;
        watcher_->watch(std::move(cb_path),
            [this, key = path](std::string_view /*p*/)
            {
                // Inside the watcher callback we only mark the entry pending.
                // The actual dispatch (and any throttle decision) happens
                // back in tick() once we know the current time.
                note_event_(key, ChangeKind::kModified, Clock::now());
            });
    }

    return id;
}

// -----------------------------------------------------------------------------
// unwatch (whole path)
// -----------------------------------------------------------------------------
std::size_t HotReloadBus::unwatch(const std::string& path)
{
    auto it = paths_.find(path);
    if (it == paths_.end()) return 0;

    const std::size_t removed = it->second.subscribers.size();
    paths_.erase(it);
    watcher_->unwatch(path);
    return removed;
}

// -----------------------------------------------------------------------------
// unwatch_subscription (single observer)
// -----------------------------------------------------------------------------
bool HotReloadBus::unwatch_subscription(SubscriptionId id)
{
    if (!id.is_valid()) return false;

    for (auto it = paths_.begin(); it != paths_.end(); ++it)
    {
        auto& subs = it->second.subscribers;
        for (auto sit = subs.begin(); sit != subs.end(); ++sit)
        {
            if (sit->id == id)
            {
                subs.erase(sit);
                if (subs.empty())
                {
                    const std::string path_copy = it->first;
                    paths_.erase(it);
                    watcher_->unwatch(path_copy);
                }
                return true;
            }
        }
    }
    return false;
}

// -----------------------------------------------------------------------------
// tick
// -----------------------------------------------------------------------------
std::size_t HotReloadBus::tick()
{
    return tick(Clock::now());
}

std::size_t HotReloadBus::tick(TimePoint now)
{
    // Phase 1: drain raw mtime events from FileWatcher. The watcher fires
    // synchronously inside poll(); each callback funnels into
    // `note_event_(path, kModified, ...)`.
    watcher_->poll();

    // Phase 2: detect creation / deletion transitions ourselves.
    for (auto& [path, entry] : paths_)
    {
        const bool exists_now = path_exists_on_disk(path);
        if (exists_now && !entry.file_present_last_poll)
        {
            // Fresh creation. FileWatcher will start tracking mtime from
            // here on; we route a kModified event so subscribers can load
            // the new file. (Deletion takes precedence elsewhere; if both
            // creation and deletion happened inside the same window we
            // would not be in this branch because the final state is
            // "missing".)
            note_event_(path, ChangeKind::kModified, now);
        }
        else if (!exists_now && entry.file_present_last_poll)
        {
            // File disappeared. We still want subscribers to know — Asset
            // streamers usually invalidate any cached blob; the live game
            // can fall back to a placeholder texture.
            note_event_(path, ChangeKind::kDeleted, now);
        }
        entry.file_present_last_poll = exists_now;
    }

    // Phase 3: flush pending events whose throttle window has elapsed.
    return flush_pending_(now);
}

// -----------------------------------------------------------------------------
// note_event_
// -----------------------------------------------------------------------------
void HotReloadBus::note_event_(const std::string& path, ChangeKind kind, TimePoint now)
{
    auto it = paths_.find(path);
    if (it == paths_.end()) return;

    PathEntry& entry = it->second;
    if (!entry.pending)
    {
        entry.pending       = true;
        entry.pending_since = now;
        entry.pending_kind  = kind;
    }
    else
    {
        // Already pending — deletion wins over modified so subscribers
        // observe the final on-disk state.
        if (kind == ChangeKind::kDeleted)
        {
            entry.pending_kind = ChangeKind::kDeleted;
        }
    }
}

// -----------------------------------------------------------------------------
// flush_pending_
// -----------------------------------------------------------------------------
std::size_t HotReloadBus::flush_pending_(TimePoint now)
{
    std::size_t fired = 0;
    for (auto& [path, entry] : paths_)
    {
        if (!entry.pending) continue;

        const auto elapsed_since_pending  = now - entry.pending_since;
        const auto elapsed_since_dispatch = now - entry.last_dispatch_at;

        // First dispatch is unconditional once pending; subsequent dispatches
        // require BOTH the window-since-pending and the window-since-last-
        // dispatch to have elapsed. The pending_since stamp protects against
        // a single ultra-fast mtime tick slipping through; the last_dispatch
        // stamp coalesces a sustained burst across many ticks.
        const bool first_dispatch = (entry.last_dispatch_at == TimePoint {});
        const bool window_ok =
            (elapsed_since_pending  >= throttle_window_) &&
            (first_dispatch || elapsed_since_dispatch >= throttle_window_);

        if (!window_ok)
        {
            // A zero throttle window means "fire on the same tick the event
            // was noted" — but only if the dispatch hasn't already fired
            // this same tick. We special-case that here.
            if (throttle_window_.count() == 0)
            {
                // pass through
            }
            else
            {
                continue;
            }
        }

        const ReloadEvent ev {
            std::string_view { path },
            entry.subscribers.empty() ? AssetCategory::kTexture
                                      : entry.subscribers.front().category,
            entry.pending_kind,
        };

        // Snapshot subscribers because a callback may detach itself (we
        // tolerate self-unsubscription without iterator invalidation).
        const auto subs_snapshot = entry.subscribers;
        for (const auto& sub : subs_snapshot)
        {
            if (sub.callback)
            {
                const ReloadEvent per_sub_ev {
                    std::string_view { path },
                    sub.category,
                    entry.pending_kind,
                };
                sub.callback(per_sub_ev);
                ++fired;
            }
        }
        (void)ev;  // event header path kept for IDE-friendly inspection.

        entry.pending          = false;
        entry.last_dispatch_at = now;
    }
    return fired;
}

// -----------------------------------------------------------------------------
// Introspection
// -----------------------------------------------------------------------------
bool HotReloadBus::is_watching(const std::string& path) const noexcept
{
    return paths_.find(path) != paths_.end();
}

std::size_t HotReloadBus::subscriber_count() const noexcept
{
    std::size_t total = 0;
    for (const auto& [_, entry] : paths_)
    {
        total += entry.subscribers.size();
    }
    return total;
}

std::size_t HotReloadBus::watched_path_count() const noexcept
{
    return paths_.size();
}

}  // namespace cd::game::asset_hot_reload
