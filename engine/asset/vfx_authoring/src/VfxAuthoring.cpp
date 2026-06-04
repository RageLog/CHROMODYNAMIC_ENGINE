// =============================================================================
// CHROMODYNAMIC — engine/asset/vfx_authoring/src/VfxAuthoring.cpp
// Phase 694 — cd::asset::vfx_authoring implementation
// =============================================================================

#include <cd/asset/vfx_authoring/VfxAuthoring.hpp>
#include <cd/asset/json/Json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

namespace cd::asset::vfx_authoring
{

// ---- helpers ----------------------------------------------------------------

namespace
{

/// Build a float array Value from a std::array<float,N>.
template <std::size_t N>
[[nodiscard]] cd::asset::json::Value float_array_value(const std::array<float, N>& arr)
{
    using cd::asset::json::Array;
    using cd::asset::json::Value;

    Array a;
    a.reserve(N);
    for (const float v : arr)
    {
        a.push_back(Value { static_cast<double>(v) });
    }
    return Value { std::move(a) };
}

/// Build a cd::asset::json::Object from an AuthoredVfx.
[[nodiscard]] cd::asset::json::Object to_json_object(const AuthoredVfx& vfx)
{
    using cd::asset::json::Object;
    using cd::asset::json::Value;

    Object obj;
    obj["id"]                = Value { vfx.id };
    obj["emitter_kind"]      = Value { vfx.emitter_kind };
    obj["emit_rate_per_sec"] = Value { static_cast<double>(vfx.emit_rate_per_sec) };
    obj["life_seconds"]      = Value { static_cast<double>(vfx.life_seconds) };
    obj["color_start"]       = float_array_value(vfx.color_start);
    obj["color_end"]         = float_array_value(vfx.color_end);
    obj["size_start"]        = Value { static_cast<double>(vfx.size_start) };
    obj["size_end"]          = Value { static_cast<double>(vfx.size_end) };
    obj["velocity_min"]      = float_array_value(vfx.velocity_min);
    obj["velocity_max"]      = float_array_value(vfx.velocity_max);
    obj["texture_path"]      = Value { vfx.texture_path };
    return obj;
}

/// Read a required string field; return false on missing or non-string.
[[nodiscard]] bool read_required_string(const cd::asset::json::Value& root,
                                        const char* key,
                                        std::string& out)
{
    const auto r = root.at(key);
    if (!r || !(*r)->is_string())
    {
        return false;
    }
    out = (*r)->as_string();
    return !out.empty();
}

/// Read an optional float field; leaves `out` unchanged if missing.
void read_optional_float(const cd::asset::json::Value& root,
                         const char* key,
                         float& out)
{
    const auto r = root.at(key);
    if (r && (*r)->is_number())
    {
        out = static_cast<float>((*r)->as_number());
    }
}

/// Read an optional float array; leaves `out` unchanged if missing or wrong size.
template <std::size_t N>
void read_optional_float_array(const cd::asset::json::Value& root,
                                const char* key,
                                std::array<float, N>& out)
{
    const auto r = root.at(key);
    if (!r || !(*r)->is_array())
    {
        return;
    }
    const auto& arr = (*r)->as_array();
    for (std::size_t i = 0; i < N && i < arr.size(); ++i)
    {
        if (arr[i].is_number())
        {
            out[i] = static_cast<float>(arr[i].as_number());
        }
    }
}

}  // namespace

// ---- save_to_json -----------------------------------------------------------

bool save_to_json(const AuthoredVfx& vfx, const std::filesystem::path& path)
{
    using cd::asset::json::Value;
    const Value root { to_json_object(vfx) };
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

std::optional<AuthoredVfx> load_from_json(const std::filesystem::path& path)
{
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

    AuthoredVfx vfx;

    // id — required
    if (!read_required_string(root, "id", vfx.id))
    {
        return std::nullopt;
    }

    // emitter_kind — required
    if (!read_required_string(root, "emitter_kind", vfx.emitter_kind))
    {
        return std::nullopt;
    }

    // optional scalar fields
    read_optional_float(root, "emit_rate_per_sec", vfx.emit_rate_per_sec);
    read_optional_float(root, "life_seconds",      vfx.life_seconds);
    read_optional_float(root, "size_start",        vfx.size_start);
    read_optional_float(root, "size_end",          vfx.size_end);

    // optional array fields
    read_optional_float_array(root, "color_start",  vfx.color_start);
    read_optional_float_array(root, "color_end",    vfx.color_end);
    read_optional_float_array(root, "velocity_min", vfx.velocity_min);
    read_optional_float_array(root, "velocity_max", vfx.velocity_max);

    // optional string
    {
        const auto r = root.at("texture_path");
        if (r && (*r)->is_string())
        {
            vfx.texture_path = (*r)->as_string();
        }
    }

    return vfx;
}

// ---- validate_authored ------------------------------------------------------

bool validate_authored(const AuthoredVfx& vfx, std::vector<std::string>& out_issues)
{
    const std::size_t issues_before = out_issues.size();

    // 1. id must be non-empty.
    if (vfx.id.empty())
    {
        out_issues.emplace_back(
            "[ERROR] AuthoredVfx: 'id' is empty. Every effect must have a pipeline-unique id.");
    }

    // 2. emitter_kind must be non-empty.
    if (vfx.emitter_kind.empty())
    {
        out_issues.emplace_back(
            "[ERROR] AuthoredVfx '" + vfx.id + "': 'emitter_kind' is empty "
            "(expected e.g. \"burst\" or \"continuous\").");
    }

    // 3. emit_rate_per_sec must be > 0.
    if (vfx.emit_rate_per_sec <= 0.0F || std::isnan(vfx.emit_rate_per_sec))
    {
        std::ostringstream oss;
        oss << "[ERROR] AuthoredVfx '" << vfx.id
            << "': emit_rate_per_sec = " << vfx.emit_rate_per_sec
            << " must be > 0.";
        out_issues.emplace_back(oss.str());
    }

    // 4. life_seconds must be > 0.
    if (vfx.life_seconds <= 0.0F || std::isnan(vfx.life_seconds))
    {
        std::ostringstream oss;
        oss << "[ERROR] AuthoredVfx '" << vfx.id
            << "': life_seconds = " << vfx.life_seconds
            << " must be > 0.";
        out_issues.emplace_back(oss.str());
    }

    // 5. color_start / color_end RGBA channels in [0,1].
    auto check_color = [&](const std::array<float, 4>& color, const char* name)
    {
        for (std::size_t i = 0; i < 4; ++i)
        {
            const float ch = color[i];
            if (ch < 0.0F || ch > 1.0F || std::isnan(ch))
            {
                std::ostringstream oss;
                oss << "[ERROR] AuthoredVfx '" << vfx.id
                    << "': " << name << "[" << i << "] = " << ch
                    << " is out of [0,1]. Clamp to valid RGBA range.";
                out_issues.emplace_back(oss.str());
            }
        }
    };
    check_color(vfx.color_start, "color_start");
    check_color(vfx.color_end,   "color_end");

    // 6. size_start and size_end must be >= 0.
    if (vfx.size_start < 0.0F || std::isnan(vfx.size_start))
    {
        std::ostringstream oss;
        oss << "[ERROR] AuthoredVfx '" << vfx.id
            << "': size_start = " << vfx.size_start << " must be >= 0.";
        out_issues.emplace_back(oss.str());
    }
    if (vfx.size_end < 0.0F || std::isnan(vfx.size_end))
    {
        std::ostringstream oss;
        oss << "[ERROR] AuthoredVfx '" << vfx.id
            << "': size_end = " << vfx.size_end << " must be >= 0.";
        out_issues.emplace_back(oss.str());
    }

    // 7. velocity_min[i] <= velocity_max[i] for each axis.
    static constexpr std::array<const char*, 3> kAxisNames { "X", "Y", "Z" };
    for (std::size_t i = 0; i < 3; ++i)
    {
        if (vfx.velocity_min[i] > vfx.velocity_max[i])
        {
            std::ostringstream oss;
            oss << "[ERROR] AuthoredVfx '" << vfx.id
                << "': velocity_min[" << kAxisNames[i] << "] ("
                << vfx.velocity_min[i] << ") > velocity_max["
                << kAxisNames[i] << "] (" << vfx.velocity_max[i] << ").";
            out_issues.emplace_back(oss.str());
        }
    }

    const bool had_new_error = std::any_of(
        out_issues.begin() + static_cast<std::ptrdiff_t>(issues_before),
        out_issues.end(),
        [](const std::string& s) { return s.rfind("[ERROR]", 0) == 0; });

    return !had_new_error;
}

// ---- VfxPresets -------------------------------------------------------------

AuthoredVfx VfxPresets::jump_dust()
{
    AuthoredVfx v;
    v.id                = "jump_dust";
    v.emitter_kind      = "burst";
    v.emit_rate_per_sec = 40.0F;
    v.life_seconds      = 0.45F;
    v.color_start       = { 0.85F, 0.80F, 0.72F, 1.0F };
    v.color_end         = { 0.85F, 0.80F, 0.72F, 0.0F };
    v.size_start        = 0.08F;
    v.size_end          = 0.22F;
    v.velocity_min      = { -1.5F, 0.5F, -1.5F };
    v.velocity_max      = {  1.5F, 2.5F,  1.5F };
    v.texture_path      = "particles/smoke_soft.png";
    return v;
}

AuthoredVfx VfxPresets::muzzle_flash()
{
    AuthoredVfx v;
    v.id                = "muzzle_flash";
    v.emitter_kind      = "burst";
    v.emit_rate_per_sec = 120.0F;
    v.life_seconds      = 0.06F;
    v.color_start       = { 1.0F, 0.90F, 0.60F, 1.0F };
    v.color_end         = { 1.0F, 0.60F, 0.20F, 0.0F };
    v.size_start        = 0.04F;
    v.size_end          = 0.01F;
    v.velocity_min      = { -0.5F, -0.5F,  4.0F };
    v.velocity_max      = {  0.5F,  0.5F, 10.0F };
    v.texture_path      = "particles/spark.png";
    return v;
}

AuthoredVfx VfxPresets::fire_smoke()
{
    AuthoredVfx v;
    v.id                = "fire_smoke";
    v.emitter_kind      = "continuous";
    v.emit_rate_per_sec = 8.0F;
    v.life_seconds      = 3.5F;
    v.color_start       = { 0.22F, 0.20F, 0.18F, 0.8F };
    v.color_end         = { 0.10F, 0.10F, 0.10F, 0.0F };
    v.size_start        = 0.30F;
    v.size_end          = 1.20F;
    v.velocity_min      = { -0.2F, 1.0F, -0.2F };
    v.velocity_max      = {  0.2F, 2.5F,  0.2F };
    v.texture_path      = "particles/smoke_wisp.png";
    return v;
}

AuthoredVfx VfxPresets::water_splash()
{
    AuthoredVfx v;
    v.id                = "water_splash";
    v.emitter_kind      = "burst";
    v.emit_rate_per_sec = 60.0F;
    v.life_seconds      = 0.55F;
    v.color_start       = { 0.78F, 0.90F, 1.0F, 0.9F };
    v.color_end         = { 0.78F, 0.90F, 1.0F, 0.0F };
    v.size_start        = 0.05F;
    v.size_end          = 0.03F;
    v.velocity_min      = { -3.0F, 0.5F, -3.0F };
    v.velocity_max      = {  3.0F, 5.0F,  3.0F };
    v.texture_path      = "particles/droplet.png";
    return v;
}

}  // namespace cd::asset::vfx_authoring
