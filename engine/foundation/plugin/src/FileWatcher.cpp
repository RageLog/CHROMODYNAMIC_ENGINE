// =============================================================================
// CHROMODYNAMIC — engine/foundation/plugin/src/FileWatcher.cpp
//
// Polling + Win32 native implementations. See FileWatcher.hpp.
// =============================================================================
#include <cd/plugin/FileWatcher.hpp>
#include <cd/plugin/HotReload.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#if defined(_WIN32)
#    define NOMINMAX
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#elif defined(__linux__)
#    include <fcntl.h>
#    include <poll.h>
#    include <sys/eventfd.h>
#    include <sys/inotify.h>
#    include <unistd.h>
#elif defined(__APPLE__)
#    include <CoreServices/CoreServices.h>
#endif

namespace cd::plugin
{

namespace
{

[[nodiscard]] std::uint64_t stat_version(std::string_view path) noexcept
{
    return default_plugin_version(path);
}

class PollingWatcher final : public IFileWatcher
{
public:
    explicit PollingWatcher(std::chrono::milliseconds interval) noexcept
        : interval_ { interval }
    {
    }

    ~PollingWatcher() override { stop(); }

    [[nodiscard]] cd::core::Result<void> watch(std::string_view path, Callback cb) override
    {
        if (running_.load(std::memory_order_acquire))
            return std::unexpected(
                watcher_errors::make(watcher_errors::Code::kAlreadyWatching));
        path_ = std::string { path };
        last_version_ = stat_version(path_);
        if (last_version_ == 0 && !std::filesystem::exists(path_))
            return std::unexpected(
                watcher_errors::make(watcher_errors::Code::kFileNotFound));
        callback_ = std::move(cb);
        change_count_.store(0, std::memory_order_release);

        if (interval_.count() == 0)
        {
            // Test mode: caller drives ticks via poll_once(); we still
            // mark the watcher as running so is_watching() reflects state.
            running_.store(true, std::memory_order_release);
            return {};
        }
        running_.store(true, std::memory_order_release);
        thread_ = std::thread { [this] { run_(); } };
        return {};
    }

    void stop() override
    {
        bool was_running = true;
        if (!running_.compare_exchange_strong(was_running, false))
            return;
        cv_.notify_all();
        if (thread_.joinable())
            thread_.join();
        callback_ = nullptr;
        path_.clear();
    }

    [[nodiscard]] bool is_watching() const noexcept override
    {
        return running_.load(std::memory_order_acquire);
    }

    void poll_once() override
    {
        if (!running_.load(std::memory_order_acquire))
            return;
        const auto v = stat_version(path_);
        if (v != 0 && v != last_version_)
        {
            last_version_ = v;
            change_count_.fetch_add(1, std::memory_order_release);
            if (callback_)
                callback_();
        }
    }

    [[nodiscard]] std::uint64_t change_count() const noexcept override
    {
        return change_count_.load(std::memory_order_acquire);
    }

private:
    void run_()
    {
        std::unique_lock guard { mu_ };
        while (running_.load(std::memory_order_acquire))
        {
            cv_.wait_for(guard, interval_,
                         [this] { return !running_.load(std::memory_order_acquire); });
            if (!running_.load(std::memory_order_acquire))
                return;
            const auto v = stat_version(path_);
            if (v != 0 && v != last_version_)
            {
                last_version_ = v;
                change_count_.fetch_add(1, std::memory_order_release);
                if (callback_)
                {
                    // Drop the lock around the user callback so a slow
                    // callback can't block stop().
                    guard.unlock();
                    callback_();
                    guard.lock();
                }
            }
        }
    }

