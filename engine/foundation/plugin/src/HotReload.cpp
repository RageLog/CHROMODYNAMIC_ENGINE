// =============================================================================
// CHROMODYNAMIC — engine/foundation/plugin/src/HotReload.cpp
//
// Default filesystem-mtime watcher + reloader factory. See HotReload.hpp.
// =============================================================================
#include <cd/plugin/HotReload.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>

namespace cd::plugin
{

std::uint64_t default_plugin_version(std::string_view path)
{
    std::error_code ec;
    const std::filesystem::path fs_path { std::string { path } };
    const auto mtime = std::filesystem::last_write_time(fs_path, ec);
    if (ec)
        return 0;
    return static_cast<std::uint64_t>(mtime.time_since_epoch().count());
}

HotReloader make_default_hot_reloader(Loader& loader)
{
    Loader* loader_ptr = &loader;
    PluginLoadFn load_fn = [loader_ptr](std::string_view path) {
        return loader_ptr->load(path);
    };
    return HotReloader { std::move(load_fn), &default_plugin_version };
}

}  // namespace cd::plugin
