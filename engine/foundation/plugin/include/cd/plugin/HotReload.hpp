// =============================================================================
// CHROMODYNAMIC — cd/plugin/HotReload.hpp
// Phase 5 / S4.x — file-watcher-driven hot reload for cd::plugin.
//
// `HotReloader` mounts one plugin path, snapshots its "version" (file
// mtime by default, hash on opt-in), and on every `poll()` re-checks
// the version. If it changed it:
//   1. Fires `before_unload(current_plugin)` so the host can release
//      references it owns into the plugin (e.g. handles, callbacks).
//   2. Destroys the old LoadedPlugin (the loader's RAII guard unmaps
//      the shared library).
//   3. Reloads via the underlying load fn.
//   4. Fires `after_load(new_plugin)` so the host can re-acquire whatever
//      it released.
//
// Polling is explicit — the host calls `poll()` once per frame (or once
// per editor tick). No background thread, no signals, no deadlocks: the
// reload happens on the caller's stack frame with predictable ordering.
//
// The class is intentionally factored around two callable hooks so that
// unit tests can inject mock load + version functions without spinning
// up a real DLL on disk. The public `make_default_hot_reloader()` wires
// the real Loader and filesystem mtime watcher together.
//
// Restart safety: a reload that fails (load_fn returns an error) leaves
// the previous plugin mounted and reports the error via the Result<bool>
// return — the host stays on the last-known-good plugin until the
// developer fixes the broken build.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>
#include <cd/plugin/IPlugin.hpp>
#include <cd/plugin/Loader.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace cd::plugin
{

/// Opaque version stamp for a plugin file. Two stamps comparing unequal
/// trigger a reload; equality means "no change". The default impl uses
/// filesystem last_write_time, but tests / advanced hosts can swap in
/// a content-hash or build-id reader.
using PluginVersionFn = std::function<std::uint64_t(std::string_view path)>;

/// Underlying load callback. Returning an error leaves the current
/// plugin mounted (the reload aborts cleanly).
using PluginLoadFn = std::function<cd::core::Result<LoadedPlugin>(std::string_view path)>;

namespace hot_reload_errors
{
inline constexpr std::uint32_t kDomain = 0x001A;

enum class Code : std::uint32_t
{
    kOk = 0,
    kNotMounted = 1,
    kAlreadyMounted = 2,
    kReloadFailed = 3,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}
}  // namespace hot_reload_errors

class HotReloader
{
public:
    using BeforeUnload = std::function<void(IPlugin&)>;
    using AfterLoad = std::function<void(IPlugin&)>;

    HotReloader(PluginLoadFn load_fn, PluginVersionFn version_fn)
        : load_fn_ { std::move(load_fn) }, version_fn_ { std::move(version_fn) }
    {
    }

    HotReloader(const HotReloader&) = delete;
    HotReloader& operator=(const HotReloader&) = delete;
    HotReloader(HotReloader&&) = default;
    HotReloader& operator=(HotReloader&&) = default;

    /// Initial mount. Returns kAlreadyMounted if a plugin is already loaded.
    [[nodiscard]] cd::core::Result<void> mount(std::string path)
    {
        if (plugin_.has_value())
            return std::unexpected(hot_reload_errors::make(hot_reload_errors::Code::kAlreadyMounted));
        auto loaded = load_fn_(path);
        if (!loaded.has_value())
            return std::unexpected(loaded.error());
        path_ = std::move(path);
        version_ = version_fn_(path_);
        plugin_.emplace(std::move(*loaded));
        if (after_load_ && plugin_->instance)
            after_load_(*plugin_->instance);
        return {};
    }

    /// Detach + unload. Fires `before_unload` if a callback was set.
    void unmount()
    {
        if (!plugin_.has_value())
            return;
        if (before_unload_ && plugin_->instance)
            before_unload_(*plugin_->instance);
        plugin_.reset();
        path_.clear();
        version_ = 0;
    }

    /// Re-check the on-disk version. If it changed since the last
    /// mount/reload, drive the unload → load cycle. Returns:
    ///   * `true`  when a reload actually fired
    ///   * `false` when no version change was detected
    ///   * an error if a reload was attempted but `load_fn` failed (the
    ///     previous plugin stays mounted in that case — we load the
    ///     new build BEFORE tearing the old one down so a failed reload
    ///     never leaves the host with no plugin).
    [[nodiscard]] cd::core::Result<bool> poll()
    {
        if (!plugin_.has_value())
            return std::unexpected(hot_reload_errors::make(hot_reload_errors::Code::kNotMounted));
        const auto current = version_fn_(path_);
        if (current == version_)
            return false;

        // Load the new build first. If it fails the old plugin is
        // untouched and the caller can keep running on the last-known
        // good build until they fix the failure.
        auto loaded = load_fn_(path_);
        if (!loaded.has_value())
            return std::unexpected(loaded.error());

        // Old plugin teardown + swap. before_unload fires before we
        // destroy the previous LoadedPlugin so the host can still
        // safely call into the old instance from the callback.
        if (before_unload_ && plugin_->instance)
            before_unload_(*plugin_->instance);
        plugin_.reset();

        plugin_.emplace(std::move(*loaded));
        version_ = current;
        ++reload_count_;
        if (after_load_ && plugin_->instance)
            after_load_(*plugin_->instance);
        return true;
    }

    [[nodiscard]] IPlugin* current() noexcept
    {
        return plugin_.has_value() ? plugin_->instance.get() : nullptr;
    }

    [[nodiscard]] std::string_view current_path() const noexcept { return path_; }
    [[nodiscard]] std::uint64_t current_version() const noexcept { return version_; }
    [[nodiscard]] std::uint64_t reload_count() const noexcept { return reload_count_; }
    [[nodiscard]] bool is_mounted() const noexcept { return plugin_.has_value(); }

    void set_before_unload(BeforeUnload fn) { before_unload_ = std::move(fn); }
    void set_after_load(AfterLoad fn) { after_load_ = std::move(fn); }

private:
    PluginLoadFn load_fn_;
    PluginVersionFn version_fn_;
    BeforeUnload before_unload_ {};
    AfterLoad after_load_ {};
    std::optional<LoadedPlugin> plugin_ {};
    std::string path_ {};
    std::uint64_t version_ { 0 };
    std::uint64_t reload_count_ { 0 };
};

/// Filesystem-mtime watcher. Returns 0 if the path can't be stat'd —
/// callers should treat 0 as "version unknown, do not reload".
[[nodiscard]] std::uint64_t default_plugin_version(std::string_view path);

/// Convenience: build a HotReloader wired to the real `Loader` and
/// filesystem mtime watcher. The Loader is borrowed (NOT owned) — the
/// caller keeps it alive for the reloader's lifetime.
[[nodiscard]] HotReloader make_default_hot_reloader(Loader& loader);

}  // namespace cd::plugin
