// =============================================================================
// CHROMODYNAMIC — cd::plugin tests (Sprint S2.4)
//
// Note: full end-to-end loader tests (compile a tiny test DLL, dlopen it) live
// in Sprint S2.4+. v1 here exercises the error paths and the IPlugin interface
// surface that callers will implement.
// =============================================================================
#include <cd/plugin/IPlugin.hpp>
#include <cd/plugin/Loader.hpp>
#include <gtest/gtest.h>

namespace
{

TEST(PluginIPlugin, AbiVersionStable)
{
    EXPECT_GE(cd::plugin::kAbiVersion, 1u);
}

TEST(PluginIPlugin, EntrySymbolNonEmpty)
{
    EXPECT_FALSE(cd::plugin::kEntrySymbol.empty());
}

TEST(PluginLoader, LoadNonexistentFails)
{
    cd::plugin::Loader loader;
    auto r = loader.load("nonexistent_plugin_zxyq.dll");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::plugin::loader_errors::Code::kLoadFailed));
}

TEST(PluginLoader, LoadedPathsTracksSuccesses)
{
    cd::plugin::Loader loader;
    EXPECT_TRUE(loader.loaded_paths().empty());
    auto r = loader.load("definitely-not-here.so");
    EXPECT_FALSE(r.has_value());
    EXPECT_TRUE(loader.loaded_paths().empty());
}

}  // namespace
