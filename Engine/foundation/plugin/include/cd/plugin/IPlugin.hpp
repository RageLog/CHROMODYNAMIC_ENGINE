// =============================================================================
// CHROMODYNAMIC — cd/plugin/IPlugin.hpp
// ADR-017 P2 (DfH common/plugin/iplugin.hpp + pluginloader.hpp salvage,
// distilled. ABI gate + manifest cross-check + RAII handle).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <string_view>

namespace cd::plugin
{

/// Bumped whenever the plugin C ABI (IPlugin vtable + entry symbol) changes.
inline constexpr std::uint32_t kAbiVersion = 1;

/// Standard plugin entry-point symbol — `cd_plugin_create` must be exported
/// from every plugin DLL/.so as `extern "C"`.
inline constexpr std::string_view kEntrySymbol = "cd_plugin_create";

struct PluginInfo
{
    std::string_view name {};
    std::string_view version {};
    std::string_view description {};
    std::uint32_t abi_version { kAbiVersion };
};

/// Base interface every plugin implements. Concrete plugins extend this with
/// their own virtual surface (renderer backend, asset importer, …).
class IPlugin
{
public:
    IPlugin() noexcept = default;
    virtual ~IPlugin() = default;
    IPlugin(const IPlugin&) = delete;
    IPlugin& operator=(const IPlugin&) = delete;
    IPlugin(IPlugin&&) = delete;
    IPlugin& operator=(IPlugin&&) = delete;

    [[nodiscard]] virtual PluginInfo info() const noexcept = 0;
    [[nodiscard]] virtual bool initialize() noexcept = 0;
    virtual void shutdown() noexcept = 0;
};

/// Signature of the exported entry point. Returns a heap-allocated plugin
/// owned by the loader (loader deletes via `delete`).
using PluginCreateFn = IPlugin* (*)();

}  // namespace cd::plugin
