// =============================================================================
// CHROMODYNAMIC: cd/world2d/CommandBus2D.hpp
// FAZ 35 (ADR-20261111): Symmetric Human Undo/Redo + AI JSON Command Protocol.
//
// Every mutation in 2D Studio (Human mouse/brush/inspector OR AI JSON command)
// passes through `CommandBus2D`, guaranteeing 100% deterministic Undo/Redo and
// structured machine-readable responses.
// =============================================================================
#pragma once

#include <cd/core/Result.hpp>
#include <cd/schema/Schema.hpp>
#include <cd/world2d/GenreTemplates2D.hpp>
#include <cd/world2d/Stage2D.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cd::world2d
{

struct CommandExecutionReport2D
{
    bool          ok { true };
    std::uint32_t applied_count { 0U };
    std::uint32_t last_created_entity_id { 0U };
    std::string   summary {};
    std::string   error_message {};

    [[nodiscard]] std::string to_json_string() const;
};

class CommandBus2D
{
public:
    CommandBus2D() = default;

    // ---- Human & Programmatic Reversible Operations -------------------------
    std::uint32_t spawn_entity(Stage2D& stage, Entity2D entity);
    bool delete_entity(Stage2D& stage, std::uint32_t entity_id);
    bool move_entity(Stage2D& stage, std::uint32_t entity_id, cd::math::Vec2f new_pos);

    cd::core::Result<void> set_entity_field(
        Stage2D&               stage,
        std::uint32_t          entity_id,
        std::string_view       field_name,
        cd::schema::FieldValue value);

    void paint_tile(Stage2D& stage, int gx, int gy, std::uint16_t palette_id);
    void paint_rect(Stage2D& stage, int x0, int y0, int x1, int y1, std::uint16_t palette_id);
    std::size_t flood_fill(Stage2D& stage, int start_x, int start_y, std::uint16_t replacement_id);
    std::size_t stamp(
        Stage2D&                       stage,
        int                            start_x,
        int                            start_y,
        int                            pattern_width,
        int                            pattern_height,
        std::span<const std::uint16_t> pattern,
        bool                           update_autotile = true);
    bool attach_lua_script(Stage2D& stage, std::uint32_t entity_id, std::string lua_code);
    void load_starter_platformer(Stage2D& stage);
    void load_genre_template(Stage2D& stage, GenreTemplateKind2D kind);

    /// Generic snapshot transaction helper for multi-tile brush strokes in the viewport:
    /// call `begin_stroke(stage, description)` on mouse-down and `end_stroke(stage)` on mouse-up.
    void begin_stroke(const Stage2D& stage, std::string description);
    void end_stroke(Stage2D& stage);

    // ---- Symmetric AI JSON Command Execution --------------------------------
    /// Accepts either a single JSON command object `{"op": "spawn_entity_2d", ...}`
    /// or a JSON array / `{"commands": [ ... ]}` batch and executes atomically.
    [[nodiscard]] CommandExecutionReport2D
    execute_json(Stage2D& stage, std::string_view json_text);

    // ---- Undo / Redo Stack --------------------------------------------------
    bool undo(Stage2D& stage);
    bool redo(Stage2D& stage);
    void clear_history() noexcept;

    [[nodiscard]] std::size_t undo_depth() const noexcept { return undo_stack_.size(); }
    [[nodiscard]] std::size_t redo_depth() const noexcept { return redo_stack_.size(); }
    [[nodiscard]] std::string_view last_action_description() const noexcept;

private:
    struct SnapshotRecord
    {
        std::string description {};
        std::string before_json {};
        std::string after_json {};
    };

    void push_record(std::string description, std::string before_json, std::string after_json);

    std::vector<SnapshotRecord> undo_stack_ {};
    std::vector<SnapshotRecord> redo_stack_ {};
    bool                        stroke_open_ { false };
    std::string                 stroke_desc_ {};
    std::string                 stroke_before_json_ {};
};

}  // namespace cd::world2d
