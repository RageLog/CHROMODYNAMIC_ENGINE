// =============================================================================
// CHROMODYNAMIC — samples/hello_watch
//
// Demonstrates the cd::plugin file watcher: writes a temp file, watches
// it with both the polling and the native-best-available watchers,
// rewrites the file a few times, and prints when each watcher fires.
//
// Headless / smoke-safe: terminates after a fixed wall-clock budget and
// closes both watchers explicitly so no background thread leaks.
// =============================================================================
#include <cd/plugin/FileWatcher.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>

namespace
{

class TempFile
{
public:
    TempFile()
    {
        path_ = std::filesystem::temp_directory_path()
              / ("cd_hello_watch_" + std::to_string(static_cast<unsigned long long>(
                                         std::chrono::steady_clock::now().time_since_epoch().count()))
                 + ".bin");
        write("v0");
    }
    ~TempFile()
    {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
    void write(std::string_view contents)
    {
        std::ofstream out { path_, std::ios::binary | std::ios::trunc };
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        out.close();
        std::error_code ec;
        std::filesystem::last_write_time(
            path_,
            std::filesystem::file_time_type::clock::now() + std::chrono::seconds { 1 },
            ec);
    }
    [[nodiscard]] std::string path_str() const { return path_.string(); }

private:
    std::filesystem::path path_;
};

}  // namespace

int main()
{
    std::printf("=== hello_watch — cd::plugin file watcher demo ===\n");

    TempFile f;
    std::printf("  watching: %s\n", f.path_str().c_str());

    auto poll_watcher = cd::plugin::make_polling_file_watcher(
        std::chrono::milliseconds { 30 });
    auto native_watcher = cd::plugin::make_native_file_watcher();

    std::atomic<int> poll_hits { 0 };
    std::atomic<int> native_hits { 0 };

    auto r1 = poll_watcher->watch(f.path_str(),
                                  [&] { ++poll_hits; });
    if (!r1.has_value())
    {
        std::printf("[hello_watch] polling watch failed\n");
        return 1;
    }
    auto r2 = native_watcher->watch(f.path_str(),
                                    [&] { ++native_hits; });
    if (!r2.has_value())
    {
        std::printf("[hello_watch] native watch failed\n");
        return 1;
    }

    // Rewrite the file a few times. Pause between rewrites so each one
    // produces a distinct mtime (the watcher's polling cadence is 30 ms).
    constexpr int kRewrites = 3;
    for (int i = 1; i <= kRewrites; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds { 80 });
        f.write("v" + std::to_string(i));
        std::printf("  rewrite #%d\n", i);
    }

    // Give the watchers a window to fire.
    std::this_thread::sleep_for(std::chrono::milliseconds { 200 });

    poll_watcher->stop();
    native_watcher->stop();

    std::printf("\n=== Summary ===\n");
    std::printf("  polling watcher fires : %d\n", poll_hits.load());
    std::printf("  polling change_count  : %llu\n",
                static_cast<unsigned long long>(poll_watcher->change_count()));
    std::printf("  native  watcher fires : %d\n", native_hits.load());
    std::printf("  native  change_count  : %llu\n",
                static_cast<unsigned long long>(native_watcher->change_count()));
    std::printf("[hello_watch] done\n");
    return 0;
}
