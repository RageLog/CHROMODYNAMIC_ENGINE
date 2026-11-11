// =============================================================================
// CHROMODYNAMIC — cd/schema/Schema.cpp
// FAZ 33 (ADR-20261111) — Self-Describing C++23 Type, Property & AI Command
// Reflection Schema Layer Implementation.
// =============================================================================
#include <cd/schema/Schema.hpp>

#include <cmath>
#include <cstdio>
#include <sstream>

namespace cd::schema
{

namespace
{

void append_json_kind_schema(std::ostringstream& os, const FieldDesc& f, std::string_view indent)
{
    switch (f.kind)
    {
        case FieldKind::kBool:
            os << indent << "\"type\": \"boolean\"";
            break;
        case FieldKind::kInt32:
            os << indent << "\"type\": \"integer\"";
            if (f.has_range)
            {
                os << ",\n" << indent << "\"minimum\": " << static_cast<std::int32_t>(f.min_value)
                   << ",\n" << indent << "\"maximum\": " << static_cast<std::int32_t>(f.max_value);
            }
            break;
        case FieldKind::kFloat:
            os << indent << "\"type\": \"number\"";
            if (f.has_range)
            {
                os << ",\n" << indent << "\"minimum\": " << f.min_value
                   << ",\n" << indent << "\"maximum\": " << f.max_value;
            }
            break;
        case FieldKind::kVec2f:
            os << indent << "\"type\": \"array\",\n"
               << indent << "\"items\": { \"type\": \"number\" },\n"
               << indent << "\"minItems\": 2,\n"
               << indent << "\"maxItems\": 2";
            break;
        case FieldKind::kVec3f:
            os << indent << "\"type\": \"array\",\n"
               << indent << "\"items\": { \"type\": \"number\" },\n"
               << indent << "\"minItems\": 3,\n"
               << indent << "\"maxItems\": 3";
            break;
        case FieldKind::kColorRgba:
            os << indent << "\"type\": \"array\",\n"
               << indent << "\"items\": { \"type\": \"number\", \"minimum\": 0.0, \"maximum\": 1.0 },\n"
               << indent << "\"minItems\": 4,\n"
               << indent << "\"maxItems\": 4";
            break;
        case FieldKind::kString:
        case FieldKind::kAssetPath:
            os << indent << "\"type\": \"string\"";
            if (f.kind == FieldKind::kAssetPath)
            {
                os << ",\n" << indent << "\"x-cd-semantic\": \"asset_path\"";
            }
            break;
        case FieldKind::kEnum:
            os << indent << "\"type\": \"integer\"";
            if (!f.enum_labels.empty())
            {
                os << ",\n" << indent << "\"minimum\": 0,\n"
                   << indent << "\"maximum\": " << (static_cast<int>(f.enum_labels.size()) - 1) << ",\n"
                   << indent << "\"x-cd-enum-labels\": [";
                for (std::size_t i = 0U; i < f.enum_labels.size(); ++i)
                {
                    if (i > 0U)
                    {
                        os << ", ";
                    }
                    os << "\"" << escape_json_string(f.enum_labels.at(i)) << "\"";
                }
                os << "]";
            }
            break;
    }
}

void append_param_kind_schema(std::ostringstream& os, FieldKind kind)
{
    switch (kind)
    {
        case FieldKind::kBool:
            os << "\"boolean\"";
            break;
        case FieldKind::kInt32:
        case FieldKind::kEnum:
            os << "\"integer\"";
            break;
        case FieldKind::kFloat:
            os << "\"number\"";
            break;
        case FieldKind::kVec2f:
        case FieldKind::kVec3f:
        case FieldKind::kColorRgba:
            os << "\"array\"";
            break;
        case FieldKind::kString:
        case FieldKind::kAssetPath:
            os << "\"string\"";
            break;
    }
}

}  // namespace

std::string escape_json_string(std::string_view raw)
{
    std::string out;
    out.reserve(raw.size() + 4U);
    for (const char ch : raw)
    {
        switch (ch)
        {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20U)
                {
                    char buf[8] {};
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(static_cast<unsigned char>(ch)));
                    out += buf;
                }
                else
                {
                    out.push_back(ch);
                }
                break;
        }
    }
    return out;
}

bool field_value_matches_kind(const FieldValue& value, FieldKind kind) noexcept
{
    switch (kind)
    {
        case FieldKind::kBool:
            return std::holds_alternative<bool>(value);
        case FieldKind::kInt32:
        case FieldKind::kEnum:
            return std::holds_alternative<std::int32_t>(value);
        case FieldKind::kFloat:
            return std::holds_alternative<float>(value);
        case FieldKind::kVec2f:
            return std::holds_alternative<cd::math::Vec2f>(value);
        case FieldKind::kVec3f:
            return std::holds_alternative<cd::math::Vec3f>(value);
        case FieldKind::kColorRgba:
            return std::holds_alternative<cd::math::Vec4f>(value);
        case FieldKind::kString:
        case FieldKind::kAssetPath:
            return std::holds_alternative<std::string>(value);
    }
    return false;
}

