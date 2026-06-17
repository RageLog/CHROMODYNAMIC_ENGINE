// =============================================================================
// CHROMODYNAMIC — cd/world_container/ProjectIo.hpp
//
// phase1108 — .cdproject persistence for the World/Project/Level/Layer
// container (gap #18 follow-up). This is the step that turns the
// in-memory foundation into a PRODUCT artefact: an editor or runtime
// can now open a project from disk, enumerate its levels, and follow
// each level's scene_path to the entity payload (.cdscene via
// cd::scene::Serializer — the same file family the hello_editor <->
// hello_engine bridge reads).
//
// Format (schema_version 1):
//   {
//     "schema_version": 1,
//     "name": "My Project",
//     "settings": { "enable_csm": true, ..., "tonemap_op": 2,
//                   "master_volume": 1.0, "default_transport": "udp" },
//     "levels": [ {
//        "name": "Main",
//        "scene_path": "scenes/main.cdscene",
//        "active_layer": 0,
//        "bounds": { "min": [x,y,z], "max": [x,y,z] },
//        "layers": [ { "name": "Default", "visible": true,
//                      "locked": false, "persistent": false,
//                      "color_tag": [r,g,b], "draw_order": 0,
//                      "postfx": { "override_bloom": false, ... } } ]
//     } ]
//   }
//
// Conventions (mirrors cd::editor::cdproj + cd::scene::Serializer):
//   * save_project_file() is ATOMIC: writes a .tmp sibling, then
//     renames over the target — a crash mid-write never leaves a
//     half-written project behind.
//   * deserialize rejects unknown schema_version (a newer file is
//     never silently misread); unknown keys are ignored (forward
//     compat); absent optional keys keep the type's defaults.
//   * Result<> error reporting via serializer-style domain codes.
// =============================================================================
#pragma once

