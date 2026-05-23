// =============================================================================
// CHROMODYNAMIC — cd/shader/FileWatcher.hpp
//
// Polling file-watcher for shader hot-reload. Tracks a fixed list of
// paths and reports any whose `last_write_time` changed since the last
// poll(). Deliberately simple — does not use ReadDirectoryChangesW /
// inotify / FSEvents because:
//   * Polling is portable across Win / Linux / macOS with no platform
//     ifdefs (std::filesystem only).
//   * Shader files are small (~1 KB) and few (~tens), so stat'ing them
//     all at 60 Hz costs sub-millisecond.
//   * Native APIs add per-OS background threads and recursive-directory
//     edge cases (rename → delete + create) that are easy to get wrong.
//     The polling design is correct by construction.
//
// Usage pattern in the sample loop:
//   FileWatcher w;
//   w.add("shaders/triangle.vert");
//   ...
//   if (w.poll()) {
//     for (auto& p : w.dirty()) reload_shader(p);
//   }
//
// `add()` records the file's CURRENT mtime so the first poll() does not
// false-positive on freshly-added paths. Polling a missing file is OK —
// the file is tracked as "missing"; its appearance later counts as a
// change.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cd::shader
{

class FileWatcher
{
public:
    FileWatcher() noexcept = default;

    /// Begin tracking `path`. If the file exists, its mtime is sampled
    /// immediately so the first poll() reports no change. Idempotent —
    /// re-adding an already-tracked path replaces its baseline.
    void add(std::string_view path)
    {
        const std::string key { path };
        auto& info = tracked_[key];
        info.exists = false;
        info.mtime = {};
        std::error_code ec;
        const std::filesystem::path p { key };
        if (std::filesystem::exists(p, ec) && !ec)
        {
            info.mtime = std::filesystem::last_write_time(p, ec);
            if (!ec)
                info.exists = true;
        }
    }

    /// Stop tracking `path`. No-op if it wasn't being watched.
    void remove(std::string_view path)
    {
        tracked_.erase(std::string { path });
    }

    /// Re-stat every tracked path. Returns true if at least one file
    /// changed since the previous poll(). The list of changed paths is
    /// available via `dirty()` until the next poll() call.
    bool poll()
    {
        dirty_.clear();
        for (auto& [path, info] : tracked_)
        {
            std::error_code ec;
            const std::filesystem::path p { path };
            const bool now_exists = std::filesystem::exists(p, ec) && !ec;
            if (now_exists != info.exists)
            {
                info.exists = now_exists;
                if (now_exists)
                {
                    info.mtime = std::filesystem::last_write_time(p, ec);
                    if (ec)
                        info.mtime = {};
                }
                else
                {
                    info.mtime = {};
                }
                dirty_.push_back(path);
                continue;
            }
            if (now_exists)
            {
                const auto t = std::filesystem::last_write_time(p, ec);
                if (!ec && t != info.mtime)
                {
                    info.mtime = t;
                    dirty_.push_back(path);
                }
            }
        }
        return !dirty_.empty();
    }

    /// Paths that changed at the most recent poll() call. Invalidated by
    /// the next poll(); copy out if you need them later.
    [[nodiscard]] const std::vector<std::string>& dirty() const noexcept
    {
        return dirty_;
    }

    [[nodiscard]] std::size_t watched_count() const noexcept
    {
        return tracked_.size();
    }

private:
    struct Info
    {
        std::filesystem::file_time_type mtime {};
        bool exists { false };
    };

    std::unordered_map<std::string, Info> tracked_;
    std::vector<std::string> dirty_;
};

}  // namespace cd::shader
