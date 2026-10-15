// =============================================================================
// CHROMODYNAMIC — cd/gameplay_framework/VesselPilotDirector.cpp
// FAZ 5 Wave 5.3 — Gameplay Framework Implementation.
// =============================================================================
#include <cd/gameplay_framework/VesselPilotDirector.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace cd::gameplay_vessel
{

Vessel::~Vessel()
{
    disembark();
}

void Vessel::embark(cd::gameplay_pilot::Pilot* pilot) noexcept
{
    if (pilot_ == pilot)
        return;
    if (pilot_ != nullptr)
        disembark();
    pilot_ = pilot;
    if (pilot_ != nullptr)
    {
        if (pilot_->current_vessel() != this)
            pilot_->command_vessel(this);
        on_embarked(pilot_);
        on_embarked_pulse.emit(this, pilot_);
    }
}

void Vessel::disembark() noexcept
{
    if (pilot_ == nullptr)
        return;
    auto* prev_pilot = pilot_;
    pilot_ = nullptr;
    move_intent_ = { 0.0F, 0.0F, 0.0F };
    look_delta_ = { 0.0F, 0.0F };
    if (prev_pilot->current_vessel() == this)
        prev_pilot->release_vessel();
    on_disembarked();
    on_disembarked_pulse.emit(this);
}

Avatar::Avatar(std::string n)
    : Vessel(std::move(n))
{
    mesh_ = &emplace_child<cd::elements_visual::RiggedMesh3D>("AvatarRiggedMesh");
    spring_arm_ = &emplace_child<cd::elements_visual::SpringArm3D>("AvatarSpringArm");
    camera_lens_ = &spring_arm_->emplace_child<cd::elements_visual::CameraLens3D>("AvatarCameraLens");
}

void Avatar::trigger_jump() noexcept
{
    if (gait == GaitState::kGrounded)
    {
        vertical_velocity_mps_ = jump_impulse_mps;
        gait = GaitState::kAirborne;
    }
}

void Avatar::on_fixed_tick(float fixed_dt)
{
    if (fixed_dt <= 0.0F)
        return;

    const auto intent = pending_move_intent();
    auto pos = local_position();
    pos[0] += intent[0] * walk_speed_mps * fixed_dt;
    pos[2] += intent[2] * walk_speed_mps * fixed_dt;

    if (gait == GaitState::kAirborne)
    {
        pos[1] += vertical_velocity_mps_ * fixed_dt;
        vertical_velocity_mps_ -= 9.81F * fixed_dt;
        if (pos[1] <= 0.0F)
        {
            pos[1] = 0.0F;
            vertical_velocity_mps_ = 0.0F;
            gait = GaitState::kGrounded;
        }
    }

    set_local_position(pos[0], pos[1], pos[2]);
}

void Avatar::reflect_properties(std::vector<cd::element_core::PropertyDesc>& out) const
{
    Vessel::reflect_properties(out);
    out.push_back(cd::element_core::PropertyDesc {
        "walk_speed_mps", "Avatar", cd::element_core::PropertyKind::kFloat, 0.0F, 50.0F, std::to_string(walk_speed_mps)
    });
    out.push_back(cd::element_core::PropertyDesc {
        "sprint_speed_mps", "Avatar", cd::element_core::PropertyKind::kFloat, 0.0F, 50.0F, std::to_string(sprint_speed_mps)
    });
    out.push_back(cd::element_core::PropertyDesc {
        "enable_foot_ik", "Avatar", cd::element_core::PropertyKind::kBool, 0.0F, 1.0F, enable_foot_ik ? "true" : "false"
    });
}

void RoverVessel::set_vehicle_controls(float throttle, float steer_deg, float brake) noexcept
{
    throttle_input = std::clamp(throttle, -1.0F, 1.0F);
    steering_angle_deg = std::clamp(steer_deg, -45.0F, 45.0F);
    brake_input = std::clamp(brake, 0.0F, 1.0F);
}

void RoverVessel::on_fixed_tick(float fixed_dt)
{
    if (fixed_dt <= 0.0F)
        return;

    const float accel = throttle_input * (engine_peak_torque_nm * 0.02F) - brake_input * 12.0F;
    forward_speed_mps = std::max(0.0F, forward_speed_mps + accel * fixed_dt);
    current_gear = std::clamp(1 + static_cast<std::int32_t>(forward_speed_mps / 8.0F), 1, 6);

    auto pos = local_position();
    const float rad = steering_angle_deg * (3.14159265F / 180.0F);
    pos[0] += std::sin(rad) * forward_speed_mps * fixed_dt;
    pos[2] += std::cos(rad) * forward_speed_mps * fixed_dt;
    set_local_position(pos[0], pos[1], pos[2]);
}

}  // namespace cd::gameplay_vessel