#include <cd/asset/json/Json.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>
#include <cd/world_container/Project.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace cd::world_container
{

namespace project_io_errors
{
inline constexpr std::uint32_t kDomain = 0x0019;

enum class Code : std::uint32_t
{
    kOk = 0,
    kBadShape = 1,    ///< JSON root is not the expected object shape.
    kBadVersion = 2,  ///< schema_version unknown.
    kIoFailure = 3,   ///< file read/write/rename failed.
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}
}  // namespace project_io_errors

inline constexpr std::uint32_t kProjectJsonVersion = 1;

// ---- serialize -------------------------------------------------------------

[[nodiscard]] inline cd::asset::json::Value
vec3_to_json_(const cd::math::Vec3f& v)
{
    cd::asset::json::Array a;
    a.reserve(3);
    a.emplace_back(static_cast<double>(v.x));
    a.emplace_back(static_cast<double>(v.y));
    a.emplace_back(static_cast<double>(v.z));
    return cd::asset::json::Value { std::move(a) };
}

[[nodiscard]] inline cd::asset::json::Value
serialize_project(const Project& p)
{
    using cd::asset::json::Array;
    using cd::asset::json::Object;
    using cd::asset::json::Value;

    Object root;
    root["schema_version"] = Value { static_cast<int>(kProjectJsonVersion) };
    root["name"]           = Value { std::string { p.name() } };

    {
        const auto& s = p.settings();
        Object so;
        so["enable_csm"]        = Value { s.enable_csm };
        so["enable_rt_shadows"] = Value { s.enable_rt_shadows };
        so["enable_bloom"]      = Value { s.enable_bloom };
        so["enable_gtao"]       = Value { s.enable_gtao };
        so["enable_ssr"]        = Value { s.enable_ssr };
        so["tonemap_op"]        = Value { static_cast<int>(s.tonemap_op) };
        so["master_volume"]     = Value { static_cast<double>(s.master_volume) };
        so["default_transport"] = Value { s.default_transport };
        root["settings"] = Value { std::move(so) };
    }

    Array levels;
    for (std::size_t li = 0; li < p.level_count(); ++li)
    {
        const Level* lvl = p.level(li);
        if (lvl == nullptr) continue;
        Object lo;
        lo["name"]         = Value { std::string { lvl->name() } };
        lo["scene_path"]   = Value { std::string { lvl->scene_path() } };
        lo["active_layer"] = Value { static_cast<int>(lvl->active_layer()) };
        {
            Object bo;
            bo["min"] = vec3_to_json_(lvl->bounds().min);
            bo["max"] = vec3_to_json_(lvl->bounds().max);
            lo["bounds"] = Value { std::move(bo) };
        }
        Array layers;
        for (std::size_t i = 0; i < lvl->layer_count(); ++i)
        {
            const Layer* lay = lvl->layer(i);
            if (lay == nullptr) continue;
            Object yo;
            yo["name"]       = Value { std::string { lay->name() } };
            yo["visible"]    = Value { lay->visible() };
            yo["locked"]     = Value { lay->locked() };
            yo["persistent"] = Value { lay->persistent() };
            yo["color_tag"]  = vec3_to_json_(lay->color_tag());
            yo["draw_order"] = Value { static_cast<int>(lay->draw_order()) };
            {
                const auto& fx = lay->postfx();
                Object fo;
                fo["override_bloom"] = Value { fx.override_bloom };
                fo["enable_bloom"]   = Value { fx.enable_bloom };
                fo["override_gtao"]  = Value { fx.override_gtao };
                fo["enable_gtao"]    = Value { fx.enable_gtao };
                fo["override_ssr"]   = Value { fx.override_ssr };
                fo["enable_ssr"]     = Value { fx.enable_ssr };
                yo["postfx"] = Value { std::move(fo) };
            }
            layers.emplace_back(std::move(yo));
        }
        lo["layers"] = Value { std::move(layers) };
        levels.emplace_back(std::move(lo));
    }
    root["levels"] = Value { std::move(levels) };
    return Value { std::move(root) };
}

// ---- deserialize -----------------------------------------------------------

namespace detail
{
inline void read_vec3_(const cd::asset::json::Object& o, const char* key,
                       cd::math::Vec3f& out)
{
    const auto it = o.find(key);
    if (it == o.end() || !it->second.is_array() ||
        it->second.as_array().size() != 3)
        return;
    const auto& a = it->second.as_array();
    if (a[0].is_number() && a[1].is_number() && a[2].is_number())
        out = { static_cast<float>(a[0].as_number()),
                static_cast<float>(a[1].as_number()),
                static_cast<float>(a[2].as_number()) };
}

inline void read_bool_(const cd::asset::json::Object& o, const char* key, bool& out)
{
    if (const auto it = o.find(key); it != o.end() && it->second.is_bool())
        out = it->second.as_bool();
}

inline void read_str_(const cd::asset::json::Object& o, const char* key, std::string& out)
{
    if (const auto it = o.find(key); it != o.end() && it->second.is_string())
        out = it->second.as_string();
}

template <class T>
inline void read_num_(const cd::asset::json::Object& o, const char* key, T& out)
{
    if (const auto it = o.find(key); it != o.end() && it->second.is_number())
        out = static_cast<T>(it->second.as_number());
}
}  // namespace detail

[[nodiscard]] inline cd::core::Result<std::unique_ptr<Project>>
deserialize_project(const cd::asset::json::Value& json)
{
    using project_io_errors::make;
    using project_io_errors::Code;
    if (!json.is_object())
        return std::unexpected(make(Code::kBadShape, "root is not an object"));
    const auto& root = json.as_object();

    {
        const auto it = root.find("schema_version");
        if (it == root.end() || !it->second.is_number() ||
            static_cast<std::uint32_t>(it->second.as_number()) != kProjectJsonVersion)
            return std::unexpected(make(Code::kBadVersion, "unknown schema_version"));
    }

    auto proj = std::make_unique<Project>();
    {
        std::string name { "Untitled Project" };
        detail::read_str_(root, "name", name);
        proj->set_name(std::move(name));
    }
    if (const auto it = root.find("settings");
        it != root.end() && it->second.is_object())
    {
        const auto& so = it->second.as_object();
        auto& s = proj->settings();
        detail::read_bool_(so, "enable_csm", s.enable_csm);
        detail::read_bool_(so, "enable_rt_shadows", s.enable_rt_shadows);
        detail::read_bool_(so, "enable_bloom", s.enable_bloom);
        detail::read_bool_(so, "enable_gtao", s.enable_gtao);
        detail::read_bool_(so, "enable_ssr", s.enable_ssr);
        detail::read_num_(so, "tonemap_op", s.tonemap_op);
        detail::read_num_(so, "master_volume", s.master_volume);
        detail::read_str_(so, "default_transport", s.default_transport);
    }
    if (const auto it = root.find("levels");
        it != root.end() && it->second.is_array())
    {
        for (const auto& lv : it->second.as_array())
        {
            if (!lv.is_object()) continue;
            const auto& lo = lv.as_object();
            std::string lname { "Untitled Level" };
            detail::read_str_(lo, "name", lname);
            Level* lvl = proj->add_level(std::move(lname));
            {
                std::string sp;
                detail::read_str_(lo, "scene_path", sp);
                lvl->set_scene_path(std::move(sp));
            }
            if (const auto bit = lo.find("bounds");
                bit != lo.end() && bit->second.is_object())
            {
                const auto& bo = bit->second.as_object();
                detail::read_vec3_(bo, "min", lvl->bounds().min);
                detail::read_vec3_(bo, "max", lvl->bounds().max);
            }
            if (const auto yit = lo.find("layers");
                yit != lo.end() && yit->second.is_array())
            {
                bool first = true;
                for (const auto& yv : yit->second.as_array())
                {
                    if (!yv.is_object()) continue;
                    const auto& yo = yv.as_object();
                    std::string yname { "Default" };
                    detail::read_str_(yo, "name", yname);
                    // The Level ctor pre-creates a "Default" layer —
                    // the FIRST serialized layer reuses it (renamed)
                    // so a round-trip doesn't grow the list.
                    Layer* lay = nullptr;
                    if (first)
                    {
                        lay = lvl->layer(0);
                        lay->set_name(std::move(yname));
                        first = false;
                    }
                    else
                    {
                        lay = lvl->add_layer(std::move(yname));
                    }
                    bool b = lay->visible();
                    detail::read_bool_(yo, "visible", b);
                    lay->set_visible(b);
                    b = lay->locked();
                    detail::read_bool_(yo, "locked", b);
                    lay->set_locked(b);
                    b = lay->persistent();
                    detail::read_bool_(yo, "persistent", b);
                    lay->set_persistent(b);
                    cd::math::Vec3f ct = lay->color_tag();
                    detail::read_vec3_(yo, "color_tag", ct);
                    lay->set_color_tag(ct);
                    std::int32_t dro = lay->draw_order();
                    detail::read_num_(yo, "draw_order", dro);
                    lay->set_draw_order(dro);
                    if (const auto fit = yo.find("postfx");
                        fit != yo.end() && fit->second.is_object())
                    {
                        const auto& fo = fit->second.as_object();
                        auto& fx = lay->postfx();
                        detail::read_bool_(fo, "override_bloom", fx.override_bloom);
                        detail::read_bool_(fo, "enable_bloom", fx.enable_bloom);
                        detail::read_bool_(fo, "override_gtao", fx.override_gtao);
                        detail::read_bool_(fo, "enable_gtao", fx.enable_gtao);
                        detail::read_bool_(fo, "override_ssr", fx.override_ssr);
                        detail::read_bool_(fo, "enable_ssr", fx.enable_ssr);
                    }
                }
            }
            {
                int al = 0;
                detail::read_num_(lo, "active_layer", al);
                lvl->set_active_layer(static_cast<std::size_t>(al < 0 ? 0 : al));
            }
        }
    }
    return proj;
}

// ---- file I/O ---------------------------------------------------------------

/// Atomic save: write `<path>.tmp`, then rename over `path`.
[[nodiscard]] inline cd::core::Result<void>
save_project_file(const std::filesystem::path& path, const Project& p)
{
    using project_io_errors::make;
    using project_io_errors::Code;
    const auto txt = cd::asset::json::serialize(serialize_project(p), true);
    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream f { tmp, std::ios::binary | std::ios::trunc };
        if (!f)
            return std::unexpected(make(Code::kIoFailure, "cannot open tmp for write"));
        f.write(txt.data(), static_cast<std::streamsize>(txt.size()));
        if (!f.good())
            return std::unexpected(make(Code::kIoFailure, "tmp write failed"));
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec)
    {
        std::filesystem::remove(tmp, ec);
        return std::unexpected(make(Code::kIoFailure, "rename over target failed"));
    }
    return {};
}

[[nodiscard]] inline cd::core::Result<std::unique_ptr<Project>>
load_project_file(const std::filesystem::path& path)
{
    using project_io_errors::make;
    using project_io_errors::Code;
    std::ifstream f { path, std::ios::binary };
    if (!f)
        return std::unexpected(make(Code::kIoFailure, "cannot open project file"));
    std::string txt { std::istreambuf_iterator<char> { f },
                      std::istreambuf_iterator<char> {} };
    auto parsed = cd::asset::json::parse(txt);
    if (!parsed.has_value())
        return std::unexpected(make(Code::kBadShape, "json parse failed"));
    return deserialize_project(*parsed);
}

}  // namespace cd::world_container