    std::chrono::milliseconds interval_;
    std::string path_ {};
    Callback callback_ {};
    std::uint64_t last_version_ { 0 };
    std::atomic<bool> running_ { false };
    std::atomic<std::uint64_t> change_count_ { 0 };
    std::mutex mu_;
    std::condition_variable cv_;
    std::thread thread_;
};

#if defined(_WIN32)
// Win32 native watcher: ReadDirectoryChangesW on the parent directory,
// filtered by FILE_NOTIFY_CHANGE_LAST_WRITE. The watched filename is
// compared against the OS-reported notification list; non-matching
// changes are ignored so callers can target one file in a busy dir.
class Win32NativeWatcher final : public IFileWatcher
{
public:
    Win32NativeWatcher() noexcept = default;
    ~Win32NativeWatcher() override { stop(); }

    [[nodiscard]] cd::core::Result<void> watch(std::string_view path, Callback cb) override
    {
        if (running_.load(std::memory_order_acquire))
            return std::unexpected(
                watcher_errors::make(watcher_errors::Code::kAlreadyWatching));
        const std::filesystem::path fs_path { std::string { path } };
        if (!std::filesystem::exists(fs_path))
            return std::unexpected(
                watcher_errors::make(watcher_errors::Code::kFileNotFound));
        path_ = fs_path;
        filename_ = fs_path.filename().wstring();
        const auto dir = fs_path.parent_path().wstring();
        dir_handle_ = ::CreateFileW(
            dir.c_str(),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            nullptr);
        if (dir_handle_ == INVALID_HANDLE_VALUE)
            return std::unexpected(
                watcher_errors::make(watcher_errors::Code::kBackendError,
                                     "CreateFileW failed for watched directory"));
        callback_ = std::move(cb);
        change_count_.store(0, std::memory_order_release);
        running_.store(true, std::memory_order_release);
        thread_ = std::thread { [this] { run_(); } };
        return {};
    }

    void stop() override
    {
        bool was_running = true;
        if (!running_.compare_exchange_strong(was_running, false))
            return;
        if (dir_handle_ != INVALID_HANDLE_VALUE)
            ::CancelIoEx(dir_handle_, nullptr);
        if (thread_.joinable())
            thread_.join();
        if (dir_handle_ != INVALID_HANDLE_VALUE)
        {
            ::CloseHandle(dir_handle_);
            dir_handle_ = INVALID_HANDLE_VALUE;
        }
        callback_ = nullptr;
    }

    [[nodiscard]] bool is_watching() const noexcept override
    {
        return running_.load(std::memory_order_acquire);
    }

    void poll_once() override
    {
        // No-op on the native impl — the OS event arrives asynchronously.
        // Test scaffolding that wants determinism should use the polling
        // watcher with `interval == 0`.
    }

    [[nodiscard]] std::uint64_t change_count() const noexcept override
    {
        return change_count_.load(std::memory_order_acquire);
    }

private:
    void run_()
    {
        // 4 KiB buffer is enough for thousands of notifications per call.
        // Allocated on the heap because the worker holds it across waits.
        constexpr DWORD kBufBytes = 4096;
        auto buf = std::make_unique<std::byte[]>(kBufBytes);
        OVERLAPPED overlapped {};
        overlapped.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (overlapped.hEvent == nullptr)
            return;
        while (running_.load(std::memory_order_acquire))
        {
            ::ResetEvent(overlapped.hEvent);
            DWORD ret = 0;
            const BOOL ok = ::ReadDirectoryChangesW(
                dir_handle_, buf.get(), kBufBytes, FALSE,
                FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE
                    | FILE_NOTIFY_CHANGE_CREATION | FILE_NOTIFY_CHANGE_FILE_NAME,
                &ret, &overlapped, nullptr);
            if (ok == FALSE)
                break;
            const DWORD wait = ::WaitForSingleObject(overlapped.hEvent, INFINITE);
            if (wait != WAIT_OBJECT_0 || !running_.load(std::memory_order_acquire))
                break;
            DWORD bytes = 0;
            if (::GetOverlappedResult(dir_handle_, &overlapped, &bytes, FALSE) == FALSE
                || bytes == 0)
                continue;

            // Walk the FILE_NOTIFY_INFORMATION linked list. Each entry's
            // FileName is NOT null-terminated; FileNameLength is in
            // bytes (i.e. wchar_t count * 2).
            const std::byte* cursor = buf.get();
            while (true)
            {
                const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(cursor);
                const std::wstring_view name {
                    info->FileName,
                    static_cast<std::size_t>(info->FileNameLength / sizeof(WCHAR))
                };
                if (name == filename_)
                {
                    change_count_.fetch_add(1, std::memory_order_release);
                    if (callback_)
                        callback_();
                    break;  // one fire per notification batch is enough
                }
                if (info->NextEntryOffset == 0)
                    break;
                cursor += info->NextEntryOffset;
            }
        }
        ::CloseHandle(overlapped.hEvent);
    }

