// =============================================================================
// CHROMODYNAMIC — test_asset_json.cpp
// =============================================================================
#include <cd/asset/json/CVarBridge.hpp>
#include <cd/asset/json/Json.hpp>
#include <cd/core/CVar.hpp>
#include <gtest/gtest.h>

#include <string>
#include <string_view>

using cd::asset::json::Array;
using cd::asset::json::Object;
using cd::asset::json::parse;
using cd::asset::json::serialize;
using cd::asset::json::Value;

TEST(AssetJsonTest, ParseNullTrueFalse)
{
    EXPECT_TRUE(parse("null").value().is_null());
    EXPECT_TRUE(parse("true").value().as_bool());
    EXPECT_FALSE(parse("false").value().as_bool());
}

TEST(AssetJsonTest, ParseNumbers)
{
    EXPECT_EQ(parse("0").value().as_number(), 0.0);
    EXPECT_EQ(parse("42").value().as_number(), 42.0);
    EXPECT_EQ(parse("-7").value().as_number(), -7.0);
    EXPECT_NEAR(parse("3.14").value().as_number(), 3.14, 1e-12);
    EXPECT_NEAR(parse("1e3").value().as_number(), 1000.0, 1e-9);
    EXPECT_NEAR(parse("-2.5e-2").value().as_number(), -0.025, 1e-12);
}

TEST(AssetJsonTest, ParseStringWithEscapes)
{
    auto v = parse(R"json("a\nb\tc\"d")json");
    ASSERT_TRUE(v.has_value()) << v.error().message;
    EXPECT_EQ(v->as_string(), std::string { "a\nb\tc\"d" });
}

TEST(AssetJsonTest, ParseStringWithUnicodeEscape)
{
    auto v = parse(R"json("é")json");
    ASSERT_TRUE(v.has_value()) << v.error().message;
    // U+00E9 (é) encoded as 0xC3 0xA9 in UTF-8.
    EXPECT_EQ(v->as_string(), std::string { "\xC3\xA9" });
}

TEST(AssetJsonTest, ParseEmptyContainers)
{
    EXPECT_TRUE(parse("[]").value().is_array());
    EXPECT_TRUE(parse("[]").value().as_array().empty());
    EXPECT_TRUE(parse("{}").value().is_object());
    EXPECT_TRUE(parse("{}").value().as_object().empty());
}

TEST(AssetJsonTest, ParseNestedObject)
{
    constexpr std::string_view src = R"({
        "name": "scene01",
        "active": true,
        "entities": [
            { "id": 1, "pos": [0.0, 1.5, -3.0] },
            { "id": 2, "pos": [10.0, 0.0, 0.0] }
        ],
        "metadata": null
    })";
    auto v = parse(src);
    ASSERT_TRUE(v.has_value()) << v.error().message;
    ASSERT_TRUE(v->is_object());
    EXPECT_EQ(v->at("name").value()->as_string(), "scene01");
    EXPECT_TRUE(v->at("active").value()->as_bool());
    EXPECT_TRUE(v->at("metadata").value()->is_null());
    auto ents = v->at("entities").value();
    ASSERT_TRUE(ents->is_array());
    ASSERT_EQ(ents->as_array().size(), 2u);
    auto pos1 = ents->at(0).value()->at("pos").value();
    ASSERT_TRUE(pos1->is_array());
    EXPECT_NEAR(pos1->as_array()[1].as_number(), 1.5, 1e-12);
}

TEST(AssetJsonTest, RejectsUnterminatedString)
{
    auto v = parse("\"abc");
    ASSERT_FALSE(v.has_value());
    EXPECT_EQ(v.error().code, static_cast<std::uint32_t>(cd::asset::json::json_errors::Code::kUnterminatedString));
}

TEST(AssetJsonTest, RejectsBadEscape)
{
    auto v = parse(R"json("\x")json");
    ASSERT_FALSE(v.has_value());
    EXPECT_EQ(v.error().code, static_cast<std::uint32_t>(cd::asset::json::json_errors::Code::kBadEscape));
}