namespace cd::gameplay_pilot
{

Pilot::~Pilot()
{
    release_vessel();
}

void Pilot::command_vessel(cd::gameplay_vessel::Vessel* vessel) noexcept
{
    if (vessel_ == vessel)
        return;
    if (vessel_ != nullptr)
        vessel_->disembark();
    vessel_ = vessel;
    if (vessel_ != nullptr)
        vessel_->embark(this);
}

void Pilot::release_vessel() noexcept
{
    if (vessel_ != nullptr)
    {
        auto* old = vessel_;
        vessel_ = nullptr;
        old->disembark();
    }
}

void HumanPilot::inject_move_and_look(std::array<float, 3> move_xyz, float yaw_delta, float pitch_delta) noexcept
{
    if (record_inputs)
        ++recorded_frames_;
    if (auto* v = current_vessel())
    {
        v->apply_movement_input(move_xyz);
        v->apply_look_input(yaw_delta * look_sensitivity, pitch_delta * look_sensitivity);
    }
}

void SynthPilot::on_tick(float /*dt*/)
{
    auto* v = current_vessel();
    if (v == nullptr)
        return;

    const auto pos = v->world_position();
    const float dx = patrol_objective[0] - pos[0];
    const float dy = patrol_objective[1] - pos[1];
    const float dz = patrol_objective[2] - pos[2];
    const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len > 1e-3F)
    {
        v->apply_movement_input({ dx / len, dy / len, dz / len });
    }
    else
    {
        v->apply_movement_input({ 0.0F, 0.0F, 0.0F });
    }
}

}  // namespace cd::gameplay_pilot