    std::filesystem::path path_ {};
    std::wstring filename_ {};
    HANDLE dir_handle_ { INVALID_HANDLE_VALUE };
    Callback callback_ {};
    std::atomic<bool> running_ { false };
    std::atomic<std::uint64_t> change_count_ { 0 };
    std::thread thread_;
};
#endif  // _WIN32

#if defined(__linux__)
// Linux native watcher: inotify on the parent directory, filtered to
// IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE for the
// watched filename. An eventfd wakes the poll loop on stop() for
// deterministic teardown.
class InotifyNativeWatcher final : public IFileWatcher
{
public:
    InotifyNativeWatcher() noexcept = default;
    ~InotifyNativeWatcher() override { stop(); }

    [[nodiscard]] cd::core::Result<void> watch(std::string_view path, Callback cb) override
    {
        if (running_.load(std::memory_order_acquire))
            return std::unexpected(
                watcher_errors::make(watcher_errors::Code::kAlreadyWatching));
        const std::filesystem::path fs_path { std::string { path } };
        if (!std::filesystem::exists(fs_path))
            return std::unexpected(
                watcher_errors::make(watcher_errors::Code::kFileNotFound));
        filename_ = fs_path.filename().string();
        const auto dir = fs_path.parent_path().string();

        inotify_fd_ = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (inotify_fd_ < 0)
            return std::unexpected(
                watcher_errors::make(watcher_errors::Code::kBackendError,
                                     "inotify_init1 failed"));
        watch_id_ = ::inotify_add_watch(
            inotify_fd_, dir.c_str(),
            IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
        if (watch_id_ < 0)
        {
            ::close(inotify_fd_);
            inotify_fd_ = -1;
            return std::unexpected(
                watcher_errors::make(watcher_errors::Code::kBackendError,
                                     "inotify_add_watch failed"));
        }
        stop_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (stop_fd_ < 0)
        {
            ::inotify_rm_watch(inotify_fd_, watch_id_);
            ::close(inotify_fd_);
            inotify_fd_ = -1;
            return std::unexpected(
                watcher_errors::make(watcher_errors::Code::kBackendError,
                                     "eventfd failed"));
        }
        callback_ = std::move(cb);
        change_count_.store(0, std::memory_order_release);
        running_.store(true, std::memory_order_release);
        thread_ = std::thread { [this] { run_(); } };
        return {};
    }

    void stop() override
    {
        bool was_running = true;
        if (!running_.compare_exchange_strong(was_running, false))
            return;
        if (stop_fd_ >= 0)
        {
            const std::uint64_t v = 1;
            (void)::write(stop_fd_, &v, sizeof(v));
        }
        if (thread_.joinable())
            thread_.join();
        if (watch_id_ >= 0 && inotify_fd_ >= 0)
            ::inotify_rm_watch(inotify_fd_, watch_id_);
        if (inotify_fd_ >= 0)
            ::close(inotify_fd_);
        if (stop_fd_ >= 0)
            ::close(stop_fd_);
        inotify_fd_ = -1;
        watch_id_ = -1;
        stop_fd_ = -1;
        callback_ = nullptr;
    }

    [[nodiscard]] bool is_watching() const noexcept override
    {
        return running_.load(std::memory_order_acquire);
    }

    void poll_once() override
    {
        // No-op on the native impl — the inotify event arrives via the
        // background thread.
    }

    [[nodiscard]] std::uint64_t change_count() const noexcept override
    {
        return change_count_.load(std::memory_order_acquire);
    }

private:
    void run_()
    {
        // inotify_event has a trailing variable-length name; the read
        // buffer must accommodate at least sizeof(inotify_event) +
        // NAME_MAX + 1. 4 KiB covers a busy directory comfortably.
        constexpr std::size_t kBufBytes = 4096;
        auto buf = std::make_unique<char[]>(kBufBytes);
        struct pollfd fds[2] {};
        fds[0].fd = inotify_fd_;
        fds[0].events = POLLIN;
        fds[1].fd = stop_fd_;
        fds[1].events = POLLIN;
        while (running_.load(std::memory_order_acquire))
        {
            const int rc = ::poll(fds, 2, -1);
            if (rc < 0)
                break;
            if ((fds[1].revents & POLLIN) != 0)
                return;
            if ((fds[0].revents & POLLIN) == 0)
                continue;

            const ssize_t n = ::read(inotify_fd_, buf.get(), kBufBytes);
            if (n <= 0)
                continue;
            ssize_t cursor = 0;
            while (cursor < n)
            {
                const auto* evt =
                    reinterpret_cast<const struct inotify_event*>(buf.get() + cursor);
                if (evt->len > 0 && filename_ == evt->name)
                {
                    change_count_.fetch_add(1, std::memory_order_release);
                    if (callback_)
                        callback_();
                    // One fire per batch is plenty.
                    break;
                }
                cursor += static_cast<ssize_t>(sizeof(struct inotify_event)) + evt->len;
            }
        }
    }

    std::string filename_ {};
    int inotify_fd_ { -1 };
    int watch_id_ { -1 };
    int stop_fd_ { -1 };
    Callback callback_ {};
    std::atomic<bool> running_ { false };
    std::atomic<std::uint64_t> change_count_ { 0 };
    std::thread thread_;
};
#endif  // __linux__

#if defined(__APPLE__)
// macOS native watcher: FSEvents on the parent directory with
// kFSEventStreamCreateFlagFileEvents so the callback receives
// per-file paths (not just the parent directory). The watcher owns
// a dedicated thread that runs a CFRunLoop; stop() asks the loop to
// exit from the outside via CFRunLoopStop().
class FSEventsNativeWatcher final : public IFileWatcher
{
public:
    FSEventsNativeWatcher() noexcept = default;
    ~FSEventsNativeWatcher() override { stop(); }

