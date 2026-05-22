// =============================================================================
// CHROMODYNAMIC — cd/plugin/Loader.hpp
// ADR-017 P2 (DfH common/plugin/pluginloader.hpp 315-line salvage, distilled)
//
// RAII DLL/.so loader with ABI version check. Each load produces a
// `LoadedPlugin` whose destruction invokes `shutdown()` and unloads the
// shared library.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/Result.hpp>
#include <cd/plugin/IPlugin.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cd::plugin
{

namespace loader_errors
{
inline constexpr std::uint32_t kDomain = 0x0004;
enum class Code : std::uint32_t
{
    kOk = 0,
    kFileNotFound = 1,
    kLoadFailed = 2,
    kSymbolMissing = 3,
    kAbiMismatch = 4,
    kFactoryFailed = 5,
    kInitFailed = 6,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}
}  // namespace loader_errors

struct LoadedPlugin
{
    std::unique_ptr<IPlugin> instance {};
    void* native_handle { nullptr };  // opaque platform handle (HMODULE / void*)
    std::string path {};

    LoadedPlugin() noexcept = default;
    ~LoadedPlugin();
    LoadedPlugin(const LoadedPlugin&) = delete;
    LoadedPlugin& operator=(const LoadedPlugin&) = delete;
    LoadedPlugin(LoadedPlugin&& other) noexcept;
    LoadedPlugin& operator=(LoadedPlugin&& other) noexcept;
};

class Loader
{
public:
    /// Load `path` (DLL on Windows, .so on Linux, .dylib on macOS), verify ABI,
    /// invoke the factory + initialize() and return the live plugin.
    [[nodiscard]] cd::core::Result<LoadedPlugin> load(std::string_view path);

    [[nodiscard]] const std::vector<std::string>& loaded_paths() const noexcept
    {
        return loaded_paths_;
    }

private:
    std::vector<std::string> loaded_paths_;
};

}  // namespace cd::plugin