const FieldDesc* TypeSchema::find_field(std::string_view field_name) const noexcept
{
    for (const auto& f : fields)
    {
        if (f.name == field_name)
        {
            return &f;
        }
    }
    return nullptr;
}

cd::core::Result<FieldValue> TypeSchema::read_raw(const void* instance, std::string_view field_name) const
{
    if (instance == nullptr)
    {
        return std::unexpected(schema_errors::make(schema_errors::Code::kInvalidArgument, "null instance"));
    }
    const FieldDesc* f = find_field(field_name);
    if (f == nullptr)
    {
        return std::unexpected(schema_errors::make(schema_errors::Code::kUnknownField, field_name));
    }
    if (!f->getter)
    {
        return std::unexpected(schema_errors::make(schema_errors::Code::kInvalidArgument, "missing getter"));
    }
    return f->getter(instance);
}

cd::core::Result<void>
TypeSchema::write_raw(void* instance, std::string_view field_name, FieldValue value, bool clamp_ranges) const
{
    if (instance == nullptr)
    {
        return std::unexpected(schema_errors::make(schema_errors::Code::kInvalidArgument, "null instance"));
    }
    const FieldDesc* f = find_field(field_name);
    if (f == nullptr)
    {
        return std::unexpected(schema_errors::make(schema_errors::Code::kUnknownField, field_name));
    }
    if (!f->setter)
    {
        return std::unexpected(schema_errors::make(schema_errors::Code::kReadOnlyField, field_name));
    }
    if (!field_value_matches_kind(value, f->kind))
    {
        return std::unexpected(schema_errors::make(schema_errors::Code::kTypeMismatch, field_name));
    }

    if (f->kind == FieldKind::kFloat && f->has_range)
    {
        const float v = std::get<float>(value);
        if (!std::isfinite(v))
        {
            return std::unexpected(schema_errors::make(schema_errors::Code::kOutOfRange, field_name));
        }
        if (clamp_ranges)
        {
            value = std::clamp(v, f->min_value, f->max_value);
        }
        else if (v < f->min_value || v > f->max_value)
        {
            return std::unexpected(schema_errors::make(schema_errors::Code::kOutOfRange, field_name));
        }
    }
    else if (f->kind == FieldKind::kInt32 && f->has_range)
    {
        const std::int32_t v   = std::get<std::int32_t>(value);
        const std::int32_t lo  = static_cast<std::int32_t>(f->min_value);
        const std::int32_t hi  = static_cast<std::int32_t>(f->max_value);
        if (clamp_ranges)
        {
            value = std::clamp(v, lo, hi);
        }
        else if (v < lo || v > hi)
        {
            return std::unexpected(schema_errors::make(schema_errors::Code::kOutOfRange, field_name));
        }
    }
    else if (f->kind == FieldKind::kEnum && !f->enum_labels.empty())
    {
        const std::int32_t v  = std::get<std::int32_t>(value);
        const std::int32_t hi = static_cast<std::int32_t>(f->enum_labels.size()) - 1;
        if (v < 0 || v > hi)
        {
            if (clamp_ranges)
            {
                value = std::clamp(v, 0, hi);
            }
            else
            {
                return std::unexpected(schema_errors::make(schema_errors::Code::kOutOfRange, field_name));
            }
        }
    }

    f->setter(instance, value);
    return {};
}

void SchemaRegistry::register_type(TypeSchema schema)
{
    for (auto& existing : types_)
    {
        if (existing.type_name == schema.type_name)
        {
            existing = std::move(schema);
            return;
        }
    }
    types_.push_back(std::move(schema));
}

void SchemaRegistry::register_command(CommandSchema cmd)
{
    for (auto& existing : commands_)
    {
        if (existing.op_name == cmd.op_name)
        {
            existing = std::move(cmd);
            return;
        }
    }
    commands_.push_back(std::move(cmd));
}

const TypeSchema* SchemaRegistry::find_type(std::string_view type_name) const noexcept
{
    for (const auto& t : types_)
    {
        if (t.type_name == type_name)
        {
            return &t;
        }
    }
    return nullptr;
}

const CommandSchema* SchemaRegistry::find_command(std::string_view op_name) const noexcept
{
    for (const auto& c : commands_)
    {
        if (c.op_name == op_name)
        {
            return &c;
        }
    }
    return nullptr;
}

