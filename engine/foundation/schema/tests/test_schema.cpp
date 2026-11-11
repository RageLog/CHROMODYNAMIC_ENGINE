// =============================================================================
// CHROMODYNAMIC — test_schema.cpp
// Unit tests for cd::schema (TypeSchema, member reflection, range validation,
// and JSON Schema Draft 2020-12 AI export).
// =============================================================================
#include <cd/schema/Schema.hpp>

#include <gtest/gtest.h>

namespace
{

enum class SampleBlend : std::uint8_t
{
    kAlpha    = 0,
    kAdditive = 1,
    kMultiply = 2,
};

struct SampleSpriteComponent
{
    bool            visible { true };
    float           opacity { 1.0F };
    cd::math::Vec2f position { 2.0F, 4.0F };
    cd::math::Vec4f tint { 1.0F, 1.0F, 1.0F, 1.0F };
    std::string     texture_path { "sprites/hero.png" };
    SampleBlend     blend { SampleBlend::kAlpha };
};

[[nodiscard]] cd::schema::TypeSchema make_sample_schema()
{
    cd::schema::TypeSchema ts {};
    ts.type_name   = "SampleSpriteComponent";
    ts.category    = "2D";
    ts.description = "2D sprite visual properties";
    ts.fields.push_back(cd::schema::make_member_field(
        "visible", "Visible", cd::schema::FieldKind::kBool,
        &SampleSpriteComponent::visible, true, "Toggle visibility"));
    ts.fields.push_back(cd::schema::make_member_field(
        "opacity", "Opacity", cd::schema::FieldKind::kFloat,
        &SampleSpriteComponent::opacity, 1.0F, "Alpha opacity", true, 0.0F, 1.0F, 0.05F));
    ts.fields.push_back(cd::schema::make_member_field(
        "position", "Position", cd::schema::FieldKind::kVec2f,
        &SampleSpriteComponent::position, cd::math::Vec2f { 0.0F, 0.0F }, "World XY"));
    ts.fields.push_back(cd::schema::make_member_field(
        "tint", "Tint RGBA", cd::schema::FieldKind::kColorRgba,
        &SampleSpriteComponent::tint, cd::math::Vec4f { 1.0F, 1.0F, 1.0F, 1.0F }, "Color tint"));
    ts.fields.push_back(cd::schema::make_member_field(
        "texture_path", "Texture", cd::schema::FieldKind::kAssetPath,
        &SampleSpriteComponent::texture_path, std::string { "" }, "Sprite asset path"));
    ts.fields.push_back(cd::schema::make_member_field(
        "blend", "Blend Mode", cd::schema::FieldKind::kEnum,
        &SampleSpriteComponent::blend, std::int32_t { 0 }, "Blend equation",
        false, 0.0F, 2.0F, 1.0F, { "Alpha", "Additive", "Multiply" }));
    return ts;
}

TEST(SchemaTest, ReadsAndWritesTypedMemberFieldsWithRangeClamping)
{
    const auto ts = make_sample_schema();
    SampleSpriteComponent comp {};

    const auto cur_pos = ts.read(comp, "position");
    ASSERT_TRUE(cur_pos.has_value());
    EXPECT_EQ(std::get<cd::math::Vec2f>(*cur_pos), (cd::math::Vec2f { 2.0F, 4.0F }));

    ASSERT_TRUE(ts.write(comp, "opacity", 2.75F, true).has_value());
    EXPECT_FLOAT_EQ(comp.opacity, 1.0F);

    const auto strict_err = ts.write(comp, "opacity", -0.5F, false);
    EXPECT_FALSE(strict_err.has_value());

    const auto mismatch_err = ts.write(comp, "visible", 0.5F, true);
    EXPECT_FALSE(mismatch_err.has_value());

    ASSERT_TRUE(ts.write(comp, "blend", std::int32_t { 2 }, false).has_value());
    EXPECT_EQ(comp.blend, SampleBlend::kMultiply);
}

TEST(SchemaTest, RegistryValidatesAndExportsDraft2020JsonSchema)
{
    cd::schema::SchemaRegistry reg {};
    reg.register_type(make_sample_schema());
    reg.register_command(cd::schema::CommandSchema {
        .op_name    = "spawn_entity_2d",
        .summary    = "Spawn a 2D entity onto the active Stage2D",
        .reversible = true,
        .params     = {
            { "name", cd::schema::FieldKind::kString, true, "Entity display name" },
            { "pos", cd::schema::FieldKind::kVec2f, false, "Initial XY coordinates" },
        },
    });

    EXPECT_TRUE(reg.validate_value("SampleSpriteComponent", "opacity", 0.65F).has_value());
    EXPECT_FALSE(reg.validate_value("SampleSpriteComponent", "opacity", 4.0F).has_value());
    EXPECT_FALSE(reg.validate_value("UnknownType", "opacity", 0.5F).has_value());

    const std::string json_schema = reg.export_json_schema_draft2020("Test Engine Schema");
    EXPECT_NE(json_schema.find("https://json-schema.org/draft/2020-12/schema"), std::string::npos);
    EXPECT_NE(json_schema.find("\"SampleSpriteComponent\""), std::string::npos);
    EXPECT_NE(json_schema.find("\"spawn_entity_2d\""), std::string::npos);
    EXPECT_NE(json_schema.find("\"x-cd-enum-labels\": [\"Alpha\", \"Additive\", \"Multiply\"]"), std::string::npos);
}

}  // namespace
