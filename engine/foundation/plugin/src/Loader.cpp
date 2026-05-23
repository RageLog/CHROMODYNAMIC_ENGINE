// =============================================================================
// CHROMODYNAMIC — cd/plugin/Loader.cpp
// =============================================================================
#include <cd/core/Defines.hpp>
#include <cd/plugin/Loader.hpp>

#if CD_OS_WINDOWS
// clang-format off
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// clang-format on
#else
    #include <dlfcn.h>
#endif

#include <string>
#include <utility>

namespace cd::plugin
{

namespace
{

void* platform_load(const std::string& path) noexcept
{
#if CD_OS_WINDOWS
    return static_cast<void*>(::LoadLibraryA(path.c_str()));
#else
    return ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void platform_unload(void* handle) noexcept
{
    if (handle == nullptr)
        return;
#if CD_OS_WINDOWS
    ::FreeLibrary(static_cast<HMODULE>(handle));
#else
    ::dlclose(handle);
#endif
}

void* platform_symbol(void* handle, const char* name) noexcept
{
    if (handle == nullptr)
        return nullptr;
#if CD_OS_WINDOWS
    return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(handle), name));
#else
    return ::dlsym(handle, name);
#endif
}

}  // namespace

LoadedPlugin::~LoadedPlugin()
{
    if (instance)
    {
        instance->shutdown();
        instance.reset();
    }
    if (native_handle)
    {
        platform_unload(native_handle);
        native_handle = nullptr;
    }
}

LoadedPlugin::LoadedPlugin(LoadedPlugin&& other) noexcept
    : instance { std::move(other.instance) }
    , native_handle { std::exchange(other.native_handle, nullptr) }
    , path { std::move(other.path) }
{
}

LoadedPlugin& LoadedPlugin::operator=(LoadedPlugin&& other) noexcept
{
    if (this != &other)
    {
        this->~LoadedPlugin();
        instance = std::move(other.instance);
        native_handle = std::exchange(other.native_handle, nullptr);
        path = std::move(other.path);
    }
    return *this;
}

cd::core::Result<LoadedPlugin> Loader::load(std::string_view path)
{
    std::string spath { path };
    void* handle = platform_load(spath);
    if (handle == nullptr)
    {
        return std::unexpected(loader_errors::make(loader_errors::Code::kLoadFailed, "dlopen/LoadLibrary failed"));
    }
    auto sym = platform_symbol(handle, kEntrySymbol.data());
    if (sym == nullptr)
    {
        platform_unload(handle);
        return std::unexpected(loader_errors::make(loader_errors::Code::kSymbolMissing, kEntrySymbol));
    }
    auto factory = reinterpret_cast<PluginCreateFn>(sym);
    IPlugin* raw = factory();
    if (raw == nullptr)
    {
        platform_unload(handle);
        return std::unexpected(loader_errors::make(loader_errors::Code::kFactoryFailed, "factory returned null"));
    }
    const auto info = raw->info();
    if (info.abi_version != kAbiVersion)
    {
        delete raw;
        platform_unload(handle);
        return std::unexpected(loader_errors::make(loader_errors::Code::kAbiMismatch, "plugin ABI version mismatch"));
    }
    if (!raw->initialize())
    {
        delete raw;
        platform_unload(handle);
        return std::unexpected(
            loader_errors::make(loader_errors::Code::kInitFailed, "plugin initialize() returned false")
        );
    }
    LoadedPlugin lp;
    lp.instance.reset(raw);
    lp.native_handle = handle;
    lp.path = spath;
    loaded_paths_.push_back(spath);
    return lp;
}

}  // namespace cd::plugin
