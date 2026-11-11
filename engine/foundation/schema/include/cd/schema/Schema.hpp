// =============================================================================
// CHROMODYNAMIC — cd/schema/Schema.hpp
// FAZ 33 (ADR-20261111) — Self-Describing C++23 Type, Property & AI Command
// Reflection Schema Layer (Level 0 Foundation).
//
// Single source of truth for:
//   1. Automatic ChromaUI Inspector widget generation (sliders, drag-floats,
//      color pickers, enum combos, asset paths, tooltips, min/max clamping).
//   2. Lossless typed field read/write and range/type validation (`Result<void>`).
//   3. Zero-hallucination AI Agent introspection via Draft 2020-12 JSON Schema
//      export (`--dump-schema`).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace cd::schema
{

namespace schema_errors
{
inline constexpr std::uint32_t kDomain = 0x002A;

enum class Code : std::uint32_t
{
    kOk              = 0,
    kUnknownType     = 1,
    kUnknownField    = 2,
    kTypeMismatch    = 3,
    kOutOfRange      = 4,
    kReadOnlyField   = 5,
    kInvalidArgument = 6,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view msg = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), msg };
}
}  // namespace schema_errors

enum class FieldKind : std::uint8_t
{
    kBool      = 0,
    kInt32     = 1,
    kFloat     = 2,
    kVec2f     = 3,
    kVec3f     = 4,
    kColorRgba = 5,
    kString    = 6,
    kAssetPath = 7,
    kEnum      = 8,
};

[[nodiscard]] constexpr std::string_view field_kind_name(FieldKind kind) noexcept
{
    switch (kind)
    {
        case FieldKind::kBool:      return "bool";
        case FieldKind::kInt32:     return "int32";
        case FieldKind::kFloat:     return "float";
        case FieldKind::kVec2f:     return "vec2f";
        case FieldKind::kVec3f:     return "vec3f";
        case FieldKind::kColorRgba: return "color_rgba";
        case FieldKind::kString:    return "string";
        case FieldKind::kAssetPath: return "asset_path";
        case FieldKind::kEnum:      return "enum";
    }
    return "unknown";
}

using FieldValue = std::variant<
    bool,
    std::int32_t,
    float,
    cd::math::Vec2f,
    cd::math::Vec3f,
    cd::math::Vec4f,
    std::string>;

[[nodiscard]] bool field_value_matches_kind(const FieldValue& value, FieldKind kind) noexcept;

struct FieldDesc
{
    std::string              name {};
    std::string              label {};
    std::string              tooltip {};
    FieldKind                kind { FieldKind::kFloat };
    bool                     has_range { false };
    float                    min_value { 0.0F };
    float                    max_value { 1.0F };
    float                    step { 0.05F };
    std::vector<std::string> enum_labels {};
    FieldValue               default_value { 0.0F };

    std::function<FieldValue(const void*)>       getter {};
    std::function<void(void*, const FieldValue&)> setter {};
};