    [[nodiscard]] cd::core::Result<void> watch(std::string_view path, Callback cb) override
    {
        if (running_.load(std::memory_order_acquire))
            return std::unexpected(
                watcher_errors::make(watcher_errors::Code::kAlreadyWatching));
        const std::filesystem::path fs_path { std::string { path } };
        if (!std::filesystem::exists(fs_path))
            return std::unexpected(
                watcher_errors::make(watcher_errors::Code::kFileNotFound));
        filename_ = fs_path.filename().string();
        watched_path_ = fs_path.string();
        const auto dir = fs_path.parent_path().string();

        callback_ = std::move(cb);
        change_count_.store(0, std::memory_order_release);
        // Hand the directory string to the worker thread; it owns the
        // CFRunLoop + stream lifetime entirely so we don't have to
        // marshal Core Foundation objects across threads.
        dir_path_ = dir;
        ready_ = false;
        thread_ = std::thread { [this] { run_(); } };
        // Wait briefly for the worker to install its run loop so a
        // very fast stop() right after watch() finds something to
        // tear down.
        {
            std::unique_lock guard { ready_mu_ };
            ready_cv_.wait_for(guard, std::chrono::milliseconds { 500 },
                                [this] { return ready_; });
        }
        running_.store(true, std::memory_order_release);
        return {};
    }

