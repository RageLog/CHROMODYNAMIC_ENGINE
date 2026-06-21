// =============================================================================
// CHROMODYNAMIC — engine/asset/vfx_authoring/src/VfxAuthoring.cpp
// Phase 694 — cd::asset::vfx_authoring implementation
// Phase 1268 — FloatCurve, ColorGradient, schema versioning, VfxLibrary,
//              clone_with_id, extended validation ([WARNING] checks)
// =============================================================================

#include <cd/asset/vfx_authoring/VfxAuthoring.hpp>
#include <cd/asset/json/Json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ranges>
#include <sstream>
#include <string>

namespace cd::asset::vfx_authoring
{

// =============================================================================
// FloatCurve
// =============================================================================

// Curve evaluation: piecewise-linear interpolation between sorted keyframes.
// The name "eval" here refers to mathematical curve evaluation (sampling),
// not code execution — no security concern.
float FloatCurve::eval(float t) const noexcept
{
    if (keyframes.empty())
    {
        return 0.0F;
    }

    // Clamp t to [0, 1].
    const float tc = std::clamp(t, 0.0F, 1.0F);

    // Before first keyframe.
    if (tc <= keyframes.front().time)
    {
        return keyframes.front().value;
    }
    // After last keyframe.
    if (tc >= keyframes.back().time)
    {
        return keyframes.back().value;
    }

    // Binary search for the segment [lo, hi] containing tc.
    // Compare by time field.
    const auto it = std::ranges::lower_bound(keyframes, tc,
                                             std::ranges::less {},
                                             &Keyframe::time);

    // it->time >= tc; the previous keyframe is at (it - 1).
    const Keyframe& hi = *it;
    const Keyframe& lo = *(it - 1);

    const float span = hi.time - lo.time;
    if (span <= 0.0F)
    {
        return lo.value;
    }
    const float alpha = (tc - lo.time) / span;
    return lo.value + alpha * (hi.value - lo.value);
}

void FloatCurve::add_keyframe(float time, float value)
{
    Keyframe kf { time, value };
    // Insert sorted by time.
    const auto pos = std::ranges::lower_bound(keyframes, time,
                                              std::ranges::less {},
                                              &Keyframe::time);
    keyframes.insert(pos, kf);
}

// =============================================================================
// ColorGradient
// =============================================================================

// Gradient evaluation: piecewise-linear per-channel interpolation.
// "eval" here means sampling the colour at a normalised time — not code eval.
std::array<float, 4> ColorGradient::eval(float t) const noexcept
{
    if (keyframes.empty())
    {
        return { 0.0F, 0.0F, 0.0F, 0.0F };
    }

    const float tc = std::clamp(t, 0.0F, 1.0F);

    if (tc <= keyframes.front().time)
    {
        return keyframes.front().rgba;
    }
    if (tc >= keyframes.back().time)
    {
        return keyframes.back().rgba;
    }

    const auto it = std::ranges::lower_bound(keyframes, tc,
                                             std::ranges::less {},
                                             &Keyframe::time);

    const Keyframe& hi = *it;
    const Keyframe& lo = *(it - 1);

    const float span = hi.time - lo.time;
    if (span <= 0.0F)
    {
        return lo.rgba;
    }
    const float alpha = (tc - lo.time) / span;

    std::array<float, 4> result {};
    for (std::size_t i = 0; i < 4; ++i)
    {
        result[i] = lo.rgba[i] + alpha * (hi.rgba[i] - lo.rgba[i]);
    }
    return result;
}

void ColorGradient::add_keyframe(float time, std::array<float, 4> rgba)
{
    Keyframe kf { time, rgba };
    const auto pos = std::ranges::lower_bound(keyframes, time,
                                              std::ranges::less {},
                                              &Keyframe::time);
    keyframes.insert(pos, kf);
}

// =============================================================================
// Internal helpers
// =============================================================================

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
        a.emplace_back(static_cast<double>(v));
    }
    return Value { std::move(a) };
}