/// Type-safe C++23 pointer-to-member field descriptor builder (zero macros).
template <typename OwnerT, typename MemberT>
[[nodiscard]] FieldDesc make_member_field(
    std::string              name,
    std::string              label,
    FieldKind                kind,
    MemberT OwnerT::*        member_ptr,
    FieldValue               default_val,
    std::string              tooltip     = {},
    bool                     has_range   = false,
    float                    min_val     = 0.0F,
    float                    max_val     = 1.0F,
    float                    step_val    = 0.05F,
    std::vector<std::string> enum_labels = {})
{
    FieldDesc d {};
    d.name          = std::move(name);
    d.label         = std::move(label);
    d.tooltip       = std::move(tooltip);
    d.kind          = kind;
    d.has_range     = has_range;
    d.min_value     = min_val;
    d.max_value     = max_val;
    d.step          = step_val;
    d.enum_labels   = std::move(enum_labels);
    d.default_value = std::move(default_val);

    d.getter = [member_ptr](const void* raw) -> FieldValue {
        const auto* obj = static_cast<const OwnerT*>(raw);
        if constexpr (std::is_enum_v<MemberT>)
        {
            return static_cast<std::int32_t>(obj->*member_ptr);
        }
        else if constexpr (std::is_same_v<MemberT, std::uint32_t> || std::is_same_v<MemberT, int>)
        {
            return static_cast<std::int32_t>(obj->*member_ptr);
        }
        else
        {
            return FieldValue { obj->*member_ptr };
        }
    };

    d.setter = [member_ptr](void* raw, const FieldValue& val) {
        auto* obj = static_cast<OwnerT*>(raw);
        if constexpr (std::is_enum_v<MemberT>)
        {
            if (const auto* iv = std::get_if<std::int32_t>(&val))
            {
                obj->*member_ptr = static_cast<MemberT>(*iv);
            }
        }
        else if constexpr (std::is_same_v<MemberT, bool>)
        {
            if (const auto* bv = std::get_if<bool>(&val))
            {
                obj->*member_ptr = *bv;
            }
        }
        else if constexpr (std::is_same_v<MemberT, std::int32_t> || std::is_same_v<MemberT, std::uint32_t>)
        {
            if (const auto* iv = std::get_if<std::int32_t>(&val))
            {
                obj->*member_ptr = static_cast<MemberT>(*iv);
            }
        }
        else if constexpr (std::is_same_v<MemberT, float>)
        {
            if (const auto* fv = std::get_if<float>(&val))
            {
                obj->*member_ptr = *fv;
            }
        }
        else if constexpr (std::is_same_v<MemberT, cd::math::Vec2f>)
        {
            if (const auto* v2 = std::get_if<cd::math::Vec2f>(&val))
            {
                obj->*member_ptr = *v2;
            }
        }
        else if constexpr (std::is_same_v<MemberT, cd::math::Vec3f>)
        {
            if (const auto* v3 = std::get_if<cd::math::Vec3f>(&val))
            {
                obj->*member_ptr = *v3;
            }
        }
        else if constexpr (std::is_same_v<MemberT, cd::math::Vec4f>)
        {
            if (const auto* v4 = std::get_if<cd::math::Vec4f>(&val))
            {
                obj->*member_ptr = *v4;
            }
        }
        else if constexpr (std::is_same_v<MemberT, std::string>)
        {
            if (const auto* sv = std::get_if<std::string>(&val))
            {
                obj->*member_ptr = *sv;
            }
        }
    };

    return d;
}

class TypeSchema
{
public:
    std::string            type_name {};
    std::string            category {};
    std::string            description {};
    std::uint32_t          version { 1U };
    std::vector<FieldDesc> fields {};

    [[nodiscard]] const FieldDesc* find_field(std::string_view field_name) const noexcept;

    [[nodiscard]] cd::core::Result<FieldValue>
    read_raw(const void* instance, std::string_view field_name) const;

    [[nodiscard]] cd::core::Result<void>
    write_raw(void* instance, std::string_view field_name, FieldValue value, bool clamp_ranges = true) const;

    template <typename T>
    [[nodiscard]] cd::core::Result<FieldValue> read(const T& instance, std::string_view field_name) const
    {
        return read_raw(static_cast<const void*>(&instance), field_name);
    }

    template <typename T>
    [[nodiscard]] cd::core::Result<void>
    write(T& instance, std::string_view field_name, FieldValue value, bool clamp_ranges = true) const
    {
        return write_raw(static_cast<void*>(&instance), field_name, std::move(value), clamp_ranges);
    }
};

struct CommandParamDesc
{
    std::string name {};
    FieldKind   kind { FieldKind::kString };
    bool        required { true };
    std::string description {};
};

struct CommandSchema
{
    std::string                   op_name {};
    std::string                   summary {};
    bool                          reversible { true };
    std::vector<CommandParamDesc> params {};
};

class SchemaRegistry
{
public:
    SchemaRegistry() = default;

    void register_type(TypeSchema schema);
    void register_command(CommandSchema cmd);

    [[nodiscard]] const TypeSchema* find_type(std::string_view type_name) const noexcept;
    [[nodiscard]] const CommandSchema* find_command(std::string_view op_name) const noexcept;

    [[nodiscard]] std::span<const TypeSchema> types() const noexcept { return types_; }
    [[nodiscard]] std::span<const CommandSchema> commands() const noexcept { return commands_; }

    [[nodiscard]] cd::core::Result<void>
    validate_value(std::string_view type_name, std::string_view field_name, const FieldValue& value) const;

    /// Exports Draft 2020-12 JSON Schema covering all registered engine types
    /// and reversible AI/Human CommandBus operations.
    [[nodiscard]] std::string export_json_schema_draft2020(
        std::string_view title = "CHROMODYNAMIC Engine Self-Describing Schema") const;

private:
    std::vector<TypeSchema>    types_ {};
    std::vector<CommandSchema> commands_ {};
};

[[nodiscard]] std::string escape_json_string(std::string_view raw);

}  // namespace cd::schema
