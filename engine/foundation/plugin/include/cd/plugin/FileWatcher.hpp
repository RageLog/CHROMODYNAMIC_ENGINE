// =============================================================================
// CHROMODYNAMIC — cd/plugin/FileWatcher.hpp
// Phase 6 / Wave 38 — file change notifier for plugin hot-reload.
//
// `IFileWatcher` is the surface the host calls. Two concrete impls ship:
//
//   * Polling watcher — works on every platform. A background thread
//     stats the file at a fixed cadence; when the mtime changes it
//     invokes the callback. Latency = poll interval (default 250 ms).
//
//   * Native watcher (Win32) — ReadDirectoryChangesW on the directory
//     containing the watched file. The OS notifies on FILE_NOTIFY_
//     CHANGE_LAST_WRITE; the callback fires on the watcher's thread.
//     Latency is sub-poll-tick.
//
// Linux (inotify) and macOS (FSEvents) implementations land in their
// own follow-up waves; until then `make_native_file_watcher()` falls
// back to the polling impl on those hosts.
//
// Callback contract: invoked from the watcher's background thread. The
// host must do its own synchronisation if it touches data the main
// thread is also using. Returning from the callback yields the thread
// back to the watcher; throwing from it is undefined behaviour (the
// watcher does NOT catch — exceptions propagate and crash the process,
// which is the correct safety stance for a callback contract).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

namespace cd::plugin
{

namespace watcher_errors
{
inline constexpr std::uint32_t kDomain = 0x001B;

enum class Code : std::uint32_t
{
    kOk = 0,
    kAlreadyWatching = 1,
    kFileNotFound = 2,
    kBackendError = 3,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}
}  // namespace watcher_errors

class IFileWatcher
{
public:
    using Callback = std::function<void()>;

    IFileWatcher() noexcept = default;
    virtual ~IFileWatcher() = default;
    IFileWatcher(const IFileWatcher&) = delete;
    IFileWatcher& operator=(const IFileWatcher&) = delete;
    IFileWatcher(IFileWatcher&&) = delete;
    IFileWatcher& operator=(IFileWatcher&&) = delete;

    /// Start watching `path`. Fires `cb` every time the file changes.
    /// Returns kAlreadyWatching when already mounted on a different
    /// path; call `stop()` first.
    [[nodiscard]] virtual cd::core::Result<void>
    watch(std::string_view path, Callback cb) = 0;

    /// Stop watching. Joins the watcher thread. Safe to call multiple
    /// times; a no-op if not currently watching.
    virtual void stop() = 0;

    /// True between `watch()` and `stop()`.
    [[nodiscard]] virtual bool is_watching() const noexcept = 0;

    /// Test hook: poll manually. The polling impl performs one stat()
    /// + fire-on-change here; the native impl is a no-op. Lets tests
    /// drive the watcher deterministically without sleeping.
    virtual void poll_once() = 0;

    /// Diagnostic counter — number of times the watched file's version
    /// stamp changed since `watch()` was called.
    [[nodiscard]] virtual std::uint64_t change_count() const noexcept = 0;
};

/// Always-available polling watcher. `interval` is the background poll
/// cadence; pass 0 to disable the thread and rely solely on
/// `poll_once()` (test-friendly mode).
[[nodiscard]] std::unique_ptr<IFileWatcher>
make_polling_file_watcher(std::chrono::milliseconds interval = std::chrono::milliseconds { 250 });

/// Best-available native watcher. On Windows: ReadDirectoryChangesW;
/// on Linux/macOS: falls back to the polling impl (real inotify /
/// FSEvents land in their own waves). Result is always non-null.
[[nodiscard]] std::unique_ptr<IFileWatcher> make_native_file_watcher();

}  // namespace cd::plugin
