// =============================================================================
// CHROMODYNAMIC — cd::plugin tests (Sprint S2.4)
//
// Note: full end-to-end loader tests (compile a tiny test DLL, dlopen it) live
// in Sprint S2.4+. v1 here exercises the error paths and the IPlugin interface
// surface that callers will implement.
// =============================================================================
#include <cd/plugin/HotReload.hpp>
#include <cd/plugin/IPlugin.hpp>
#include <cd/plugin/Loader.hpp>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

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

// -----------------------------------------------------------------------------
// HotReload — Wave 35
// Tests use injected mock load + version functions so we don't need a
// real on-disk DLL. The mocks fabricate a LoadedPlugin around a
// trivial IPlugin implementation.
// -----------------------------------------------------------------------------

class FakePlugin final : public cd::plugin::IPlugin
{
public:
    explicit FakePlugin(std::uint32_t id) noexcept : id_ { id } {}
    [[nodiscard]] cd::plugin::PluginInfo info() const noexcept override
    {
        return { "fake", "1.0", "test", cd::plugin::kAbiVersion };
    }
    [[nodiscard]] bool initialize() noexcept override { return true; }
    void shutdown() noexcept override {}
    [[nodiscard]] std::uint32_t id() const noexcept { return id_; }

private:
    std::uint32_t id_;
};

namespace
{

cd::plugin::LoadedPlugin make_fake_loaded(std::uint32_t id, std::string path)
{
    cd::plugin::LoadedPlugin lp;
    lp.instance = std::make_unique<FakePlugin>(id);
    lp.native_handle = nullptr;
    lp.path = std::move(path);
    return lp;
}

}  // namespace

TEST(HotReload, MountThenPollNoChangeReturnsFalse)
{
    std::uint64_t fake_version = 42;
    std::uint32_t next_id = 0;
    cd::plugin::HotReloader hr {
        [&](std::string_view p) -> cd::core::Result<cd::plugin::LoadedPlugin> {
            return make_fake_loaded(++next_id, std::string { p });
        },
        [&](std::string_view) { return fake_version; }
    };
    ASSERT_TRUE(hr.mount("dummy/path").has_value());
    EXPECT_TRUE(hr.is_mounted());
    EXPECT_EQ(hr.current_version(), 42U);
    EXPECT_EQ(hr.reload_count(), 0U);
    auto r = hr.poll();
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(*r);
    EXPECT_EQ(hr.reload_count(), 0U);
}

TEST(HotReload, VersionChangeTriggersReload)
{
    std::uint64_t fake_version = 1;
    std::uint32_t next_id = 0;
    cd::plugin::HotReloader hr {
        [&](std::string_view p) -> cd::core::Result<cd::plugin::LoadedPlugin> {
            return make_fake_loaded(++next_id, std::string { p });
        },
        [&](std::string_view) { return fake_version; }
    };
    ASSERT_TRUE(hr.mount("plugin.dll").has_value());
    auto* first = dynamic_cast<FakePlugin*>(hr.current());
    ASSERT_NE(first, nullptr);
    const auto first_id = first->id();

    fake_version = 2;  // simulate disk update
    auto r = hr.poll();
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(*r);
    EXPECT_EQ(hr.reload_count(), 1U);
    auto* second = dynamic_cast<FakePlugin*>(hr.current());
    ASSERT_NE(second, nullptr);
    EXPECT_NE(second->id(), first_id);  // fresh instance
    EXPECT_EQ(hr.current_version(), 2U);
}

TEST(HotReload, BeforeUnloadAndAfterLoadFireInOrder)
{
    std::uint64_t fake_version = 100;
    std::uint32_t next_id = 0;
    cd::plugin::HotReloader hr {
        [&](std::string_view p) -> cd::core::Result<cd::plugin::LoadedPlugin> {
            return make_fake_loaded(++next_id, std::string { p });
        },
        [&](std::string_view) { return fake_version; }
    };
    int unload_calls = 0;
    int load_calls = 0;
    hr.set_before_unload([&](cd::plugin::IPlugin&) { ++unload_calls; });
    hr.set_after_load([&](cd::plugin::IPlugin&) { ++load_calls; });

    ASSERT_TRUE(hr.mount("p.dll").has_value());
    EXPECT_EQ(load_calls, 1);  // initial mount → after_load
    EXPECT_EQ(unload_calls, 0);

    fake_version = 200;
    auto r = hr.poll();
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(*r);
    EXPECT_EQ(unload_calls, 1);  // old plugin unloaded
    EXPECT_EQ(load_calls, 2);    // new plugin loaded
}

TEST(HotReload, FailedReloadKeepsPreviousPluginMounted)
{
    std::uint64_t fake_version = 1;
    std::uint32_t next_id = 0;
    bool fail_next = false;
    int unload_calls = 0;
    cd::plugin::HotReloader hr {
        [&](std::string_view p) -> cd::core::Result<cd::plugin::LoadedPlugin> {
            if (fail_next)
                return std::unexpected(
                    cd::plugin::loader_errors::make(cd::plugin::loader_errors::Code::kFactoryFailed));
            return make_fake_loaded(++next_id, std::string { p });
        },
        [&](std::string_view) { return fake_version; }
    };
    hr.set_before_unload([&](cd::plugin::IPlugin&) { ++unload_calls; });
    ASSERT_TRUE(hr.mount("p.dll").has_value());
    const auto first_id = dynamic_cast<FakePlugin*>(hr.current())->id();

    fake_version = 2;
    fail_next = true;
    auto r = hr.poll();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::plugin::loader_errors::Code::kFactoryFailed));
    // The previous plugin must still be live and untouched — the load
    // attempt happens BEFORE the unload, so a failed reload never
    // leaves the host without a plugin.
    ASSERT_TRUE(hr.is_mounted());
    EXPECT_EQ(unload_calls, 0);  // teardown never fired
    EXPECT_EQ(hr.reload_count(), 0U);
    auto* still_first = dynamic_cast<FakePlugin*>(hr.current());
    ASSERT_NE(still_first, nullptr);
    EXPECT_EQ(still_first->id(), first_id);
}

TEST(HotReload, PollBeforeMountReturnsError)
{
    cd::plugin::HotReloader hr {
        [](std::string_view p) -> cd::core::Result<cd::plugin::LoadedPlugin> {
            return make_fake_loaded(1, std::string { p });
        },
        [](std::string_view) { return std::uint64_t { 1 }; }
    };
    auto r = hr.poll();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::plugin::hot_reload_errors::Code::kNotMounted));
}

TEST(HotReload, DefaultPluginVersionReturnsZeroForMissingFile)
{
    EXPECT_EQ(cd::plugin::default_plugin_version("this_path_does_not_exist_zxyq.dll"), 0U);
}

}  // namespace
