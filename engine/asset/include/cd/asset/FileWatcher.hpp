// =============================================================================
// CHROMODYNAMIC — cd/asset/FileWatcher.hpp
// Phase 16.C / Wave 173 — header-only polling file watcher.
//
// Tracks a list of files by absolute path and invokes a per-file
// callback when the file's modification time advances. Polling rather
// than OS notifications because:
//   * No vendor surface (ReadDirectoryChangesW / inotify / FSEvents)
//     to abstract across Win/Linux/macOS in a portable way for this
//     header-only utility.
//   * Game-engine asset reload is bounded — typical project has dozens
//     of watched files, not thousands. Per-tick stat() of dozens of
//     files is cheap.
//   * The polling interval is caller-driven; cd::runtime can wire it
//     into the standard frame tick.
//
// Usage:
//   cd::asset::FileWatcher fw;
//   fw.watch("shaders/lit.frag.glsl",
//            [](std::string_view path) { /* trigger reload */ });
//   ...
//   fw.poll();   // called once per frame / tick / second
//
// Header-only. Depends on <filesystem> + <chrono>.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>

namespace cd::asset
{

class FileWatcher
{
public:
    using Callback = std::function<void(std::string_view path)>;

    /// Add `path` to the watch set. `cb` fires when the file's mtime
    /// advances. The first `poll()` after `watch()` does NOT fire
    /// (only subsequent changes are reported).
    void watch(std::string path, Callback cb)
    {
        Entry e;
        e.callback = std::move(cb);
        e.last_mtime = read_mtime_(path);
        entries_.emplace(std::move(path), std::move(e));
    }

    /// Stop watching `path`. No-op if not previously watched.
    void unwatch(const std::string& path)
    {
        entries_.erase(path);
    }

    /// Poll every watched file. For each whose mtime has advanced
    /// since the last poll, invoke its callback (synchronously, on
    /// the calling thread) with the file's path. Returns the number
    /// of callbacks fired this poll.
    std::size_t poll()
    {
        std::size_t fired = 0;
        for (auto& [path, e] : entries_)
        {
            const auto now = read_mtime_(path);
            if (now != Mtime {} && now != e.last_mtime)
            {
                e.last_mtime = now;
                if (e.callback) e.callback(path);
                ++fired;
            }
        }
        return fired;
    }

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    void clear() noexcept { entries_.clear(); }

private:
    using Mtime = std::filesystem::file_time_type;

    struct Entry
    {
        Callback callback;
        Mtime last_mtime {};
    };

    [[nodiscard]] static Mtime read_mtime_(const std::string& path) noexcept
    {
        std::error_code ec;
        const auto t = std::filesystem::last_write_time(path, ec);
        if (ec)
            return Mtime {};
        return t;
    }

    std::unordered_map<std::string, Entry> entries_;
};

}  // namespace cd::asset
