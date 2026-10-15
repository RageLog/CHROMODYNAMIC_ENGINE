// =============================================================================
// CHROMODYNAMIC — cd/gameplay_framework/VesselPilotDirector.hpp
// FAZ 5 Wave 5.3 — Original CHROMODYNAMIC Gameplay Framework:
//   * cd::gameplay_vessel   : Vessel (embark/disembark), Avatar (humanoid Jolt
//                             capsule + Locomotion + RiggedMesh3D + AnimGraph +
//                             IkRig + SpringArm3D + CameraLens3D), RoverVessel
//                             (4+ wheel drivable vehicle)
//   * cd::gameplay_pilot    : Pilot, HumanPilot (ActionMap + CameraBrain +
//                             InputRecorder + HudLayer), SynthPilot (BehaviorTree +
//                             StateMachine + WayfinderAgent3D + SquadCoordinator)
//   * cd::gameplay_director : StageDirector, WorldLedger, SpawnBeacon3D,
//                             AiDirectorElement, QuestLedgerElement,
//                             DialogueElement, CutsceneElement,
//                             LuaBehaviorElement, NetSyncElement
// =============================================================================
#pragma once

#include <cd/element_core/Element.hpp>
#include <cd/elements_domain/DomainElements.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cd::gameplay_pilot
{
class Pilot;
}  // namespace cd::gameplay_pilot

// =============================================================================
// 1. cd::gameplay_vessel — Vessel, Avatar, RoverVessel
// =============================================================================
namespace cd::gameplay_vessel
{

class Vessel : public cd::element_core::Spatial3D
{
public:
    explicit Vessel(std::string n = "Vessel") : Spatial3D(std::move(n)) {}
    ~Vessel() override;
    [[nodiscard]] std::string_view class_name() const noexcept override { return "Vessel"; }

    /// Attach a `Pilot` (`HumanPilot` or `SynthPilot`) to command this Vessel.
    void embark(cd::gameplay_pilot::Pilot* pilot) noexcept;

    /// Detach the active `Pilot` from this Vessel.
    void disembark() noexcept;

    [[nodiscard]] cd::gameplay_pilot::Pilot* active_pilot() const noexcept { return pilot_; }
    [[nodiscard]] bool is_occupied() const noexcept { return pilot_ != nullptr; }

    virtual void apply_movement_input(std::array<float, 3> world_move_intent) noexcept
    {
        move_intent_ = world_move_intent;
    }

    virtual void apply_look_input(float delta_yaw_deg, float delta_pitch_deg) noexcept
    {
        look_delta_ = { delta_yaw_deg, delta_pitch_deg };
    }

    [[nodiscard]] std::array<float, 3> pending_move_intent() const noexcept { return move_intent_; }
    [[nodiscard]] std::array<float, 2> pending_look_delta() const noexcept { return look_delta_; }

    cd::element_core::Pulse<Vessel*, cd::gameplay_pilot::Pilot*> on_embarked_pulse {};
    cd::element_core::Pulse<Vessel*> on_disembarked_pulse {};

protected:
    virtual void on_embarked(cd::gameplay_pilot::Pilot* /*pilot*/) {}
    virtual void on_disembarked() {}

private:
    cd::gameplay_pilot::Pilot* pilot_ { nullptr };
    std::array<float, 3> move_intent_ { 0.0F, 0.0F, 0.0F };
    std::array<float, 2> look_delta_ { 0.0F, 0.0F };
};

class Avatar : public Vessel
{
public:
    enum class GaitState : std::uint8_t
    {
        kGrounded = 0,
        kAirborne = 1,
        kCrouching = 2,
        kMantling = 3,
        kSwimming = 4,
    };

    explicit Avatar(std::string n = "Avatar");
    [[nodiscard]] std::string_view class_name() const noexcept override { return "Avatar"; }

    float capsule_radius_m { 0.35F };
    float capsule_half_height_m { 0.90F };
    float walk_speed_mps { 5.5F };
    float sprint_speed_mps { 8.5F };
    float jump_impulse_mps { 6.2F };
    bool  enable_foot_ik { true };
    GaitState gait { GaitState::kGrounded };