cd::core::Result<void>
SchemaRegistry::validate_value(std::string_view type_name, std::string_view field_name, const FieldValue& value) const
{
    const TypeSchema* t = find_type(type_name);
    if (t == nullptr)
    {
        return std::unexpected(schema_errors::make(schema_errors::Code::kUnknownType, type_name));
    }
    const FieldDesc* f = t->find_field(field_name);
    if (f == nullptr)
    {
        return std::unexpected(schema_errors::make(schema_errors::Code::kUnknownField, field_name));
    }
    if (!field_value_matches_kind(value, f->kind))
    {
        return std::unexpected(schema_errors::make(schema_errors::Code::kTypeMismatch, field_name));
    }
    if (f->kind == FieldKind::kFloat && f->has_range)
    {
        const float v = std::get<float>(value);
        if (!std::isfinite(v) || v < f->min_value || v > f->max_value)
        {
            return std::unexpected(schema_errors::make(schema_errors::Code::kOutOfRange, field_name));
        }
    }
    else if (f->kind == FieldKind::kInt32 && f->has_range)
    {
        const std::int32_t v  = std::get<std::int32_t>(value);
        const std::int32_t lo = static_cast<std::int32_t>(f->min_value);
        const std::int32_t hi = static_cast<std::int32_t>(f->max_value);
        if (v < lo || v > hi)
        {
            return std::unexpected(schema_errors::make(schema_errors::Code::kOutOfRange, field_name));
        }
    }
    else if (f->kind == FieldKind::kEnum && !f->enum_labels.empty())
    {
        const std::int32_t v  = std::get<std::int32_t>(value);
        const std::int32_t hi = static_cast<std::int32_t>(f->enum_labels.size()) - 1;
        if (v < 0 || v > hi)
        {
            return std::unexpected(schema_errors::make(schema_errors::Code::kOutOfRange, field_name));
        }
    }
    return {};
}

std::string SchemaRegistry::export_json_schema_draft2020(std::string_view title) const
{
    std::ostringstream os;
    os << "{\n"
       << "  \"$schema\": \"https://json-schema.org/draft/2020-12/schema\",\n"
       << "  \"title\": \"" << escape_json_string(title) << "\",\n"
       << "  \"x-cd-engine-version\": \"0.99.94\",\n"
       << "  \"$defs\": {\n";

    for (std::size_t ti = 0U; ti < types_.size(); ++ti)
    {
        const auto& t = types_.at(ti);
        os << "    \"" << escape_json_string(t.type_name) << "\": {\n"
           << "      \"type\": \"object\",\n"
           << "      \"x-cd-category\": \"" << escape_json_string(t.category) << "\",\n"
           << "      \"x-cd-version\": " << t.version << ",\n"
           << "      \"description\": \"" << escape_json_string(t.description) << "\",\n"
           << "      \"properties\": {\n";

        for (std::size_t fi = 0U; fi < t.fields.size(); ++fi)
        {
            const auto& f = t.fields.at(fi);
            os << "        \"" << escape_json_string(f.name) << "\": {\n"
               << "          \"title\": \"" << escape_json_string(f.label) << "\",\n"
               << "          \"description\": \"" << escape_json_string(f.tooltip) << "\",\n";
            append_json_kind_schema(os, f, "          ");
            os << "\n        }" << (fi + 1U < t.fields.size() ? "," : "") << "\n";
        }
        os << "      }\n"
           << "    }" << (ti + 1U < types_.size() ? "," : "") << "\n";
    }

    os << "  },\n"
       << "  \"x-cd-commands\": [\n";

    for (std::size_t ci = 0U; ci < commands_.size(); ++ci)
    {
        const auto& c = commands_.at(ci);
        os << "    {\n"
           << "      \"op\": \"" << escape_json_string(c.op_name) << "\",\n"
           << "      \"summary\": \"" << escape_json_string(c.summary) << "\",\n"
           << "      \"reversible\": " << (c.reversible ? "true" : "false") << ",\n"
           << "      \"params\": [\n";
        for (std::size_t pi = 0U; pi < c.params.size(); ++pi)
        {
            const auto& p = c.params.at(pi);
            os << "        { \"name\": \"" << escape_json_string(p.name)
               << "\", \"type\": ";
            append_param_kind_schema(os, p.kind);
            os << ", \"required\": " << (p.required ? "true" : "false")
               << ", \"description\": \"" << escape_json_string(p.description) << "\" }"
               << (pi + 1U < c.params.size() ? "," : "") << "\n";
        }
        os << "      ]\n"
           << "    }" << (ci + 1U < commands_.size() ? "," : "") << "\n";
    }

    os << "  ]\n"
       << "}\n";
    return os.str();
}

}  // namespace cd::schema
