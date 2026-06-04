// =============================================================================
// CHROMODYNAMIC — engine/asset/material_authoring/src/MaterialAuthoring.cpp
// Phase 644 — cd::asset::material_authoring implementation
// =============================================================================

#include <cd/asset/material_authoring/MaterialAuthoring.hpp>
#include <cd/asset/json/Json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <ranges>
#include <span>
#include <sstream>
#include <string>

namespace cd::asset::material_authoring
{

// ---- helpers -----------------------------------------------------------------

namespace
{

/// Map AlphaMode enum -> JSON string token.
[[nodiscard]] const char* alpha_mode_to_string(cd::material::AlphaMode m) noexcept
{
    switch (m)
    {
        case cd::material::AlphaMode::kOpaque: return "OPAQUE";
        case cd::material::AlphaMode::kMask:   return "MASK";
        case cd::material::AlphaMode::kBlend:  return "BLEND";
    }
    return "OPAQUE";
}

/// Map JSON string token -> AlphaMode (case-sensitive per glTF spec).
[[nodiscard]] cd::material::AlphaMode alpha_mode_from_string(const std::string& s) noexcept
{
    if (s == "MASK")  return cd::material::AlphaMode::kMask;
    if (s == "BLEND") return cd::material::AlphaMode::kBlend;
    return cd::material::AlphaMode::kOpaque;  // default / unknown
}

/// Build a cd::asset::json::Object from an AuthoredMaterial.
[[nodiscard]] cd::asset::json::Object to_json_object(const AuthoredMaterial& mat)
{
    using cd::asset::json::Array;
    using cd::asset::json::Object;
    using cd::asset::json::Value;

    Object obj;
    obj["id"]                   = Value { mat.id };
    obj["base_color"]           = Value { Array {
        Value { static_cast<double>(mat.base_color[0]) },
        Value { static_cast<double>(mat.base_color[1]) },
        Value { static_cast<double>(mat.base_color[2]) },
    }};
    obj["metallic"]             = Value { static_cast<double>(mat.metallic) };
    obj["roughness"]            = Value { static_cast<double>(mat.roughness) };
    obj["albedo_texture_path"]  = Value { mat.albedo_texture_path };
    obj["normal_texture_path"]  = Value { mat.normal_texture_path };
    obj["mr_texture_path"]      = Value { mat.mr_texture_path };
    obj["alpha_mode"]           = Value { std::string { alpha_mode_to_string(mat.alpha_mode) } };
    obj["alpha_cutoff"]         = Value { static_cast<double>(mat.alpha_cutoff) };
    return obj;
}

}  // namespace

// ---- save_to_json -----------------------------------------------------------

bool save_to_json(const AuthoredMaterial& mat, const std::filesystem::path& path)
{
    using cd::asset::json::Value;
    const Value root { to_json_object(mat) };
    const std::string text = cd::asset::json::serialize(root, /*pretty=*/true);

    std::ofstream ofs { path, std::ios::binary };
    if (!ofs.is_open())
    {
        return false;
    }
    ofs << text;
    return ofs.good();
}

// ---- load_from_json ---------------------------------------------------------

std::optional<AuthoredMaterial> load_from_json(const std::filesystem::path& path)
{
    // Use the engine's JSON loader (reads file + parses).
    const auto result = cd::asset::json::load(path.string());
    if (!result)
    {
        return std::nullopt;
    }
    const cd::asset::json::Value& root = *result;
    if (!root.is_object())
    {
        return std::nullopt;
    }

    AuthoredMaterial mat;

    // id — required
    {
        const auto id_r = root.at("id");
        if (!id_r || !(*id_r)->is_string())
        {
            return std::nullopt;
        }
        mat.id = (*id_r)->as_string();
        if (mat.id.empty())
        {
            return std::nullopt;
        }
    }

    // base_color — optional (default [1,1,1])
    {
        const auto bc_r = root.at("base_color");
        if (bc_r && (*bc_r)->is_array())
        {
            const auto& arr = (*bc_r)->as_array();
            for (std::size_t i = 0; i < 3 && i < arr.size(); ++i)
            {
                if (arr[i].is_number())
                {
                    mat.base_color[i] = static_cast<float>(arr[i].as_number());
                }
            }
        }
    }

    // metallic — optional (default 0)
    {
        const auto r = root.at("metallic");
        if (r && (*r)->is_number())
        {
            mat.metallic = static_cast<float>((*r)->as_number());
        }
    }

    // roughness — optional (default 0.5)
    {
        const auto r = root.at("roughness");
        if (r && (*r)->is_number())
        {
            mat.roughness = static_cast<float>((*r)->as_number());
        }
    }

    // texture paths — optional (default "")
    auto read_string_field = [&](const char* key, std::string& out)
    {
        const auto r = root.at(key);
        if (r && (*r)->is_string())
        {
            out = (*r)->as_string();
        }
    };
    read_string_field("albedo_texture_path", mat.albedo_texture_path);
    read_string_field("normal_texture_path", mat.normal_texture_path);
    read_string_field("mr_texture_path",     mat.mr_texture_path);

    // alpha_mode — optional (default OPAQUE)
    {
        const auto r = root.at("alpha_mode");
        if (r && (*r)->is_string())
        {
            mat.alpha_mode = alpha_mode_from_string((*r)->as_string());
        }
    }

    // alpha_cutoff — optional (default 0.5)
    {
        const auto r = root.at("alpha_cutoff");
        if (r && (*r)->is_number())
        {
            mat.alpha_cutoff = static_cast<float>((*r)->as_number());
        }
    }

    return mat;
}

// ---- validate_authored ------------------------------------------------------

bool validate_authored(const AuthoredMaterial& mat, std::vector<std::string>& out_issues)
{
    const std::size_t issues_before = out_issues.size();

    // 1. id must be non-empty.
    if (mat.id.empty())
    {
        out_issues.emplace_back("[ERROR] AuthoredMaterial: 'id' is empty. Every material must have a pipeline-unique id.");
    }

    // 2. base_color channels in [0,1].
    for (std::size_t i = 0; i < 3; ++i)
    {
        const float ch = mat.base_color[i];
        if (ch < 0.0F || ch > 1.0F || std::isnan(ch))
        {
            std::ostringstream oss;
            oss << "[ERROR] AuthoredMaterial '" << mat.id
                << "': base_color[" << i << "] = " << ch
                << " is out of [0,1]. Clamp to valid range.";
            out_issues.emplace_back(oss.str());
        }
    }

    // 3. metallic in [0,1].
    if (mat.metallic < 0.0F || mat.metallic > 1.0F || std::isnan(mat.metallic))
    {
        std::ostringstream oss;
        oss << "[ERROR] AuthoredMaterial '" << mat.id
            << "': metallic = " << mat.metallic
            << " is out of [0,1]. Physical range is 0 (dielectric) to 1 (conductor).";
        out_issues.emplace_back(oss.str());
    }

    // 4. roughness in [0,1].
    if (mat.roughness < 0.0F || mat.roughness > 1.0F || std::isnan(mat.roughness))
    {
        std::ostringstream oss;
        oss << "[ERROR] AuthoredMaterial '" << mat.id
            << "': roughness = " << mat.roughness
            << " is out of [0,1]. 0 = mirror, 1 = fully diffuse.";
        out_issues.emplace_back(oss.str());
    }

    // 5. alpha_cutoff in [0,1].
    if (mat.alpha_cutoff < 0.0F || mat.alpha_cutoff > 1.0F || std::isnan(mat.alpha_cutoff))
    {
        std::ostringstream oss;
        oss << "[ERROR] AuthoredMaterial '" << mat.id
            << "': alpha_cutoff = " << mat.alpha_cutoff
            << " is out of [0,1].";
        out_issues.emplace_back(oss.str());
    }

    // 6. MASK: cutoff in (0,1) exclusive — exactly 0 or exactly 1 is a typo.
    if (mat.alpha_mode == cd::material::AlphaMode::kMask)
    {
        if (mat.alpha_cutoff <= 0.0F)
        {
            std::ostringstream oss;
            oss << "[WARNING] AuthoredMaterial '" << mat.id
                << "': alpha_mode=MASK with alpha_cutoff=" << mat.alpha_cutoff
                << " discards ALL fragments. Did you mean a value like 0.5?";
            out_issues.emplace_back(oss.str());
        }
        else if (mat.alpha_cutoff >= 1.0F)
        {
            std::ostringstream oss;
            oss << "[WARNING] AuthoredMaterial '" << mat.id
                << "': alpha_mode=MASK with alpha_cutoff=" << mat.alpha_cutoff
                << " keeps NO fragments (fully transparent). Did you mean a value like 0.5?";
            out_issues.emplace_back(oss.str());
        }
    }

    // 7. BLEND: alpha_cutoff has no effect — inform designer.
    if (mat.alpha_mode == cd::material::AlphaMode::kBlend && mat.alpha_cutoff != 0.5F)
    {
        std::ostringstream oss;
        oss << "[INFO] AuthoredMaterial '" << mat.id
            << "': alpha_mode=BLEND — alpha_cutoff (" << mat.alpha_cutoff
            << ") is ignored by the renderer.";
        out_issues.emplace_back(oss.str());
    }

    // Return true = no new kError-level issues were added by this call.
    // Detect errors by the prefix "[ERROR]" we write above.
    const bool had_new_error = std::ranges::any_of(
        std::span(out_issues).subspan(issues_before),
        [](const std::string& s) { return s.starts_with("[ERROR]"); });

    return !had_new_error;
}

// ---- AuthoringDefaults ------------------------------------------------------

AuthoredMaterial AuthoringDefaults::dielectric()
{
    AuthoredMaterial m;
    m.id        = "preset_dielectric";
    m.base_color = { 0.5F, 0.5F, 0.5F };
    m.metallic   = 0.0F;
    m.roughness  = 0.6F;
    m.alpha_mode = cd::material::AlphaMode::kOpaque;
    m.alpha_cutoff = 0.5F;
    return m;
}

AuthoredMaterial AuthoringDefaults::metal()
{
    AuthoredMaterial m;
    m.id         = "preset_metal";
    m.base_color = { 0.85F, 0.85F, 0.85F };
    m.metallic   = 1.0F;
    m.roughness  = 0.15F;
    m.alpha_mode = cd::material::AlphaMode::kOpaque;
    m.alpha_cutoff = 0.5F;
    return m;
}

AuthoredMaterial AuthoringDefaults::cloth()
{
    AuthoredMaterial m;
    m.id         = "preset_cloth";
    m.base_color = { 0.75F, 0.65F, 0.55F };
    m.metallic   = 0.0F;
    m.roughness  = 0.95F;
    m.alpha_mode = cd::material::AlphaMode::kOpaque;
    m.alpha_cutoff = 0.5F;
    return m;
}

}  // namespace cd::asset::material_authoring
