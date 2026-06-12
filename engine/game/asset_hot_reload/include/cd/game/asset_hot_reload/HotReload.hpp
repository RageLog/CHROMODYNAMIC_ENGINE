// =============================================================================
// CHROMODYNAMIC — cd/game/asset_hot_reload/HotReload.hpp
// Phase 502 — cd::game::asset_hot_reload::HotReloadBus (G5.1 of the gameplay
// library family, see ADR-20260530-gameplay-library-family).
//
// Runtime asset reload dispatcher. Sits one tier above cd::asset::FileWatcher
// (the production producer) and below subsystem-specific reload sinks
// (texture cache invalidation, locale table swap, shader pipeline rebuild,
// mesh GPU upload, audio buffer refresh). The bus:
//
//   * Categorises every watch by `AssetCategory` so subsystems can subscribe
//     by domain ("all textures") or by path ("just shaders/lit.frag.glsl").
//   * Polls a single shared cd::asset::FileWatcher each `tick()` so the
//     polling cost is paid once per frame regardless of how many subscribers
//     attach to the same file.
//   * Throttles "rapid bursts" — when an editor save coalesces into multiple
//     mtime ticks within the throttle window the bus collapses them into a
//     single observable callback per file. Default window: 100 ms.
//   * Supports multiple subscribers per path with stable subscription handles
//     so callers can detach individually without disturbing siblings.
//
// Dependencies (CLAUDE.md §7): cd::core (for Defines + Result) and
// cd::asset (header-only INTERFACE — FileWatcher lives in include/cd/asset/).
// The library is below cd::editor and cd::sample in the engine DAG; it never
// pulls in the renderer, GPU resources, or any concrete reload sink.
//
// Thread-safety: not thread-safe. Drive from the thread that owns the
// frame tick (cd::runtime). Subsystems hand callbacks back to the bus and
// the bus invokes them synchronously on that same thread.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Forward-declare cd::asset::FileWatcher so the header pulls in only the
// header-only include cd::asset publishes. The .cpp pulls the real symbol.
namespace cd::asset
{
class FileWatcher;
}

namespace cd::game::asset_hot_reload
{

// -----------------------------------------------------------------------------
// AssetCategory — coarse classifier for routed callbacks.
//
// The bus does not infer the category from the path extension — callers
// pass it explicitly because a `.json` file might be a locale strings table
// for one subsystem and a material parameter block for another. The enum
// stays deliberately small; new categories land as Phase G5 adds new
// reload pipelines.
// -----------------------------------------------------------------------------
enum class AssetCategory : std::uint8_t
{
    kTexture = 0,  ///< GPU image / sampler resource.
    kMesh    = 1,  ///< Static or skinned mesh asset.
    kAudio   = 2,  ///< WAV / OGG / streaming audio source.
    kLocale  = 3,  ///< Localization strings tables (.po, .json bundles).
    kShader  = 4,  ///< GLSL / HLSL / SPV source / pipeline definition.
};

[[nodiscard]] constexpr std::string_view to_string(AssetCategory c) noexcept
{
    switch (c)
    {
        case AssetCategory::kTexture: return "texture";
        case AssetCategory::kMesh:    return "mesh";
        case AssetCategory::kAudio:   return "audio";
        case AssetCategory::kLocale:  return "locale";
        case AssetCategory::kShader:  return "shader";
    }
    return "unknown";
}

// -----------------------------------------------------------------------------
// ChangeKind — emitted to subscribers so they can distinguish creation /
// modification (lump-summed under kModified, matching the FileWatcher event
// surface) from deletion (the file disappeared after we had been watching it).
// -----------------------------------------------------------------------------
enum class ChangeKind : std::uint8_t
{
    kModified = 0,  ///< File was created or modified (mtime advanced or first appearance).
    kDeleted  = 1,  ///< File existed previously and is now missing from disk.
};

// -----------------------------------------------------------------------------
// ReloadEvent — payload handed to each subscriber callback.
// -----------------------------------------------------------------------------
struct ReloadEvent
{
    std::string_view path;
    AssetCategory    category { AssetCategory::kTexture };
    ChangeKind       kind     { ChangeKind::kModified };
};

// -----------------------------------------------------------------------------
// SubscriptionId — opaque handle returned by `watch()`. Use it with
// `unwatch_subscription()` to detach a single observer without disturbing
// other observers on the same path. Monotonically allocated; 0 is reserved
// for the "invalid / never returned" sentinel.
// -----------------------------------------------------------------------------
struct SubscriptionId
{
    std::uint64_t value { 0 };

    [[nodiscard]] constexpr bool is_valid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const SubscriptionId&) const noexcept = default;
};

using ReloadCallback = std::function<void(const ReloadEvent&)>;

