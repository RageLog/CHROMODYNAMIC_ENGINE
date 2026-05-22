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
#else
    // Linux / macOS native implementations land in follow-up waves.
    // Until then, the polling impl is the canonical fallback so the
    // factory contract ("always non-null") stays intact.
    return make_polling_file_watcher();
#endif
}

}  // namespace cd::plugin