    void stop() override
    {
        bool was_running = true;
        if (!running_.compare_exchange_strong(was_running, false))
            return;
        if (run_loop_ != nullptr)
            ::CFRunLoopStop(run_loop_);
        if (thread_.joinable())
            thread_.join();
        callback_ = nullptr;
    }

    [[nodiscard]] bool is_watching() const noexcept override
    {
        return running_.load(std::memory_order_acquire);
    }

    void poll_once() override
    {
        // No-op on the native impl — FSEvents callback arrives on the
        // worker's CFRunLoop.
    }

    [[nodiscard]] std::uint64_t change_count() const noexcept override
    {
        return change_count_.load(std::memory_order_acquire);
    }

private:
    static void event_cb_(ConstFSEventStreamRef /*stream*/, void* info,
                          std::size_t num_events, void* event_paths,
                          const FSEventStreamEventFlags* /*flags*/,
                          const FSEventStreamEventId* /*ids*/)
    {
        auto* self = static_cast<FSEventsNativeWatcher*>(info);
        const auto** paths = const_cast<const char**>(static_cast<char**>(event_paths));
        for (std::size_t i = 0; i < num_events; ++i)
        {
            const std::string_view p { paths[i] };
            if (p.find(self->filename_) != std::string_view::npos)
            {
                self->change_count_.fetch_add(1, std::memory_order_release);
                if (self->callback_)
                    self->callback_();
                break;  // one fire per batch
            }
        }
    }

    void run_()
    {
        run_loop_ = ::CFRunLoopGetCurrent();
        CFStringRef path_cf = ::CFStringCreateWithCString(
            kCFAllocatorDefault, dir_path_.c_str(), kCFStringEncodingUTF8);
        const void* paths_array[] = { path_cf };
        CFArrayRef paths = ::CFArrayCreate(kCFAllocatorDefault, paths_array, 1, &kCFTypeArrayCallBacks);

        FSEventStreamContext ctx {};
        ctx.info = this;
        stream_ = ::FSEventStreamCreate(
            kCFAllocatorDefault, &event_cb_, &ctx, paths,
            kFSEventStreamEventIdSinceNow, /*latency=*/0.1,
            kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer);
        ::CFRelease(paths);
        ::CFRelease(path_cf);

        if (stream_ != nullptr)
        {
            ::FSEventStreamScheduleWithRunLoop(stream_, run_loop_, kCFRunLoopDefaultMode);
            ::FSEventStreamStart(stream_);
        }

        {
            std::lock_guard guard { ready_mu_ };
            ready_ = true;
        }
        ready_cv_.notify_all();

        ::CFRunLoopRun();

        if (stream_ != nullptr)
        {
            ::FSEventStreamStop(stream_);
            ::FSEventStreamInvalidate(stream_);
            ::FSEventStreamRelease(stream_);
            stream_ = nullptr;
        }
        run_loop_ = nullptr;
    }

    std::string filename_ {};
    std::string watched_path_ {};
    std::string dir_path_ {};
    FSEventStreamRef stream_ { nullptr };
    CFRunLoopRef run_loop_ { nullptr };
    Callback callback_ {};
    std::atomic<bool> running_ { false };
    std::atomic<std::uint64_t> change_count_ { 0 };
    std::mutex ready_mu_;
    std::condition_variable ready_cv_;
    bool ready_ { false };
    std::thread thread_;
};
#endif  // __APPLE__

}  // namespace

std::unique_ptr<IFileWatcher>
make_polling_file_watcher(std::chrono::milliseconds interval)
{
    return std::make_unique<PollingWatcher>(interval);
}

std::unique_ptr<IFileWatcher> make_native_file_watcher()
{
#if defined(_WIN32)
    return std::make_unique<Win32NativeWatcher>();
#elif defined(__linux__)
    return std::make_unique<InotifyNativeWatcher>();
#elif defined(__APPLE__)
    return std::make_unique<FSEventsNativeWatcher>();
#else
    // Unknown platform: polling fallback.
    return make_polling_file_watcher();
#endif
}

}  // namespace cd::plugin