/// Serialise a FloatCurve as a JSON array of [time, value] pairs.
[[nodiscard]] cd::asset::json::Value float_curve_value(const FloatCurve& curve)
{
    using cd::asset::json::Array;
    using cd::asset::json::Value;

    Array outer;
    outer.reserve(curve.keyframes.size());
    for (const auto& kf : curve.keyframes)
    {
        Array pair;
        pair.emplace_back(static_cast<double>(kf.time));
        pair.emplace_back(static_cast<double>(kf.value));
        outer.emplace_back(std::move(pair));
    }
    return Value { std::move(outer) };
}

/// Serialise a ColorGradient as a JSON array of [time, r, g, b, a] arrays.
[[nodiscard]] cd::asset::json::Value color_gradient_value(const ColorGradient& grad)
{
    using cd::asset::json::Array;
    using cd::asset::json::Value;

    Array outer;
    outer.reserve(grad.keyframes.size());
    for (const auto& kf : grad.keyframes)
    {
        Array entry;
        entry.emplace_back(static_cast<double>(kf.time));
        for (const float ch : kf.rgba)
        {
            entry.emplace_back(static_cast<double>(ch));
        }
        outer.emplace_back(std::move(entry));
    }
    return Value { std::move(outer) };
}

/// Build a cd::asset::json::Object from an AuthoredVfx.
[[nodiscard]] cd::asset::json::Object to_json_object(const AuthoredVfx& vfx)
{
    using cd::asset::json::Object;
    using cd::asset::json::Value;

    Object obj;
    obj["schema_version"]    = Value { static_cast<double>(vfx.schema_version) };
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

    if (!vfx.size_curve.empty())
    {
        obj["size_curve"] = float_curve_value(vfx.size_curve);
    }
    if (!vfx.color_gradient.empty())
    {
        obj["color_gradient"] = color_gradient_value(vfx.color_gradient);
    }

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

/// Read an optional uint32 field; leaves `out` unchanged if missing.
void read_optional_uint32(const cd::asset::json::Value& root,
                          const char* key,
                          std::uint32_t& out)
{
    const auto r = root.at(key);
    if (r && (*r)->is_number())
    {
        const double d = (*r)->as_number();
        if (d >= 0.0 && d <= static_cast<double>(UINT32_MAX))
        {
            out = static_cast<std::uint32_t>(d);
        }
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

/// Deserialise a FloatCurve from a JSON array of [time, value] pairs.
void read_optional_float_curve(const cd::asset::json::Value& root,
                                const char* key,
                                FloatCurve& out)
{
    const auto r = root.at(key);
    if (!r || !(*r)->is_array())
    {
        return;
    }
    const auto& outer = (*r)->as_array();
    for (const auto& entry : outer)
    {
        if (!entry.is_array())
        {
            continue;
        }
        const auto& pair = entry.as_array();
        if (pair.size() < 2 || !pair[0].is_number() || !pair[1].is_number())
        {
            continue;
        }
        out.add_keyframe(static_cast<float>(pair[0].as_number()),
                         static_cast<float>(pair[1].as_number()));
    }
}

/// Deserialise a ColorGradient from a JSON array of [time, r, g, b, a] arrays.
void read_optional_color_gradient(const cd::asset::json::Value& root,
                                   const char* key,
                                   ColorGradient& out)
{
    const auto r = root.at(key);
    if (!r || !(*r)->is_array())
    {
        return;
    }
    const auto& outer = (*r)->as_array();
    for (const auto& entry : outer)
    {
        if (!entry.is_array())
        {
            continue;
        }
        const auto& arr = entry.as_array();
        // Expect at least [time, r, g, b, a] = 5 elements.
        if (arr.size() < 5 || !arr[0].is_number())
        {
            continue;
        }
        const auto time = static_cast<float>(arr[0].as_number());
        std::array<float, 4> rgba { 1.0F, 1.0F, 1.0F, 1.0F };
        for (std::size_t i = 0; i < 4 && (i + 1) < arr.size(); ++i)
        {
            if (arr[i + 1].is_number())
            {
                rgba[i] = static_cast<float>(arr[i + 1].as_number());
            }
        }
        out.add_keyframe(time, rgba);
    }
}

/// Populate an AuthoredVfx from a JSON Object value (used by both single-
/// effect load and library load).  Returns false when required fields are
/// missing or schema_version > kSchemaVersion.
[[nodiscard]] bool parse_authored_vfx(const cd::asset::json::Value& root,
                                       AuthoredVfx& vfx)
{
    if (!root.is_object())
    {
        return false;
    }

    // schema_version — optional; absent in v0.  Reject future versions.
    std::uint32_t sv = 0U;
    read_optional_uint32(root, "schema_version", sv);
    if (sv > kSchemaVersion)
    {
        return false;
    }
    vfx.schema_version = sv;

    // id — required.
    if (!read_required_string(root, "id", vfx.id))
    {
        return false;
    }

    // emitter_kind — required.
    if (!read_required_string(root, "emitter_kind", vfx.emitter_kind))
    {
        return false;
    }

    // optional scalar fields.
    read_optional_float(root, "emit_rate_per_sec", vfx.emit_rate_per_sec);
    read_optional_float(root, "life_seconds",      vfx.life_seconds);
    read_optional_float(root, "size_start",        vfx.size_start);
    read_optional_float(root, "size_end",          vfx.size_end);

    // optional array fields.
    read_optional_float_array(root, "color_start",  vfx.color_start);
    read_optional_float_array(root, "color_end",    vfx.color_end);
    read_optional_float_array(root, "velocity_min", vfx.velocity_min);
    read_optional_float_array(root, "velocity_max", vfx.velocity_max);

    // optional string.
    {
        const auto r = root.at("texture_path");
        if (r && (*r)->is_string())
        {
            vfx.texture_path = (*r)->as_string();
        }
    }

    // optional curve / gradient.
    read_optional_float_curve(root,      "size_curve",      vfx.size_curve);
    read_optional_color_gradient(root,   "color_gradient",  vfx.color_gradient);

    return true;
}

}  // namespace

// =============================================================================
// save_to_json
// =============================================================================

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

// =============================================================================
// load_from_json
// =============================================================================

std::optional<AuthoredVfx> load_from_json(const std::filesystem::path& path)
{
    const auto result = cd::asset::json::load(path.string());
    if (!result)
    {
        return std::nullopt;
    }

    AuthoredVfx vfx;
    if (!parse_authored_vfx(*result, vfx))
    {
        return std::nullopt;
    }
    return vfx;
}

// =============================================================================
// clone_with_id
// =============================================================================

AuthoredVfx clone_with_id(const AuthoredVfx& src, const std::string& new_id)
{
    AuthoredVfx copy = src;
    copy.id = new_id;
    return copy;
}

// =============================================================================
// validate_authored
// =============================================================================

bool validate_authored(const AuthoredVfx& vfx, std::vector<std::string>& out_issues)
{
    const std::size_t issues_before = out_issues.size();

    // ------------------------------------------------------------------
    // [ERROR] checks
    // ------------------------------------------------------------------

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

    // 8. FloatCurve keyframe times in [0,1] and values finite.
    for (std::size_t i = 0; i < vfx.size_curve.keyframes.size(); ++i)
    {
        const auto& kf = vfx.size_curve.keyframes[i];
        if (kf.time < 0.0F || kf.time > 1.0F || std::isnan(kf.time))
        {
            std::ostringstream oss;
            oss << "[ERROR] AuthoredVfx '" << vfx.id
                << "': size_curve keyframe[" << i << "].time = " << kf.time
                << " is outside [0,1].";
            out_issues.emplace_back(oss.str());
        }
        if (std::isnan(kf.value) || std::isinf(kf.value))
        {
            std::ostringstream oss;
            oss << "[ERROR] AuthoredVfx '" << vfx.id
                << "': size_curve keyframe[" << i << "].value is not finite.";
            out_issues.emplace_back(oss.str());
        }
    }

    // 9. ColorGradient keyframe times in [0,1] and RGBA channels in [0,1].
    for (std::size_t i = 0; i < vfx.color_gradient.keyframes.size(); ++i)
    {
        const auto& kf = vfx.color_gradient.keyframes[i];
        if (kf.time < 0.0F || kf.time > 1.0F || std::isnan(kf.time))
        {
            std::ostringstream oss;
            oss << "[ERROR] AuthoredVfx '" << vfx.id
                << "': color_gradient keyframe[" << i << "].time = " << kf.time
                << " is outside [0,1].";
            out_issues.emplace_back(oss.str());
        }
        for (std::size_t ch = 0; ch < 4; ++ch)
        {
            const float v = kf.rgba[ch];
            if (v < 0.0F || v > 1.0F || std::isnan(v))
            {
                std::ostringstream oss;
                oss << "[ERROR] AuthoredVfx '" << vfx.id
                    << "': color_gradient keyframe[" << i << "].rgba[" << ch
                    << "] = " << v << " is outside [0,1].";
                out_issues.emplace_back(oss.str());
            }
        }
    }

    // ------------------------------------------------------------------
    // Determine whether any new [ERROR] was appended before warnings.
    // ------------------------------------------------------------------
    const bool had_new_error = std::ranges::any_of(
        out_issues.begin() + static_cast<std::ptrdiff_t>(issues_before),
        out_issues.end(),
        [](const std::string& s) { return s.starts_with("[ERROR]"); });

    // ------------------------------------------------------------------
    // [WARNING] checks — non-fatal, do not affect return value.
    // ------------------------------------------------------------------

    // W1. emit_rate_per_sec > 10000.
    if (!std::isnan(vfx.emit_rate_per_sec) && vfx.emit_rate_per_sec > 10000.0F)
    {
        std::ostringstream oss;
        oss << "[WARNING] AuthoredVfx '" << vfx.id
            << "': emit_rate_per_sec = " << vfx.emit_rate_per_sec
            << " is very high (> 10000). This may cause performance issues.";
        out_issues.emplace_back(oss.str());
    }

    // W2. life_seconds > 60.
    if (!std::isnan(vfx.life_seconds) && vfx.life_seconds > 60.0F)
    {
        std::ostringstream oss;
        oss << "[WARNING] AuthoredVfx '" << vfx.id
            << "': life_seconds = " << vfx.life_seconds
            << " is unusually long (> 60 s). Consider capping for performance.";
        out_issues.emplace_back(oss.str());
    }

    // W3. emitter_kind not in known set.
    if (!vfx.emitter_kind.empty())
    {
        static constexpr std::array<std::string_view, 3> kKnownKinds {
            "burst", "continuous", "one_shot"
        };
        const bool known = std::ranges::any_of(
            kKnownKinds,
            [&](std::string_view k) { return k == vfx.emitter_kind; });
        if (!known)
        {
            out_issues.emplace_back(
                "[WARNING] AuthoredVfx '" + vfx.id
                + "': emitter_kind = \"" + vfx.emitter_kind
                + "\" is not in the known set "
                  "(\"burst\", \"continuous\", \"one_shot\"). "
                  "This may be a typo or a future extension.");
        }
    }

    // W4. schema_version > kSchemaVersion.
    if (vfx.schema_version > kSchemaVersion)
    {
        std::ostringstream oss;
        oss << "[WARNING] AuthoredVfx '" << vfx.id
            << "': schema_version = " << vfx.schema_version
            << " exceeds current kSchemaVersion (" << kSchemaVersion
            << "). File was authored by a newer tool.";
        out_issues.emplace_back(oss.str());
    }

    return !had_new_error;
}

// =============================================================================
// VfxPresets
// =============================================================================

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

// =============================================================================
// VfxLibrary
// =============================================================================

bool VfxLibrary::add(AuthoredVfx effect)
{
    if (effect.id.empty())
    {
        return false;
    }
    // Overwrite existing entry if same id.
    const auto it = std::ranges::find_if(
        effects_,
        [&](const AuthoredVfx& e) { return e.id == effect.id; });
    if (it != effects_.end())
    {
        *it = std::move(effect);
    }
    else
    {
        effects_.emplace_back(std::move(effect));
    }
    return true;
}

bool VfxLibrary::remove(const std::string& id)
{
    const auto it = std::ranges::find_if(
        effects_,
        [&](const AuthoredVfx& e) { return e.id == id; });
    if (it == effects_.end())
    {
        return false;
    }
    effects_.erase(it);
    return true;
}

const AuthoredVfx* VfxLibrary::find(const std::string& id) const noexcept
{
    const auto it = std::ranges::find_if(
        effects_,
        [&](const AuthoredVfx& e) { return e.id == id; });
    return (it != effects_.end()) ? &(*it) : nullptr;
}

std::size_t VfxLibrary::size() const noexcept
{
    return effects_.size();
}

bool VfxLibrary::empty() const noexcept
{
    return effects_.empty();
}

const std::vector<AuthoredVfx>& VfxLibrary::effects() const noexcept
{
    return effects_;
}

bool VfxLibrary::save(const std::filesystem::path& path) const
{
    using cd::asset::json::Array;
    using cd::asset::json::Object;
    using cd::asset::json::Value;

    Array arr;
    arr.reserve(effects_.size());
    for (const auto& e : effects_)
    {
        arr.emplace_back(to_json_object(e));
    }

    Object root;
    root["schema_version"] = Value { static_cast<double>(kSchemaVersion) };
    root["effects"]        = Value { std::move(arr) };

    const std::string text = cd::asset::json::serialize(Value { std::move(root) }, /*pretty=*/true);

    std::ofstream ofs { path, std::ios::binary };
    if (!ofs.is_open())
    {
        return false;
    }
    ofs << text;
    return ofs.good();
}

bool VfxLibrary::load(const std::filesystem::path& path,
                      std::vector<std::string>&    out_issues)
{
    const auto parse_result = cd::asset::json::load(path.string());
    if (!parse_result)
    {
        out_issues.emplace_back(
            "[ERROR] VfxLibrary: failed to open or parse file: " + path.string());
        return false;
    }

    const cd::asset::json::Value& root = *parse_result;
    if (!root.is_object())
    {
        out_issues.emplace_back(
            "[ERROR] VfxLibrary: root JSON value is not an object in: " + path.string());
        return false;
    }

    // Library-level schema_version warning.
    std::uint32_t lib_sv = 0U;
    read_optional_uint32(root, "schema_version", lib_sv);
    if (lib_sv > kSchemaVersion)
    {
        std::ostringstream oss;
        oss << "[WARNING] VfxLibrary: file schema_version " << lib_sv
            << " exceeds kSchemaVersion (" << kSchemaVersion
            << "). Some effects may be skipped.";
        out_issues.emplace_back(oss.str());
    }

    const auto eff_r = root.at("effects");
    if (!eff_r || !(*eff_r)->is_array())
    {
        out_issues.emplace_back(
            "[ERROR] VfxLibrary: missing or non-array 'effects' key in: " + path.string());
        return false;
    }

    effects_.clear();
    const auto& arr = (*eff_r)->as_array();
    for (std::size_t i = 0; i < arr.size(); ++i)
    {
        AuthoredVfx vfx;
        if (!parse_authored_vfx(arr[i], vfx))
        {
            std::ostringstream oss;
            oss << "[WARNING] VfxLibrary: effects[" << i
                << "] could not be parsed (missing required fields or future schema). Skipping.";
            out_issues.emplace_back(oss.str());
            continue;
        }
        // Duplicate id in file — later entry wins (consistent with single-file "last wins").
        add(std::move(vfx));
    }

    return true;
}

}  // namespace cd::asset::vfx_authoring
