// =============================================================================
// CHROMODYNAMIC — cd/world2d/CommandBus2D.cpp
// FAZ 35 (ADR-20261111) — Symmetric Human Undo/Redo + AI JSON Command Protocol.
// =============================================================================
#include <cd/world2d/CommandBus2D.hpp>

#include <cd/asset/json/Json.hpp>

#include <algorithm>
#include <sstream>

namespace cd::world2d
{

namespace
{

[[nodiscard]] cd::core::Result<cd::schema::FieldValue>
json_value_to_field_value(const cd::asset::json::Value& jv, cd::schema::FieldKind kind)
{
    switch (kind)
    {
        case cd::schema::FieldKind::kBool:
            if (jv.is_bool()) return jv.as_bool();
            break;
        case cd::schema::FieldKind::kInt32:
        case cd::schema::FieldKind::kEnum:
            if (jv.is_number()) return static_cast<std::int32_t>(jv.as_number());
            break;
        case cd::schema::FieldKind::kFloat:
            if (jv.is_number()) return static_cast<float>(jv.as_number());
            break;
        case cd::schema::FieldKind::kVec2f:
            if (jv.is_array() && jv.as_array().size() >= 2U)
            {
                return cd::math::Vec2f {
                    static_cast<float>(jv.as_array().at(0).as_number()),
                    static_cast<float>(jv.as_array().at(1).as_number()),
                };
            }
            break;
        case cd::schema::FieldKind::kVec3f:
            if (jv.is_array() && jv.as_array().size() >= 3U)
            {
                return cd::math::Vec3f {
                    static_cast<float>(jv.as_array().at(0).as_number()),
                    static_cast<float>(jv.as_array().at(1).as_number()),
                    static_cast<float>(jv.as_array().at(2).as_number()),
                };
            }
            break;
        case cd::schema::FieldKind::kColorRgba:
            if (jv.is_array() && jv.as_array().size() >= 4U)
            {
                return cd::math::Vec4f {
                    static_cast<float>(jv.as_array().at(0).as_number()),
                    static_cast<float>(jv.as_array().at(1).as_number()),
                    static_cast<float>(jv.as_array().at(2).as_number()),
                    static_cast<float>(jv.as_array().at(3).as_number()),
                };
            }
            break;
        case cd::schema::FieldKind::kString:
        case cd::schema::FieldKind::kAssetPath:
            if (jv.is_string()) return jv.as_string();
            break;
    }
    return std::unexpected(cd::schema::schema_errors::make(
        cd::schema::schema_errors::Code::kTypeMismatch, "JSON value does not match schema FieldKind"));
}

}  // namespace

std::string CommandExecutionReport2D::to_json_string() const
{
    std::ostringstream os;
    os << "{\n"
       << "  \"ok\": " << (ok ? "true" : "false") << ",\n"
       << "  \"applied_count\": " << applied_count << ",\n"
       << "  \"last_created_entity_id\": " << last_created_entity_id << ",\n"
       << "  \"summary\": \"" << cd::schema::escape_json_string(summary) << "\",\n"
       << "  \"error\": \"" << cd::schema::escape_json_string(error_message) << "\"\n"
       << "}\n";
    return os.str();
}

void CommandBus2D::push_record(std::string description, std::string before_json, std::string after_json)
{
    if (before_json == after_json)
    {
        return;
    }
    constexpr std::size_t kMaxHistory = 128U;
    if (undo_stack_.size() >= kMaxHistory)
    {
        undo_stack_.erase(undo_stack_.begin());
    }
    undo_stack_.push_back(SnapshotRecord {
        .description = std::move(description),
        .before_json = std::move(before_json),
        .after_json  = std::move(after_json),
    });
    redo_stack_.clear();
}

std::uint32_t CommandBus2D::spawn_entity(Stage2D& stage, Entity2D entity)
{
    const std::string before = stage.save_to_json_string(false);
    const std::string desc   = "Spawn " + entity.name;
    const auto        id     = stage.add_entity(std::move(entity));
    push_record(desc, before, stage.save_to_json_string(false));
    return id;
}

bool CommandBus2D::delete_entity(Stage2D& stage, std::uint32_t entity_id)
{
    const Entity2D* existing = stage.find_entity(entity_id);
    if (existing == nullptr)
    {
        return false;
    }
    const std::string before = stage.save_to_json_string(false);
    const std::string desc   = "Delete " + existing->name;
    const bool        ok     = stage.remove_entity(entity_id);
    if (ok)
    {
        push_record(desc, before, stage.save_to_json_string(false));
    }
    return ok;
}

bool CommandBus2D::move_entity(Stage2D& stage, std::uint32_t entity_id, cd::math::Vec2f new_pos)
{
    Entity2D* ent = stage.find_entity(entity_id);
    if (ent == nullptr)
    {
        return false;
    }
    const std::string before = stage.save_to_json_string(false);
    ent->position            = new_pos;
    ent->home_position       = new_pos;
    push_record("Move " + ent->name, before, stage.save_to_json_string(false));
    return true;
}

cd::core::Result<void> CommandBus2D::set_entity_field(
    Stage2D&               stage,
    std::uint32_t          entity_id,
    std::string_view       field_name,
    cd::schema::FieldValue value)
{
    Entity2D* ent = stage.find_entity(entity_id);
    if (ent == nullptr)
    {
        return std::unexpected(cd::schema::schema_errors::make(
            cd::schema::schema_errors::Code::kInvalidArgument, "entity_id not found"));
    }

    const std::string            before = stage.save_to_json_string(false);
    const cd::schema::TypeSchema schema = make_entity2d_type_schema();
    auto                         res    = schema.write(*ent, field_name, std::move(value), true);
    if (!res.has_value())
    {
        return res;
    }
    ent->home_position = ent->position;
    push_record("Edit " + ent->name + "." + std::string { field_name },
                before,
                stage.save_to_json_string(false));
    return {};
}

void CommandBus2D::paint_tile(Stage2D& stage, int gx, int gy, std::uint16_t palette_id)
{
    const std::string before = stage.save_to_json_string(false);
    stage.tilemap().set_tile(gx, gy, palette_id, true);
    stage.rebuild_baked_colliders();
    push_record(palette_id == 0U ? "Erase Tile" : "Paint Tile", before, stage.save_to_json_string(false));
}

void CommandBus2D::paint_rect(Stage2D& stage, int x0, int y0, int x1, int y1, std::uint16_t palette_id)
{
    const std::string before = stage.save_to_json_string(false);
    stage.tilemap().paint_rect(x0, y0, x1, y1, palette_id, true);
    stage.rebuild_baked_colliders();
    push_record(palette_id == 0U ? "Erase Tile Rect" : "Paint Auto-Tile Rect",
                before,
                stage.save_to_json_string(false));
}

bool CommandBus2D::attach_lua_script(Stage2D& stage, std::uint32_t entity_id, std::string lua_code)
{
    Entity2D* ent = stage.find_entity(entity_id);
    if (ent == nullptr)
    {
        return false;
    }
    const std::string before = stage.save_to_json_string(false);
    ent->lua_script          = std::move(lua_code);
    const bool compiled      = stage.compile_entity_lua_script(entity_id);
    push_record("Attach Lua Script -> " + ent->name, before, stage.save_to_json_string(false));
    return compiled;
}

void CommandBus2D::load_starter_platformer(Stage2D& stage)
{
    const std::string before = stage.save_to_json_string(false);
    stage.build_starter_illuminated_platformer();
    push_record("Load Starter Showcase 2D", before, stage.save_to_json_string(false));
}

void CommandBus2D::begin_stroke(const Stage2D& stage, std::string description)
{
    if (stroke_open_)
    {
        return;
    }
    stroke_open_        = true;
    stroke_desc_        = std::move(description);
    stroke_before_json_ = stage.save_to_json_string(false);
}

void CommandBus2D::end_stroke(Stage2D& stage)
{
    if (!stroke_open_)
    {
        return;
    }
    stroke_open_ = false;
    stage.rebuild_baked_colliders();
    push_record(std::move(stroke_desc_), std::move(stroke_before_json_), stage.save_to_json_string(false));
    stroke_desc_.clear();
    stroke_before_json_.clear();
}

CommandExecutionReport2D CommandBus2D::execute_json(Stage2D& stage, std::string_view json_text)
{
    namespace j = cd::asset::json;
    CommandExecutionReport2D rep {};

    if (json_text.size() >= 3U &&
        static_cast<unsigned char>(json_text[0]) == 0xEFU &&
        static_cast<unsigned char>(json_text[1]) == 0xBBU &&
        static_cast<unsigned char>(json_text[2]) == 0xBFU)
    {
        json_text.remove_prefix(3U);
    }

    auto parsed = j::parse(json_text);
    if (!parsed.has_value())
    {
        rep.ok            = false;
        rep.error_message = std::string { parsed.error().message };
        return rep;
    }

    std::vector<const j::Value*> ops;
    const j::Value&              root = *parsed;
    if (root.is_array())
    {
        for (const auto& item : root.as_array()) ops.push_back(&item);
    }
    else if (root.is_object())
    {
        if (auto cmds = root.at("commands"); cmds.has_value() && (*cmds)->is_array())
        {
            for (const auto& item : (*cmds)->as_array()) ops.push_back(&item);
        }
        else
        {
            ops.push_back(&root);
        }
    }
    else
    {
        rep.ok            = false;
        rep.error_message = "Command JSON must be an object or array";
        return rep;
    }

    const std::string      batch_before = stage.save_to_json_string(false);
    const cd::schema::TypeSchema entity_schema = make_entity2d_type_schema();

    for (const j::Value* op_val : ops)
    {
        if (!op_val->is_object())
        {
            rep.ok            = false;
            rep.error_message = "Each command entry must be a JSON object";
            (void)stage.load_from_json_string(batch_before);
            return rep;
        }
        auto op_s = op_val->at("op");
        if (!op_s.has_value() || !(*op_s)->is_string())
        {
            rep.ok            = false;
            rep.error_message = "Missing string field 'op'";
            (void)stage.load_from_json_string(batch_before);
            return rep;
        }
        const std::string& op = (*op_s)->as_string();

        if (op == "spawn_entity_2d")
        {
            Entity2D ent {};
            if (auto v = op_val->at("name"); v.has_value() && (*v)->is_string())
                ent.name = (*v)->as_string();
            if (auto v = op_val->at("role"); v.has_value() && (*v)->is_number())
                ent.role = static_cast<EntityRole2D>(std::clamp(static_cast<int>((*v)->as_number()), 0, 9));
            if (auto v = op_val->at("pos"); v.has_value() && (*v)->is_array() && (*v)->as_array().size() >= 2U)
            {
                ent.position.x = static_cast<float>((*v)->as_array().at(0).as_number());
                ent.position.y = static_cast<float>((*v)->as_array().at(1).as_number());
            }
            if (auto v = op_val->at("size"); v.has_value() && (*v)->is_array() && (*v)->as_array().size() >= 2U)
            {
                ent.size.x = static_cast<float>((*v)->as_array().at(0).as_number());
                ent.size.y = static_cast<float>((*v)->as_array().at(1).as_number());
            }
            if (auto v = op_val->at("color"); v.has_value() && (*v)->is_array() && (*v)->as_array().size() >= 4U)
            {
                ent.color_rgba.x = static_cast<float>((*v)->as_array().at(0).as_number());
                ent.color_rgba.y = static_cast<float>((*v)->as_array().at(1).as_number());
                ent.color_rgba.z = static_cast<float>((*v)->as_array().at(2).as_number());
                ent.color_rgba.w = static_cast<float>((*v)->as_array().at(3).as_number());
            }
            if (auto v = op_val->at("lua_script"); v.has_value() && (*v)->is_string())
            {
                ent.lua_script = (*v)->as_string();
            }
            rep.last_created_entity_id = stage.add_entity(std::move(ent));
            ++rep.applied_count;
        }
        else if (op == "delete_entity_2d")
        {
            auto id_v = op_val->at("id");
            if (!id_v.has_value() || !(*id_v)->is_number() ||
                !stage.remove_entity(static_cast<std::uint32_t>((*id_v)->as_number())))
            {
                rep.ok            = false;
                rep.error_message = "delete_entity_2d: valid 'id' not found";
                (void)stage.load_from_json_string(batch_before);
                return rep;
            }
            ++rep.applied_count;
        }
        else if (op == "set_entity_field_2d")
        {
            auto id_v  = op_val->at("id");
            auto fld_v = op_val->at("field");
            auto val_v = op_val->at("value");
            if (!id_v.has_value() || !(*id_v)->is_number() ||
                !fld_v.has_value() || !(*fld_v)->is_string() ||
                !val_v.has_value())
            {
                rep.ok            = false;
                rep.error_message = "set_entity_field_2d requires 'id', 'field', and 'value'";
                (void)stage.load_from_json_string(batch_before);
                return rep;
            }
            Entity2D* ent = stage.find_entity(static_cast<std::uint32_t>((*id_v)->as_number()));
            const auto* fdesc = entity_schema.find_field((*fld_v)->as_string());
            if (ent == nullptr || fdesc == nullptr)
            {
                rep.ok            = false;
                rep.error_message = "set_entity_field_2d: unknown entity id or schema field";
                (void)stage.load_from_json_string(batch_before);
                return rep;
            }
            auto fv = json_value_to_field_value(**val_v, fdesc->kind);
            if (!fv.has_value() || !entity_schema.write(*ent, fdesc->name, *fv, true).has_value())
            {
                rep.ok            = false;
                rep.error_message = "set_entity_field_2d: value failed cd::schema validation";
                (void)stage.load_from_json_string(batch_before);
                return rep;
            }
            ent->home_position = ent->position;
            ++rep.applied_count;
        }
        else if (op == "paint_rect_2d" || op == "paint_tile_2d")
        {
            const int x0 = op_val->at("x0").has_value() ? static_cast<int>((*op_val->at("x0"))->as_number())
                         : (op_val->at("gx").has_value() ? static_cast<int>((*op_val->at("gx"))->as_number()) : 0);
            const int y0 = op_val->at("y0").has_value() ? static_cast<int>((*op_val->at("y0"))->as_number())
                         : (op_val->at("gy").has_value() ? static_cast<int>((*op_val->at("gy"))->as_number()) : 0);
            const int x1 = op_val->at("x1").has_value() ? static_cast<int>((*op_val->at("x1"))->as_number()) : x0;
            const int y1 = op_val->at("y1").has_value() ? static_cast<int>((*op_val->at("y1"))->as_number()) : y0;
            const auto pal = op_val->at("palette_id").has_value()
                ? static_cast<std::uint16_t>(std::max(0, static_cast<int>((*op_val->at("palette_id"))->as_number())))
                : std::uint16_t { 1U };
            stage.tilemap().paint_rect(x0, y0, x1, y1, pal, true);
            stage.rebuild_baked_colliders();
            ++rep.applied_count;
        }
        else if (op == "attach_lua_script_2d")
        {
            auto id_v = op_val->at("id");
            auto sc_v = op_val->at("script");
            if (!id_v.has_value() || !(*id_v)->is_number() || !sc_v.has_value() || !(*sc_v)->is_string())
            {
                rep.ok            = false;
                rep.error_message = "attach_lua_script_2d requires 'id' and 'script'";
                (void)stage.load_from_json_string(batch_before);
                return rep;
            }
            const auto eid = static_cast<std::uint32_t>((*id_v)->as_number());
            Entity2D*  ent = stage.find_entity(eid);
            if (ent == nullptr)
            {
                rep.ok            = false;
                rep.error_message = "attach_lua_script_2d: unknown entity id";
                (void)stage.load_from_json_string(batch_before);
                return rep;
            }
            ent->lua_script = (*sc_v)->as_string();
            if (!stage.compile_entity_lua_script(eid))
            {
                rep.ok            = false;
                rep.error_message = "Lua compile error: " + ent->lua_last_error;
                (void)stage.load_from_json_string(batch_before);
                return rep;
            }
            ++rep.applied_count;
        }
        else if (op == "load_starter_platformer_2d")
        {
            stage.build_starter_illuminated_platformer();
            ++rep.applied_count;
        }
        else
        {
            rep.ok            = false;
            rep.error_message = "Unknown CommandBus2D op: " + op;
            (void)stage.load_from_json_string(batch_before);
            return rep;
        }
    }

    rep.summary = "Applied " + std::to_string(rep.applied_count) + " AI/Studio 2D command(s)";
    push_record(rep.summary, batch_before, stage.save_to_json_string(false));
    return rep;
}

bool CommandBus2D::undo(Stage2D& stage)
{
    if (undo_stack_.empty())
    {
        return false;
    }
    SnapshotRecord rec = std::move(undo_stack_.back());
    undo_stack_.pop_back();
    (void)stage.load_from_json_string(rec.before_json);
    redo_stack_.push_back(std::move(rec));
    return true;
}

bool CommandBus2D::redo(Stage2D& stage)
{
    if (redo_stack_.empty())
    {
        return false;
    }
    SnapshotRecord rec = std::move(redo_stack_.back());
    redo_stack_.pop_back();
    (void)stage.load_from_json_string(rec.after_json);
    undo_stack_.push_back(std::move(rec));
    return true;
}

void CommandBus2D::clear_history() noexcept
{
    undo_stack_.clear();
    redo_stack_.clear();
    stroke_open_ = false;
}

std::string_view CommandBus2D::last_action_description() const noexcept
{
    if (undo_stack_.empty())
    {
        return {};
    }
    return undo_stack_.back().description;
}

}  // namespace cd::world2d
