// =============================================================================
// CHROMODYNAMIC — cd/game/cutscene_player/src/CutsceneJson.cpp
//
// Phase 703 — JSON serialization for cd::game::cutscene_player::Cutscene.
//
// Implementation notes:
//   * Uses cd::asset::json (hand-rolled parser) for both read and write —
//     the same library used by cd::asset::material_authoring.
//   * Atomic write: writes to <path>.tmp, then renames. On Windows the OS
//     may refuse to rename over an existing file; we fall back to remove-
//     then-rename, matching the cd::editor::cdproj pattern.
//   * EventKind is encoded as its underlying uint8_t integer so that new
//     event kinds added in future phases are readable by old serializers
//     (they become unknown integers; callers can guard against that with
//     a range-check wrapper if they choose).
//   * schema_version == 1 is the only accepted version on load. A future
//     migration helper would check for version 2+ and up-convert.
// =============================================================================
#include <cd/game/cutscene_player/CutsceneJson.hpp>

#include <cd/asset/json/Json.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

namespace cd::game::cutscene_player
{

namespace
{

// ---------------------------------------------------------------------------
// Serialization helpers
// ---------------------------------------------------------------------------

[[nodiscard]] cd::asset::json::Object event_to_json(const CutsceneEvent& ev)
{
    using cd::asset::json::Array;
    using cd::asset::json::Object;
    using cd::asset::json::Value;

    Object obj;
    obj["offset_ms"]  = Value { static_cast<double>(ev.offset_ms) };
    obj["kind"]       = Value { static_cast<double>(static_cast<std::uint8_t>(ev.kind)) };
    obj["string_arg"] = Value { ev.string_arg };
    obj["vec3_arg"]   = Value { Array {
        Value { static_cast<double>(ev.vec3_arg[0]) },
        Value { static_cast<double>(ev.vec3_arg[1]) },
        Value { static_cast<double>(ev.vec3_arg[2]) },
    }};
    return obj;
}

[[nodiscard]] cd::asset::json::Object phase_to_json(const CutscenePhase& phase)
{
    using cd::asset::json::Array;
    using cd::asset::json::Object;
    using cd::asset::json::Value;

    Object obj;
    obj["phase_id"]    = Value { phase.phase_id };
    obj["duration_ms"] = Value { static_cast<double>(phase.duration_ms) };

    Array events_arr;
    events_arr.reserve(phase.events.size());
    for (const auto& ev : phase.events)
    {
        events_arr.emplace_back(Value { event_to_json(ev) });
    }
    obj["events"] = Value { std::move(events_arr) };
    return obj;
}

[[nodiscard]] cd::asset::json::Object cutscene_to_json(const Cutscene& cs)
{
    using cd::asset::json::Array;
    using cd::asset::json::Object;
    using cd::asset::json::Value;

    Object obj;
    obj["schema_version"] = Value { 1 };
    obj["cutscene_id"]    = Value { cs.cutscene_id };
    obj["can_skip"]       = Value { cs.can_skip };

    Array phases_arr;
    phases_arr.reserve(cs.phases.size());
    for (const auto& ph : cs.phases)
    {
        phases_arr.emplace_back(Value { phase_to_json(ph) });
    }
    obj["phases"] = Value { std::move(phases_arr) };
    return obj;
}

// ---------------------------------------------------------------------------
// Deserialization helpers
// ---------------------------------------------------------------------------

[[nodiscard]] std::optional<CutsceneEvent>
event_from_json(const cd::asset::json::Value& v)
{
    if (!v.is_object()) { return std::nullopt; }

    CutsceneEvent ev;

    {
        const auto r = v.at("offset_ms");
        if (r && (*r)->is_number())
        {
            ev.offset_ms = static_cast<float>((*r)->as_number());
        }
    }

    {
        const auto r = v.at("kind");
        if (r && (*r)->is_number())
        {
            const auto raw = static_cast<std::uint8_t>((*r)->as_number());
            ev.kind = static_cast<EventKind>(raw);
        }
    }

    {
        const auto r = v.at("string_arg");
        if (r && (*r)->is_string())
        {
            ev.string_arg = (*r)->as_string();
        }
    }

    {
        const auto r = v.at("vec3_arg");
        if (r && (*r)->is_array())
        {
            const auto& arr = (*r)->as_array();
            for (std::size_t i = 0; i < 3 && i < arr.size(); ++i)
            {
                if (arr[i].is_number())
                {
                    ev.vec3_arg[i] = static_cast<float>(arr[i].as_number());
                }
            }
        }
    }

    return ev;
}

[[nodiscard]] std::optional<CutscenePhase>
phase_from_json(const cd::asset::json::Value& v)
{
    if (!v.is_object()) { return std::nullopt; }

    CutscenePhase ph;

    {
        const auto r = v.at("phase_id");
        if (r && (*r)->is_string())
        {
            ph.phase_id = (*r)->as_string();
        }
    }

    {
        const auto r = v.at("duration_ms");
        if (r && (*r)->is_number())
        {
            ph.duration_ms = static_cast<float>((*r)->as_number());
        }
    }

    {
        const auto r = v.at("events");
        if (r && (*r)->is_array())
        {
            const auto& arr = (*r)->as_array();
            ph.events.reserve(arr.size());
            for (const auto& ev_val : arr)
            {
                if (auto ev = event_from_json(ev_val))
                {
                    ph.events.push_back(std::move(*ev));
                }
            }
        }
    }

    return ph;
}

// ---------------------------------------------------------------------------
// Atomic write helper (mirrors cd::editor::cdproj)
// ---------------------------------------------------------------------------

[[nodiscard]] bool write_text_atomic(const std::filesystem::path& final_path,
                                     const std::string&           text)
{
    const auto tmp_path = std::filesystem::path(final_path) += ".tmp";
    {
        std::ofstream f(tmp_path, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) { return false; }
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
        f.flush();
        if (!f.good()) { return false; }
    }
    std::error_code ec;
    std::filesystem::rename(tmp_path, final_path, ec);
    if (ec)
    {
        // Windows: fall back to remove-then-rename for best-effort atomicity.
        std::filesystem::remove(final_path, ec);
        std::filesystem::rename(tmp_path, final_path, ec);
        if (ec) { return false; }
    }
    return true;
}

}  // namespace

// ===========================================================================
// Public API
// ===========================================================================

bool save_to_json(const Cutscene& cutscene, const std::filesystem::path& path)
{
    // Ensure parent directory exists.
    std::error_code ec;
    const auto parent = path.parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent, ec))
    {
        std::filesystem::create_directories(parent, ec);
        if (ec) { return false; }
    }

    const cd::asset::json::Value root { cutscene_to_json(cutscene) };
    const std::string text = cd::asset::json::serialize(root, /*pretty=*/true);
    return write_text_atomic(path, text);
}

