// =============================================================================
// CHROMODYNAMIC — cd::asset tests (Sprint S3.2)
// =============================================================================
#include <cd/asset/AssetId.hpp>
#include <cd/asset/AssetRegistry.hpp>
#include <cd/asset/IAssetLoader.hpp>
#include <cd/asset/SchemaRegistry.hpp>
#include <cd/vfs/MemorySource.hpp>
#include <cd/vfs/VirtualFileSystem.hpp>
#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace
{

// --- AssetId --------------------------------------------------------------
TEST(AssetId, EmptyIsNull)
{
    cd::asset::AssetId id;
    EXPECT_FALSE(id.is_valid());
    EXPECT_FALSE(static_cast<bool>(id));
}

TEST(AssetId, HashIsStableAcrossCalls)
{
    auto a = cd::asset::AssetId::from_path("shaders/standard.vert");
    auto b = cd::asset::AssetId::from_path("shaders/standard.vert");
    EXPECT_EQ(a, b);
    EXPECT_NE(a, cd::asset::AssetId::from_path("shaders/standard.frag"));
}

TEST(AssetId, ConstexprUsable)
{
    constexpr auto compile_time = cd::asset::AssetId::from_path("foo/bar.txt");
    static_assert(compile_time.value() != 0);
    auto run_time = cd::asset::AssetId::from_path("foo/bar.txt");
    EXPECT_EQ(compile_time, run_time);
}

// --- AssetRegistry --------------------------------------------------------
class AssetRegistryTest : public ::testing::Test
{
protected:
    cd::vfs::VirtualFileSystem vfs;
    std::shared_ptr<cd::vfs::MemorySource> mem = std::make_shared<cd::vfs::MemorySource>("mem");

    void SetUp() override
    {
        vfs.mount_back(mem);
    }
};

TEST_F(AssetRegistryTest, LoadTextSucceeds)
{
    mem->put_text("foo.txt", "hello");
    cd::asset::AssetRegistry reg { vfs };
    reg.register_loader(std::make_unique<cd::asset::TextAssetLoader>());
    EXPECT_EQ(reg.loader_count(), 1u);

    auto id = reg.load("text", "foo.txt");
    ASSERT_TRUE(id.has_value());
    auto* asset = reg.find(*id);
    ASSERT_NE(asset, nullptr);
    auto* txt = dynamic_cast<cd::asset::TextAsset*>(asset);
    ASSERT_NE(txt, nullptr);
    EXPECT_EQ(txt->text(), "hello");
    EXPECT_EQ(reg.cached_count(), 1u);
}

TEST_F(AssetRegistryTest, RepeatLoadIsCached)
{
    mem->put_text("a.txt", "abc");
    cd::asset::AssetRegistry reg { vfs };
    reg.register_loader(std::make_unique<cd::asset::TextAssetLoader>());

    auto id1 = reg.load("text", "a.txt");
    auto id2 = reg.load("text", "a.txt");
    ASSERT_TRUE(id1.has_value() && id2.has_value());
    EXPECT_EQ(*id1, *id2);
    EXPECT_EQ(reg.cached_count(), 1u);
}

TEST_F(AssetRegistryTest, UnknownTagRejected)
{
    mem->put_text("a.txt", "x");
    cd::asset::AssetRegistry reg { vfs };
    auto r = reg.load("nonexistent", "a.txt");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::asset_errors::Code::kNoLoaderForTag));
}

TEST_F(AssetRegistryTest, VfsMissingProducesError)
{
    cd::asset::AssetRegistry reg { vfs };
    reg.register_loader(std::make_unique<cd::asset::BytesAssetLoader>());
    auto r = reg.load("bytes", "missing.bin");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::asset_errors::Code::kVfsReadFailed));
}

TEST_F(AssetRegistryTest, ReleaseAndClear)
{
    mem->put_text("a.txt", "x");
    mem->put_text("b.txt", "y");
    cd::asset::AssetRegistry reg { vfs };
    reg.register_loader(std::make_unique<cd::asset::TextAssetLoader>());
    auto a = *reg.load("text", "a.txt");
    (void)reg.load("text", "b.txt");
    EXPECT_EQ(reg.cached_count(), 2u);
    reg.release(a);
    EXPECT_EQ(reg.cached_count(), 1u);
    reg.clear();
    EXPECT_EQ(reg.cached_count(), 0u);
}

TEST_F(AssetRegistryTest, InstallByPasses)
{
    cd::asset::AssetRegistry reg { vfs };
    auto id = cd::asset::AssetId::from_path("runtime.tag");
    reg.install(id, std::make_unique<cd::asset::TextAsset>(std::string { "injected" }));
    auto* txt = dynamic_cast<cd::asset::TextAsset*>(reg.find(id));
    ASSERT_NE(txt, nullptr);
    EXPECT_EQ(txt->text(), "injected");
}

// --- SchemaRegistry -------------------------------------------------------
TEST(SchemaRegistry, RegisterAndFind)
{
    cd::asset::SchemaRegistry reg;
    cd::asset::Schema s;
    s.name = "material.standard";
    s.version = 1;
    s.fields = {
        { "albedo",     cd::asset::FieldType::kVec4f,         false },
        { "roughness",  cd::asset::FieldType::kFloat,         false },
        { "normal_map", cd::asset::FieldType::kHandleTexture, true  },
    };
    reg.register_schema(std::move(s));
    EXPECT_EQ(reg.size(), 1u);
    auto* found = reg.find("material.standard");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->fields.size(), 3u);
    EXPECT_NE(found->find_field("albedo"), nullptr);
    EXPECT_EQ(found->find_field("missing"), nullptr);
}

TEST(SchemaRegistry, ValidatePayload)
{
    cd::asset::SchemaRegistry reg;
    cd::asset::Schema s;
    s.name = "event.tick";
    s.version = 1;
    s.fields = {
        { "dt",       cd::asset::FieldType::kFloat,  false },
        { "frame_id", cd::asset::FieldType::kUint64, false },
        { "debug",    cd::asset::FieldType::kString, true  },
    };
    reg.register_schema(std::move(s));

    std::vector<std::string> present_ok { "dt", "frame_id" };
    EXPECT_TRUE(reg.validate_payload("event.tick", present_ok).has_value());

    std::vector<std::string> present_with_optional { "dt", "frame_id", "debug" };
    EXPECT_TRUE(reg.validate_payload("event.tick", present_with_optional).has_value());

    std::vector<std::string> missing_required { "dt" };
    auto bad = reg.validate_payload("event.tick", missing_required);
    ASSERT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().code, static_cast<std::uint32_t>(cd::asset::schema_errors::Code::kMissingRequiredField));
}

TEST(SchemaRegistry, UnknownSchemaRejected)
{
    cd::asset::SchemaRegistry reg;
    auto r = reg.validate_payload("nope", {});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::schema_errors::Code::kUnknownSchema));
}

}  // namespace