    [[nodiscard]] cd::elements_visual::RiggedMesh3D* rigged_mesh() noexcept { return mesh_; }
    [[nodiscard]] cd::elements_visual::SpringArm3D* spring_arm() noexcept { return spring_arm_; }
    [[nodiscard]] cd::elements_visual::CameraLens3D* camera_lens() noexcept { return camera_lens_; }

    void trigger_jump() noexcept;
    void on_fixed_tick(float fixed_dt) override;
    void reflect_properties(std::vector<cd::element_core::PropertyDesc>& out) const override;

private:
    cd::elements_visual::RiggedMesh3D* mesh_ { nullptr };
    cd::elements_visual::SpringArm3D* spring_arm_ { nullptr };
    cd::elements_visual::CameraLens3D* camera_lens_ { nullptr };
    float vertical_velocity_mps_ { 0.0F };
};

class RoverVessel : public Vessel
{
public:
    explicit RoverVessel(std::string n = "RoverVessel") : Vessel(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "RoverVessel"; }

    std::uint32_t wheel_count { 4 };
    float engine_peak_torque_nm { 480.0F };
    float wheelbase_m { 2.75F };
    float throttle_input { 0.0F };   ///< [-1, 1]
    float steering_angle_deg { 0.0F };
    float brake_input { 0.0F };      ///< [0, 1]
    float forward_speed_mps { 0.0F };
    std::int32_t current_gear { 1 };

    void set_vehicle_controls(float throttle, float steer_deg, float brake) noexcept;
    void on_fixed_tick(float fixed_dt) override;
};

}  // namespace cd::gameplay_vessel

// =============================================================================
// 2. cd::gameplay_pilot — Pilot, HumanPilot, SynthPilot
// =============================================================================
namespace cd::gameplay_pilot
{

class Pilot : public cd::element_core::Element
{
public:
    explicit Pilot(std::string n = "Pilot") : Element(std::move(n)) {}
    ~Pilot() override;
    [[nodiscard]] std::string_view class_name() const noexcept override { return "Pilot"; }

    /// Take command of `vessel` (automatically disembarks any previously possessed Vessel).
    void command_vessel(cd::gameplay_vessel::Vessel* vessel) noexcept;
    void release_vessel() noexcept;

    [[nodiscard]] cd::gameplay_vessel::Vessel* current_vessel() const noexcept { return vessel_; }

private:
    cd::gameplay_vessel::Vessel* vessel_ { nullptr };
};

class HumanPilot : public Pilot
{
public:
    explicit HumanPilot(std::string n = "HumanPilot") : Pilot(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "HumanPilot"; }

    std::uint32_t player_index { 0 };
    float look_sensitivity { 0.15F };
    bool record_inputs { false };

    void inject_move_and_look(std::array<float, 3> move_xyz, float yaw_delta, float pitch_delta) noexcept;
    [[nodiscard]] std::uint64_t recorded_frame_count() const noexcept { return recorded_frames_; }

private:
    std::uint64_t recorded_frames_ { 0 };
};

class SynthPilot : public Pilot
{
public:
    enum class TacticalRole : std::uint8_t
    {
        kPointman = 0,
        kFlanker = 1,
        kSuppressor = 2,
        kCoverer = 3,
    };

    explicit SynthPilot(std::string n = "SynthPilot") : Pilot(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "SynthPilot"; }

    TacticalRole role { TacticalRole::kPointman };
    std::string behavior_tree_asset {};
    std::array<float, 3> patrol_objective { 0.0F, 0.0F, 0.0F };
    float desired_speed_mps { 4.0F };

    void on_tick(float dt) override;
};

}  // namespace cd::gameplay_pilot