TEST(AssetJsonTest, RejectsTrailingGarbage)
{
    auto v = parse("123abc");
    ASSERT_FALSE(v.has_value());
    // Could be kBadNumber (if scanner picks up too much) or kUnexpectedToken.
    EXPECT_TRUE(
        v.error().code == static_cast<std::uint32_t>(cd::asset::json::json_errors::Code::kBadNumber) ||
        v.error().code == static_cast<std::uint32_t>(cd::asset::json::json_errors::Code::kUnexpectedToken)
    );
}

TEST(AssetJsonTest, DepthLimitTriggers)
{
    // 200 nested '[' should bust a max_depth of 32.
    std::string deep;
    for (int i = 0; i < 200; ++i)
        deep += '[';
    deep += "null";
    for (int i = 0; i < 200; ++i)
        deep += ']';
    auto v = parse(deep, 32);
    ASSERT_FALSE(v.has_value());
    EXPECT_EQ(v.error().code, static_cast<std::uint32_t>(cd::asset::json::json_errors::Code::kDepthLimit));
}

TEST(AssetJsonTest, RoundTripCompact)
{
    Value v;
    Object o;
    o["a"] = Value { 1 };
    o["b"] = Value {
        Array { Value { "x" }, Value { false } }
    };
    o["c"] = Value {};  // null
    v = Value { std::move(o) };

    const auto txt = serialize(v, false);
    auto re = parse(txt);
    ASSERT_TRUE(re.has_value()) << re.error().message;
    EXPECT_EQ(*re, v);
}

TEST(AssetJsonTest, RoundTripPretty)
{
    Value v;
    Object o;
    o["greet"] = Value { "hello world" };
    o["items"] = Value {
        Array { Value { 1 }, Value { 2 }, Value { 3 } }
    };
    v = Value { std::move(o) };

    const auto txt = serialize(v, true);
    EXPECT_NE(txt.find('\n'), std::string::npos);
    auto re = parse(txt);
    ASSERT_TRUE(re.has_value()) << re.error().message;
    EXPECT_EQ(*re, v);
}

TEST(AssetJsonTest, EmitsEscapedQuotesAndBackslashes)
{
    Value v { std::string { "say \"hi\" \\path" } };
    const auto txt = serialize(v);
    EXPECT_NE(txt.find("\\\""), std::string::npos);
    EXPECT_NE(txt.find("\\\\"), std::string::npos);
    auto re = parse(txt);
    ASSERT_TRUE(re.has_value()) << re.error().message;
    EXPECT_EQ(re->as_string(), v.as_string());
}

TEST(AssetJsonTest, ObjectKeyLookupFailsCleanly)
{
    auto v = parse(R"({"a": 1})");
    ASSERT_TRUE(v.has_value());
    auto m = v->at("missing");
    ASSERT_FALSE(m.has_value());
    EXPECT_EQ(m.error().code, static_cast<std::uint32_t>(cd::asset::json::json_errors::Code::kKeyNotFound));
}

TEST(CVarBridgeTest, CVarsRoundTripThroughJson)
{
    cd::core::CVarRegistry reg;
    reg.set("render.vsync", cd::core::CVarValue { true });
    reg.set("render.msaa", cd::core::CVarValue { std::int64_t { 4 } });
    reg.set("audio.gain", cd::core::CVarValue { 0.85 });
    reg.set("user.locale", cd::core::CVarValue { std::string { "en-US" } });

    auto j = cd::asset::json::to_json(reg);
    ASSERT_TRUE(j.is_object());
    EXPECT_EQ(j.as_object().size(), 4u);

    cd::core::CVarRegistry reg2;
    // Seed reg2 with same types so the bridge can coerce numbers back to i64.
    reg2.set("render.vsync", cd::core::CVarValue { false });
    reg2.set("render.msaa", cd::core::CVarValue { std::int64_t { 1 } });
    reg2.set("audio.gain", cd::core::CVarValue { 0.0 });
    reg2.set("user.locale", cd::core::CVarValue { std::string {} });

    auto r = cd::asset::json::from_json(j, reg2);
    ASSERT_TRUE(r.has_value()) << r.error().message;

    auto vsync = reg2.get("render.vsync");
    auto msaa = reg2.get("render.msaa");
    auto gain = reg2.get("audio.gain");
    auto loc = reg2.get("user.locale");
    ASSERT_TRUE(vsync && msaa && gain && loc);
    EXPECT_TRUE(std::get<bool>(*vsync));
    EXPECT_EQ(std::get<std::int64_t>(*msaa), 4);
    EXPECT_NEAR(std::get<double>(*gain), 0.85, 1e-12);
    EXPECT_EQ(std::get<std::string>(*loc), "en-US");
}