namespace cd::gameplay_director
{

void WorldLedger::set_score(std::string_view key, std::int64_t value)
{
    scores_[std::string { key }] = value;
}

std::int64_t WorldLedger::score(std::string_view key) const noexcept
{
    const auto it = scores_.find(std::string { key });
    return (it != scores_.end()) ? it->second : 0;
}

void WorldLedger::set_flag(std::string_view key, bool value)
{
    flags_[std::string { key }] = value;
}

bool WorldLedger::flag(std::string_view key) const noexcept
{
    const auto it = flags_.find(std::string { key });
    return (it != flags_.end()) ? it->second : false;
}

cd::gameplay_vessel::Avatar* StageDirector::spawn_avatar_for_pilot(
    cd::element_core::Stage& stage,
    cd::gameplay_pilot::Pilot& pilot,
    std::string_view team_tag
)
{
    const auto beacons = stage.find_all<SpawnBeacon3D>();
    const SpawnBeacon3D* chosen = nullptr;
    for (const auto* b : beacons)
    {
        if (b->team_tag == team_tag)
        {
            if (chosen == nullptr || b->priority > chosen->priority)
                chosen = b;
        }
    }

    auto& avatar = stage.root().emplace_child<cd::gameplay_vessel::Avatar>(
        std::string { pilot.name() } + "_Avatar"
    );
    if (chosen != nullptr)
    {
        const auto wp = chosen->world_position();
        avatar.set_local_position(wp[0], wp[1], wp[2]);
    }
    pilot.command_vessel(&avatar);
    return &avatar;
}

void CutsceneElement::on_tick(float dt)
{
    if (!is_playing || dt <= 0.0F)
        return;
    playhead_seconds += dt;
    if (playhead_seconds >= duration_seconds)
    {
        playhead_seconds = duration_seconds;
        is_playing = false;
    }
}

std::string_view showcase_slug(ShowcaseId id) noexcept
{
    switch (id)
    {
    case ShowcaseId::kPbrMaterialsSssHair:       return "01_pbr_materials_sss_hair";
    case ShowcaseId::kCelestialAetherFogClouds:  return "02_celestial_aether_fog_clouds";
    case ShowcaseId::kSponzaClusteredDdgiRestir: return "03_sponza_clustered_ddgi_restir";
    case ShowcaseId::kOpticsCinematicStudio:     return "04_optics_cinematic_studio";
    case ShowcaseId::kAvatarLocomotionAnimIk:    return "05_avatar_locomotion_anim_ik";
    case ShowcaseId::kJoltDynamicsRoverShatter:  return "06_jolt_dynamics_rover_shatter";
    case ShowcaseId::kTerraOceanNatureWorld:     return "07_terra_ocean_nature_world";
    case ShowcaseId::kSynthPilotSquadNarrative:  return "08_synth_pilot_squad_narrative";
    case ShowcaseId::kAcousticsHrtfEchoChamber:  return "09_acoustics_hrtf_echo_chamber";
    case ShowcaseId::kSparkSwarmAndDecals:       return "10_spark_swarm_and_decals";
    case ShowcaseId::kCustomUiHudAndMenus:       return "11_custom_ui_hud_and_menus";
    case ShowcaseId::kPlanar2dPlatformerNet:     return "12_planar_2d_platformer_net";
    }
    return "01_pbr_materials_sss_hair";
}

cd::element_core::Stage build_showcase_stage(ShowcaseId id)
{
    cd::element_core::Stage stage(std::string { showcase_slug(id) });
    auto& root = stage.root();

    switch (id)
    {
    case ShowcaseId::kPbrMaterialsSssHair:
        root.emplace_child<cd::elements_visual::SurfaceMesh3D>("PbrSphereGrid5x5");
        root.emplace_child<cd::elements_radiance::SolarLight3D>("KeySolarLight");
        root.emplace_child<cd::elements_radiance::RadianceProbe3D>("StudioReflectionProbe");
        break;
    case ShowcaseId::kCelestialAetherFogClouds:
        root.emplace_child<cd::elements_aether::CelestialDome3D>("SkyAtmosphereDome");
        root.emplace_child<cd::elements_aether::AetherFog3D>("GlobalFroxelFog");
        root.emplace_child<cd::elements_aether::FogPocket3D>("ValleyFogPocket");
        root.emplace_child<cd::elements_aether::CloudStrata3D>("CumulusStrata");
        root.emplace_child<cd::elements_aether::SunBeams3D>("CrepuscularBeams");
        break;
    case ShowcaseId::kSponzaClusteredDdgiRestir:
        root.emplace_child<cd::elements_visual::SurfaceMesh3D>("SponzaAtriumMesh");
        root.emplace_child<cd::elements_radiance::IrradianceField3D>("AtriumDdgiRestirField");
        root.emplace_child<cd::elements_radiance::RadialLight3D>("TorchClusterLight0");
        root.emplace_child<cd::elements_radiance::PanelLight3D>("BannerAreaLight");
        break;
    case ShowcaseId::kOpticsCinematicStudio:
        root.emplace_child<cd::elements_optics::OpticsVolume3D>("CinematicOpticsVolume");
        root.emplace_child<CutsceneElement>("OpeningCameraSequence");
        root.emplace_child<cd::elements_visual::CameraLens3D>("CinemaAnamorphicLens");
        break;
    case ShowcaseId::kAvatarLocomotionAnimIk:
    {
        auto& beacon = root.emplace_child<SpawnBeacon3D>("HeroSpawnBeacon");
        beacon.set_local_position(0.0F, 0.0F, 5.0F);
        auto& dir = root.emplace_child<StageDirector>("LocomotionDirector");
        auto& pilot = root.emplace_child<cd::gameplay_pilot::HumanPilot>("LocalHumanPilot");
        (void)dir.spawn_avatar_for_pilot(stage, pilot);
        break;
    }
    case ShowcaseId::kJoltDynamicsRoverShatter:
        root.emplace_child<cd::elements_dynamics::AnchorBody3D>("ArenaFloorAnchor");
        root.emplace_child<cd::elements_dynamics::KineticBody3D>("DynamicBoulder");
        root.emplace_child<cd::gameplay_vessel::RoverVessel>("OffroadRover4x4");
        root.emplace_child<cd::elements_dynamics::ClothSurface3D>("WindBannerCloth");
        root.emplace_child<cd::elements_dynamics::ShatterMesh3D>("FracturablePillar");
        root.emplace_child<cd::elements_dynamics::RagdollChain3D>("TestDummyRagdoll");
        break;
    case ShowcaseId::kTerraOceanNatureWorld:
        root.emplace_child<cd::elements_nature::TerraClipmap3D>("AlpineClipmapTerrain");
        root.emplace_child<cd::elements_nature::OceanWave3D>("FftOceanSurface");
        root.emplace_child<cd::elements_visual::SwarmMesh3D>("PineForestSwarm");
        break;
    case ShowcaseId::kSynthPilotSquadNarrative:
        root.emplace_child<cd::elements_wayfinding::NavMeshZone3D>("TacticalNavZone");
        root.emplace_child<cd::elements_wayfinding::WayfinderAgent3D>("SquadPointmanAgent");
        root.emplace_child<cd::gameplay_pilot::SynthPilot>("SquadLeaderSynth");
        root.emplace_child<DialogueElement>("CommanderDialogueTree");
        root.emplace_child<QuestLedgerElement>("MainObjectiveLedger");
        root.emplace_child<AiDirectorElement>("PacingDirector");
        break;
    case ShowcaseId::kAcousticsHrtfEchoChamber:
        root.emplace_child<cd::elements_acoustics::SoundSource3D>("HrtfBeaconSource");
        root.emplace_child<cd::elements_acoustics::AudioEar3D>("ListenerEar");
        root.emplace_child<cd::elements_acoustics::EchoChamber3D>("CathedralReverbHall");
        break;
    case ShowcaseId::kSparkSwarmAndDecals:
        root.emplace_child<cd::elements_visual::SparkSwarm3D>("GpuDepthCollisionSparks");
        root.emplace_child<cd::elements_visual::ProjectionDecal3D>("ScorchMarkDecal");
        break;
    case ShowcaseId::kCustomUiHudAndMenus:
    {
        auto& hud = root.emplace_child<cd::elements_ui::HudLayer>("MainGameHud");
        hud.emplace_child<cd::elements_ui::UiGaugeBar>("HealthGauge");
        hud.emplace_child<cd::elements_ui::UiCommandButton>("PauseMenuButton");
        hud.emplace_child<cd::elements_ui::UiValueSlider>("MasterVolumeSlider");
        hud.emplace_child<cd::elements_ui::UiVectorBox>("SpawnCoordBox");
        hud.emplace_child<cd::elements_ui::UiGlyphText>("ObjectiveBannerText");
        break;
    }
    case ShowcaseId::kPlanar2dPlatformerNet:
        root.emplace_child<cd::elements_planar::TileCanvas2D>("LevelTilemap2D");
        root.emplace_child<cd::elements_planar::Flipbook2D>("RunnerFlipbook2D");
        root.emplace_child<cd::elements_planar::KineticBody2D>("PlayerBody2D");
        root.emplace_child<cd::elements_planar::Lens2D>("FollowCamera2D");
        root.emplace_child<cd::elements_planar::RadialLight2D>("TorchLight2D");
        root.emplace_child<NetSyncElement>("CoopRollbackNetSync");
        break;
    }

    stage.mount();
    return stage;
}

cd::element_core::Stage build_showcase_stage_by_slug(std::string_view slug)
{
    for (std::size_t i = 0; i < kShowcaseCount; ++i)
    {
        const auto id = static_cast<ShowcaseId>(i);
        if (showcase_slug(id) == slug)
            return build_showcase_stage(id);
    }
    return build_showcase_stage(ShowcaseId::kPbrMaterialsSssHair);
}

}  // namespace cd::gameplay_director