// -----------------------------------------------------------------------------
// HotReloadBus — the public surface.
//
// Lifecycle:
//   1. Construct (cheap, no I/O).
//   2. `watch(path, category, cb)` to attach observers; multiple subscribers
//      may share a path, and they may use different categories.
//   3. Call `tick()` once per frame (or however often you want polling). The
//      bus polls its internal FileWatcher, coalesces bursts inside the
//      throttle window, and fires the still-pending callbacks at the end of
//      the tick.
//   4. `unwatch(path)` removes *every* subscriber on `path`;
//      `unwatch_subscription(id)` removes just one.
//
// Throttle semantics:
//   * Each path keeps a `last_dispatch_time_` stamp (steady_clock).
//   * On a FileWatcher event the bus marks the path "pending" and records
//     the latest detected kind (modified / deleted; deletion wins if both
//     happened in the same window).
//   * At tick end the bus fires the callback iff
//       (now - last_dispatch_time_) >= throttle_window_.
//     Otherwise the pending state persists into subsequent ticks and the
//     callback fires on the first tick after the window elapses. Bursts
//     therefore collapse to one callback per file per `throttle_window_`.
//
// Default throttle window: 100 ms. Configurable via the ctor for tests and
// for hosts that prefer a different cadence.
// -----------------------------------------------------------------------------
class HotReloadBus
{
public:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration  = std::chrono::milliseconds;

    /// Construct with the default 100 ms throttle window.
    HotReloadBus();

    /// Construct with an explicit throttle window. A zero window disables
    /// coalescing (every detected change fires immediately at tick).
    explicit HotReloadBus(Duration throttle_window);

    HotReloadBus(const HotReloadBus&)            = delete;
    HotReloadBus& operator=(const HotReloadBus&) = delete;
    HotReloadBus(HotReloadBus&&)                 = delete;
    HotReloadBus& operator=(HotReloadBus&&)      = delete;

    ~HotReloadBus();

    /// Attach an observer to `path`. If `path` is not yet on the watch
    /// list the bus registers it with its internal FileWatcher. The
    /// callback fires the first time the file appears on disk if it
    /// was absent at registration time, and on every detected mtime
    /// advance afterwards (subject to throttling).
    ///
    /// Returns a non-zero SubscriptionId for `unwatch_subscription()`.
    /// Passing an empty path or a null callback returns the invalid
    /// sentinel (`{0}`) and has no other effect.
    SubscriptionId watch(std::string   path,
                         AssetCategory category,
                         ReloadCallback on_change);

    /// Detach every subscriber on `path` and remove the path from the
    /// internal FileWatcher. No-op if `path` is not watched.
    /// Returns the number of subscribers removed.
    std::size_t unwatch(const std::string& path);

    /// Detach a single subscriber by id. If this was the last subscriber
    /// on its path the path is also removed from the FileWatcher.
    /// Returns true iff the id was valid and successfully removed.
    bool unwatch_subscription(SubscriptionId id);

    /// Drive one frame of the bus: poll the FileWatcher, register fresh
    /// mtime events, then dispatch any pending events whose throttle
    /// window has elapsed. Returns the number of callbacks invoked.
    std::size_t tick();

    /// True if `path` has at least one subscriber.
    [[nodiscard]] bool is_watching(const std::string& path) const noexcept;

    /// Total number of subscribers across all paths.
    [[nodiscard]] std::size_t subscriber_count() const noexcept;

    /// Number of distinct watched paths.
    [[nodiscard]] std::size_t watched_path_count() const noexcept;

    /// Current throttle window.
    [[nodiscard]] Duration throttle_window() const noexcept { return throttle_window_; }

    /// Override the throttle window at runtime. Affects subsequent dispatches
    /// only — pending events keep their existing pending-since stamp.
    void set_throttle_window(Duration window) noexcept { throttle_window_ = window; }

    /// Inject the "now" timestamp the bus uses for throttling decisions.
    /// Test-only seam — production callers pass `Clock::now()` implicitly
    /// (see `tick()` overload taking no arguments).
    std::size_t tick(TimePoint now);

private:
    struct Subscriber
    {
        SubscriptionId id;
        AssetCategory  category;
        ReloadCallback callback;
    };

    struct PathEntry
    {
        std::vector<Subscriber> subscribers;
        bool                    file_present_last_poll { false };

        // Throttle state.
        bool                    pending          { false };
        ChangeKind              pending_kind     { ChangeKind::kModified };
        TimePoint               pending_since    {};
        TimePoint               last_dispatch_at {};
    };

    // Register an mtime / creation / deletion event detected this tick.
    void note_event(const std::string& path, ChangeKind kind, TimePoint now);

    // Dispatch any pending events whose throttle window has elapsed.
    std::size_t flush_pending(TimePoint now);

    Duration                                                   throttle_window_;
    std::uint64_t                                              next_id_ { 1 };
    std::unordered_map<std::string, PathEntry>                 paths_;
    std::unique_ptr<cd::asset::FileWatcher>                    watcher_;
};

}  // namespace cd::game::asset_hot_reload