TEST(CVarBridgeTest, FromJsonRejectsNonObjectRoot)
{
    auto v = cd::asset::json::parse("[1,2,3]");
    ASSERT_TRUE(v.has_value());
    cd::core::CVarRegistry reg;
    auto r = cd::asset::json::from_json(*v, reg);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::json::json_errors::Code::kTypeMismatch));
}

TEST(CVarBridgeTest, FromJsonRejectsNonScalarValues)
{
    auto v = cd::asset::json::parse(R"({"render.msaa":[1,2,3]})");
    ASSERT_TRUE(v.has_value());
    cd::core::CVarRegistry reg;
    auto r = cd::asset::json::from_json(*v, reg);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::json::json_errors::Code::kTypeMismatch));
}

TEST(CVarBridgeTest, FileRoundTripPreservesState)
{
    cd::core::CVarRegistry reg;
    reg.set("debug.draw", cd::core::CVarValue { true });
    reg.set("net.port", cd::core::CVarValue { std::int64_t { 27015 } });
    reg.set("audio.gain", cd::core::CVarValue { 0.42 });

    const auto path = std::string { ::testing::TempDir() } + "/cd_cvar_roundtrip.json";
    auto sv = cd::asset::json::save_to(path, reg);
    ASSERT_TRUE(sv.has_value()) << sv.error().message;

    cd::core::CVarRegistry reg2;
    // Seed with same types so int64 stays int64 after coerce.
    reg2.set("debug.draw", cd::core::CVarValue { false });
    reg2.set("net.port", cd::core::CVarValue { std::int64_t { 0 } });
    reg2.set("audio.gain", cd::core::CVarValue { 0.0 });

    auto ld = cd::asset::json::load_into(path, reg2);
    ASSERT_TRUE(ld.has_value()) << ld.error().message;
    EXPECT_TRUE(std::get<bool>(*reg2.get("debug.draw")));
    EXPECT_EQ(std::get<std::int64_t>(*reg2.get("net.port")), 27015);
    EXPECT_NEAR(std::get<double>(*reg2.get("audio.gain")), 0.42, 1e-12);
}

TEST(CVarBridgeTest, LoadIntoMissingFileFails)
{
    cd::core::CVarRegistry reg;
    auto r = cd::asset::json::load_into("nonexistent_zfile_qq.json", reg);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::json::json_errors::Code::kFileNotFound));
}

#include <cd/asset/json/AssetLoader.hpp>

TEST(JsonAssetLoader, AdapterDecodesValidJson)
{
    constexpr std::string_view text = R"({"a":1,"b":[true,null,"x"]})";
    cd::asset::json::JsonAssetLoader loader;
    EXPECT_EQ(loader.tag(), "json");
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    auto r = loader.decode(std::span<const std::byte> { bytes.data(), bytes.size() }, "x.json");
    ASSERT_TRUE(r.has_value()) << r.error().message;
    auto* j = dynamic_cast<cd::asset::json::JsonAsset*>(r->get());
    ASSERT_NE(j, nullptr);
    EXPECT_TRUE(j->value().is_object());
    EXPECT_EQ(j->tag(), "json");
}

TEST(JsonAssetLoader, AdapterPropagatesParseError)
{
    constexpr std::string_view text = R"({"a":)";  // truncated
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    cd::asset::json::JsonAssetLoader loader;
    auto r = loader.decode(std::span<const std::byte> { bytes.data(), bytes.size() }, "x.json");
    ASSERT_FALSE(r.has_value());
}

TEST(CVarBridgeTest, NullValueLeavesCVarUntouched)
{
    cd::core::CVarRegistry reg;
    reg.set("keep_me", cd::core::CVarValue { std::int64_t { 99 } });

    auto v = cd::asset::json::parse(R"({"keep_me": null})");
    ASSERT_TRUE(v.has_value());
    auto r = cd::asset::json::from_json(*v, reg);
    ASSERT_TRUE(r.has_value());
    auto kept = reg.get("keep_me");
    ASSERT_TRUE(kept);
    EXPECT_EQ(std::get<std::int64_t>(*kept), 99);
}