// =============================================================================
// 3. cd::gameplay_director — StageDirector, WorldLedger, SpawnBeacon3D & Systems
// =============================================================================
namespace cd::gameplay_director
{

class SpawnBeacon3D : public cd::element_core::Spatial3D
{
public:
    explicit SpawnBeacon3D(std::string n = "SpawnBeacon3D") : Spatial3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "SpawnBeacon3D"; }
    std::string team_tag { "Default" };
    std::uint32_t priority { 0 };
};

class WorldLedger : public cd::element_core::Element
{
public:
    explicit WorldLedger(std::string n = "WorldLedger") : Element(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "WorldLedger"; }

    void set_score(std::string_view key, std::int64_t value);
    [[nodiscard]] std::int64_t score(std::string_view key) const noexcept;

    void set_flag(std::string_view key, bool value);
    [[nodiscard]] bool flag(std::string_view key) const noexcept;

private:
    std::unordered_map<std::string, std::int64_t> scores_ {};
    std::unordered_map<std::string, bool> flags_ {};
};

class StageDirector : public cd::element_core::Element
{
public:
    explicit StageDirector(std::string n = "StageDirector") : Element(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "StageDirector"; }

    /// Spawn an `Avatar` at the highest-priority matching `SpawnBeacon3D` in `stage`
    /// and attach `pilot` to command it.
    cd::gameplay_vessel::Avatar* spawn_avatar_for_pilot(
        cd::element_core::Stage& stage,
        cd::gameplay_pilot::Pilot& pilot,
        std::string_view team_tag = "Default"
    );
};

class AiDirectorElement : public cd::element_core::Element
{
public:
    explicit AiDirectorElement(std::string n = "AiDirectorElement") : Element(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "AiDirectorElement"; }
    float tension_score { 0.25F };
    float peak_threshold { 0.85F };
};

class QuestLedgerElement : public cd::element_core::Element
{
public:
    explicit QuestLedgerElement(std::string n = "QuestLedgerElement") : Element(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "QuestLedgerElement"; }
    std::string active_quest_id {};
};

class DialogueElement : public cd::element_core::Element
{
public:
    explicit DialogueElement(std::string n = "DialogueElement") : Element(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "DialogueElement"; }
    std::string dialogue_graph_asset {};
};

class CutsceneElement : public cd::element_core::Element
{
public:
    explicit CutsceneElement(std::string n = "CutsceneElement") : Element(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "CutsceneElement"; }
    float duration_seconds { 10.0F };
    float playhead_seconds { 0.0F };
    bool is_playing { false };
    void on_tick(float dt) override;
};

class LuaBehaviorElement : public cd::element_core::Element
{
public:
    explicit LuaBehaviorElement(std::string n = "LuaBehaviorElement") : Element(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "LuaBehaviorElement"; }
    std::string script_path {};
    bool hot_reload_enabled { true };
};

class NetSyncElement : public cd::element_core::Element
{
public:
    explicit NetSyncElement(std::string n = "NetSyncElement") : Element(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "NetSyncElement"; }
    std::uint32_t net_authority_peer { 0 };
    float snapshot_rate_hz { 30.0F };
};

enum class ShowcaseId : std::uint8_t
{
    kPbrMaterialsSssHair = 0,
    kCelestialAetherFogClouds = 1,
    kSponzaClusteredDdgiRestir = 2,
    kOpticsCinematicStudio = 3,
    kAvatarLocomotionAnimIk = 4,
    kJoltDynamicsRoverShatter = 5,
    kTerraOceanNatureWorld = 6,
    kSynthPilotSquadNarrative = 7,
    kAcousticsHrtfEchoChamber = 8,
    kSparkSwarmAndDecals = 9,
    kCustomUiHudAndMenus = 10,
    kPlanar2dPlatformerNet = 11,
};

inline constexpr std::size_t kShowcaseCount = 12U;

[[nodiscard]] std::string_view showcase_slug(ShowcaseId id) noexcept;
[[nodiscard]] cd::element_core::Stage build_showcase_stage(ShowcaseId id);
[[nodiscard]] cd::element_core::Stage build_showcase_stage_by_slug(std::string_view slug);

}  // namespace cd::gameplay_director