std::optional<Cutscene> load_from_json(const std::filesystem::path& path)
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) { return std::nullopt; }

    const auto result = cd::asset::json::load(path.string());
    if (!result) { return std::nullopt; }

    const cd::asset::json::Value& root = *result;
    if (!root.is_object()) { return std::nullopt; }

    // schema_version — required, must be 1.
    {
        const auto r = root.at("schema_version");
        if (!r || !(*r)->is_number()) { return std::nullopt; }
        const auto ver = static_cast<int>((*r)->as_number());
        if (ver != 1) { return std::nullopt; }
    }

    // cutscene_id — required.
    Cutscene cs;
    {
        const auto r = root.at("cutscene_id");
        if (!r || !(*r)->is_string()) { return std::nullopt; }
        cs.cutscene_id = (*r)->as_string();
    }

    // can_skip — optional (default true).
    {
        const auto r = root.at("can_skip");
        if (r && (*r)->is_bool())
        {
            cs.can_skip = (*r)->as_bool();
        }
    }

    // phases — optional (empty cutscene is valid).
    {
        const auto r = root.at("phases");
        if (r && (*r)->is_array())
        {
            const auto& arr = (*r)->as_array();
            cs.phases.reserve(arr.size());
            for (const auto& ph_val : arr)
            {
                if (auto ph = phase_from_json(ph_val))
                {
                    cs.phases.push_back(std::move(*ph));
                }
            }
        }
    }

    return cs;
}

}  // namespace cd::game::cutscene_player
