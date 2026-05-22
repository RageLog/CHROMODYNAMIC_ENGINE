// =============================================================================
// CHROMODYNAMIC — cd/asset/SchemaRegistry.hpp
// ADR-006 + ADR-017 P2 (Sprint S3.2, originally deferred from S2.4) — typed
// schema declarations for material / event payloads.
//
// A `Schema` is a flat list of fields (name + type), used to validate decoded
// asset blobs before they are forwarded to subsystems (RHI, material system,
// scene loader).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>

#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cd::asset
{

enum class FieldType : std::uint8_t
{
    kBool,
    kInt32,
    kUint32,
    kInt64,
    kUint64,
    kFloat,
    kDouble,
    kString,
    kVec2f,
    kVec3f,
    kVec4f,
    kMat4f,
    kHandleTexture,
    kHandleBuffer,
    kBytes,
};

struct SchemaField
{
    std::string name;
    FieldType type { FieldType::kInt32 };
    bool optional { false };
};

struct Schema
{
    std::string name;
    std::uint32_t version { 1 };
    std::vector<SchemaField> fields;

    [[nodiscard]] const SchemaField* find_field(std::string_view n) const noexcept
    {
        for (const auto& f : fields)
            if (f.name == n)
                return &f;
        return nullptr;
    }
};

namespace schema_errors
{
inline constexpr std::uint32_t kDomain = 0x000B;

enum class Code : std::uint32_t
{
    kOk = 0,
    kUnknownSchema = 1,
    kVersionMismatch = 2,
    kMissingRequiredField = 3,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}
}  // namespace schema_errors

class SchemaRegistry
{
public:
    void register_schema(Schema s)
    {
        std::unique_lock guard { mutex_ };
        schemas_.insert_or_assign(s.name, std::move(s));
    }

    [[nodiscard]] const Schema* find(std::string_view name) const
    {
        std::shared_lock guard { mutex_ };
        auto it = schemas_.find(std::string { name });
        return it == schemas_.end() ? nullptr : &it->second;
    }

    /// Verify a "payload" represented as a list of present field names against
    /// the named schema. Returns kMissingRequiredField on the first absent
    /// non-optional field.
    [[nodiscard]] cd::core::Result<void>
    validate_payload(std::string_view schema_name, const std::vector<std::string>& present_fields) const
    {
        auto* s = find(schema_name);
        if (s == nullptr)
        {
            return std::unexpected(schema_errors::make(schema_errors::Code::kUnknownSchema, "schema not registered"));
        }
        for (const auto& f : s->fields)
        {
            if (f.optional)
                continue;
            bool found = false;
            for (const auto& p : present_fields)
            {
                if (p == f.name)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                return std::unexpected(schema_errors::make(schema_errors::Code::kMissingRequiredField, f.name));
            }
        }
        return {};
    }

    [[nodiscard]] std::size_t size() const
    {
        std::shared_lock guard { mutex_ };
        return schemas_.size();
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, Schema> schemas_;
};

}  // namespace cd::asset
