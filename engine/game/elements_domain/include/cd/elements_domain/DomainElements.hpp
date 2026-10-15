// =============================================================================
// CHROMODYNAMIC — cd/elements_domain/DomainElements.hpp
// FAZ 5 Wave 5.2 — Original CHROMODYNAMIC Domain Element Families:
//   * cd::elements_visual     : SurfaceMesh3D, RiggedMesh3D, SwarmMesh3D,
//                               ProjectionDecal3D, SparkSwarm3D, CameraLens3D, SpringArm3D
//   * cd::elements_radiance   : SolarLight3D, RadialLight3D, ConeLight3D,
//                               PanelLight3D, RadianceProbe3D, IrradianceField3D
//   * cd::elements_aether     : CelestialDome3D, AetherFog3D, FogPocket3D,
//                               CloudStrata3D, SunBeams3D
//   * cd::elements_optics     : OpticsVolume3D
//   * cd::elements_nature     : TerraClipmap3D, OceanWave3D
//   * cd::elements_dynamics   : AnchorBody3D, KineticBody3D, ClothSurface3D,
//                               ShatterMesh3D, RagdollChain3D, SensorVolume3D,
//                               CollisionHull3D, RayProbe3D
//   * cd::elements_acoustics  : SoundSource3D (MIT KEMAR HRTF + Doppler),
//                               AudioEar3D, EchoChamber3D
//   * cd::elements_wayfinding : NavMeshZone3D, WayfinderAgent3D, SplineTrack3D
//   * cd::elements_planar     : QuadSprite2D, Flipbook2D, TileCanvas2D,
//                               Lens2D, KineticBody2D, SensorZone2D, RadialLight2D
//   * cd::elements_ui         : HudLayer, UiFrame, UiCommandButton,
//                               UiValueSlider, UiVectorBox, UiGlyphText, UiGaugeBar
// =============================================================================
#pragma once

#include <cd/element_core/Element.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// =============================================================================
// 1. cd::elements_visual
// =============================================================================
namespace cd::elements_visual
{

class SurfaceMesh3D : public cd::element_core::Spatial3D
{
public:
    explicit SurfaceMesh3D(std::string n = "SurfaceMesh3D") : Spatial3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "SurfaceMesh3D"; }
    std::string mesh_asset_path {};
    std::string material_asset_path {};
    bool cast_shadows { true };
    void reflect_properties(std::vector<cd::element_core::PropertyDesc>& out) const override;
};

class RiggedMesh3D : public SurfaceMesh3D
{
public:
    explicit RiggedMesh3D(std::string n = "RiggedMesh3D") : SurfaceMesh3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "RiggedMesh3D"; }
    std::string skeleton_asset_path {};
    std::string active_clip_name { "Idle" };
    float playback_speed { 1.0F };
};

class SwarmMesh3D : public SurfaceMesh3D
{
public:
    explicit SwarmMesh3D(std::string n = "SwarmMesh3D") : SurfaceMesh3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "SwarmMesh3D"; }
    std::uint32_t instance_count { 1024 };
    float cull_distance_meters { 250.0F };
};

class ProjectionDecal3D : public cd::element_core::Spatial3D
{
public:
    explicit ProjectionDecal3D(std::string n = "ProjectionDecal3D") : Spatial3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "ProjectionDecal3D"; }
    std::string decal_texture_path {};
    float opacity { 1.0F };
    float normal_blend { 0.5F };
};

class SparkSwarm3D : public cd::element_core::Spatial3D
{
public:
    explicit SparkSwarm3D(std::string n = "SparkSwarm3D") : Spatial3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "SparkSwarm3D"; }
    std::uint32_t max_particles { 16384 };
    float emission_rate { 500.0F };
    bool depth_buffer_collision { true };
};

class CameraLens3D : public cd::element_core::Spatial3D
{
public:
    explicit CameraLens3D(std::string n = "CameraLens3D") : Spatial3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "CameraLens3D"; }
    float vertical_fov_deg { 60.0F };
    float near_plane { 0.1F };
    float far_plane { 2000.0F };
    float aperture_fstop { 2.8F };
    float shutter_speed_inv { 125.0F };
    float iso { 100.0F };
    void reflect_properties(std::vector<cd::element_core::PropertyDesc>& out) const override;
};

class SpringArm3D : public cd::element_core::Spatial3D
{
public:
    explicit SpringArm3D(std::string n = "SpringArm3D") : Spatial3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "SpringArm3D"; }
    float target_arm_length { 3.5F };
    float probe_radius { 0.25F };
    float lag_speed { 12.0F };
    [[nodiscard]] float compute_clipped_arm_length(float hit_distance) const noexcept;
};

}  // namespace cd::elements_visual

// =============================================================================
// 2. cd::elements_radiance
// =============================================================================
namespace cd::elements_radiance
{

class SolarLight3D : public cd::element_core::Spatial3D
{
public:
    explicit SolarLight3D(std::string n = "SolarLight3D") : Spatial3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "SolarLight3D"; }
    float illuminance_lux { 100000.0F };
    float temperature_kelvin { 5800.0F };
    std::uint32_t csm_cascade_count { 4 };
    bool enable_pcss { true };
    bool enable_contact_shadows { true };
    void reflect_properties(std::vector<cd::element_core::PropertyDesc>& out) const override;
};

class RadialLight3D : public cd::element_core::Spatial3D
{
public:
    explicit RadialLight3D(std::string n = "RadialLight3D") : Spatial3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "RadialLight3D"; }
    float luminous_power_lumens { 1200.0F };
    float temperature_kelvin { 3200.0F };
    float attenuation_radius { 12.0F };
    float source_radius { 0.08F };
};

class ConeLight3D : public RadialLight3D
{
public:
    explicit ConeLight3D(std::string n = "ConeLight3D") : RadialLight3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "ConeLight3D"; }
    float inner_cone_deg { 20.0F };
    float outer_cone_deg { 35.0F };
    std::string ies_profile_path {};
};

class PanelLight3D : public cd::element_core::Spatial3D
{
public:
    explicit PanelLight3D(std::string n = "PanelLight3D") : Spatial3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "PanelLight3D"; }
    float luminous_power_lumens { 2500.0F };
    float width_meters { 1.5F };
    float height_meters { 0.8F };
    bool two_sided { false };
};

class RadianceProbe3D : public cd::element_core::Spatial3D
{
public:
    explicit RadianceProbe3D(std::string n = "RadianceProbe3D") : Spatial3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "RadianceProbe3D"; }
    std::array<float, 3> box_extents { 8.0F, 4.0F, 8.0F };
    bool box_parallax_correction { true };
};

class IrradianceField3D : public cd::element_core::Spatial3D
{
public:
    explicit IrradianceField3D(std::string n = "IrradianceField3D") : Spatial3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "IrradianceField3D"; }
    std::array<std::uint32_t, 3> probe_grid_counts { 8U, 4U, 8U };
    std::array<float, 3> probe_spacing_meters { 2.0F, 2.0F, 2.0F };
    bool enable_restir_gi { true };
    bool enable_nrc { false };
};

}  // namespace cd::elements_radiance

// =============================================================================
// 3. cd::elements_aether & cd::elements_optics
// =============================================================================
namespace cd::elements_aether
{

class CelestialDome3D : public cd::element_core::Spatial3D
{
public:
    explicit CelestialDome3D(std::string n = "CelestialDome3D") : Spatial3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "CelestialDome3D"; }
    float planet_radius_km { 6360.0F };
    float atmosphere_height_km { 100.0F };
    float mie_anisotropy_g { 0.8F };
    float time_of_day_hours { 14.5F };
    void reflect_properties(std::vector<cd::element_core::PropertyDesc>& out) const override;
};

class AetherFog3D : public cd::element_core::Spatial3D
{
public:
    explicit AetherFog3D(std::string n = "AetherFog3D") : Spatial3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "AetherFog3D"; }
    float base_density { 0.015F };
    float height_falloff { 0.12F };
    float scattering_anisotropy { 0.65F };
    float temporal_blend_weight { 0.92F };
    std::array<float, 3> inscatter_tint { 0.78F, 0.86F, 1.0F };
    void reflect_properties(std::vector<cd::element_core::PropertyDesc>& out) const override;
};

class FogPocket3D : public cd::element_core::Spatial3D
{
public:
    explicit FogPocket3D(std::string n = "FogPocket3D") : Spatial3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "FogPocket3D"; }
    std::array<float, 3> half_extents { 4.0F, 2.0F, 4.0F };
    float local_density { 0.15F };
    std::array<float, 3> emission_color { 0.0F, 0.0F, 0.0F };
};

class CloudStrata3D : public cd::element_core::Spatial3D
{
public:
    explicit CloudStrata3D(std::string n = "CloudStrata3D") : Spatial3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "CloudStrata3D"; }
    float bottom_altitude_m { 1500.0F };
    float thickness_m { 2500.0F };
    float coverage { 0.55F };
    std::array<float, 2> wind_velocity_mps { 8.0F, 3.0F };
};

class SunBeams3D : public cd::element_core::Spatial3D
{
public:
    explicit SunBeams3D(std::string n = "SunBeams3D") : Spatial3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "SunBeams3D"; }
    float density { 0.9F };
    float weight { 0.35F };
    float decay { 0.96F };
    float exposure { 0.6F };
};

}  // namespace cd::elements_aether

namespace cd::elements_optics
{

class OpticsVolume3D : public cd::element_core::Spatial3D
{
public:
    explicit OpticsVolume3D(std::string n = "OpticsVolume3D") : Spatial3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "OpticsVolume3D"; }
    bool is_global { true };
    float blend_weight { 1.0F };
    bool enable_gtao { true };
    bool enable_ssr { true };
    bool enable_taa { true };
    bool enable_bloom { true };
    float bloom_intensity { 0.08F };
    bool enable_dof { false };
    float dof_focus_distance_m { 10.0F };
    bool enable_motion_blur { true };
    std::string tonemap_operator { "AgX" };
    void reflect_properties(std::vector<cd::element_core::PropertyDesc>& out) const override;
};

}  // namespace cd::elements_optics

// =============================================================================
// 4. cd::elements_nature
// =============================================================================
namespace cd::elements_nature
{

class TerraClipmap3D : public cd::element_core::Spatial3D
{
public:
    explicit TerraClipmap3D(std::string n = "TerraClipmap3D") : Spatial3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "TerraClipmap3D"; }
    std::string heightmap_asset_path {};
    std::uint32_t clipmap_rings { 6 };
    float max_elevation_m { 450.0F };
};

class OceanWave3D : public cd::element_core::Spatial3D
{
public:
    explicit OceanWave3D(std::string n = "OceanWave3D") : Spatial3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "OceanWave3D"; }
    float wind_speed_mps { 14.0F };
    float phillips_amplitude { 0.0004F };
    float choppiness_lambda { 1.2F };
    float foam_threshold { 0.65F };
};

}  // namespace cd::elements_nature

// =============================================================================
// 5. cd::elements_dynamics
// =============================================================================
namespace cd::elements_dynamics
{

class AnchorBody3D : public cd::element_core::Spatial3D
{
public:
    explicit AnchorBody3D(std::string n = "AnchorBody3D") : Spatial3D(std::move(n))
    {
        set_mobility(cd::element_core::Mobility::kStatic);
    }
    [[nodiscard]] std::string_view class_name() const noexcept override { return "AnchorBody3D"; }
    float friction { 0.6F };
    float restitution { 0.1F };
};

class KineticBody3D : public cd::element_core::Spatial3D
{
public:
    explicit KineticBody3D(std::string n = "KineticBody3D") : Spatial3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "KineticBody3D"; }
    float mass_kg { 10.0F };
    float linear_damping { 0.05F };
    float angular_damping { 0.05F };
    std::array<float, 3> linear_velocity { 0.0F, 0.0F, 0.0F };
    void on_fixed_tick(float fixed_dt) override;
};

class ClothSurface3D : public cd::element_core::Spatial3D
{
public:
    explicit ClothSurface3D(std::string n = "ClothSurface3D") : Spatial3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "ClothSurface3D"; }
    std::uint32_t grid_width { 16 };
    std::uint32_t grid_height { 16 };
    std::uint32_t solver_iterations { 8 };
};

class ShatterMesh3D : public cd::element_core::Spatial3D
{
public:
    explicit ShatterMesh3D(std::string n = "ShatterMesh3D") : Spatial3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "ShatterMesh3D"; }
    std::uint32_t voronoi_cell_count { 24 };
    float fracture_impulse_threshold { 50.0F };
    bool is_fractured { false };
    cd::element_core::Pulse<ShatterMesh3D*> on_shattered_pulse {};
    void apply_impact_impulse(float impulse_magnitude);
};

class RagdollChain3D : public cd::element_core::Spatial3D
{
public:
    explicit RagdollChain3D(std::string n = "RagdollChain3D") : Spatial3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "RagdollChain3D"; }
    float physical_blend_weight { 0.0F };
};

class SensorVolume3D : public cd::element_core::Spatial3D
{
public:
    explicit SensorVolume3D(std::string n = "SensorVolume3D") : Spatial3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "SensorVolume3D"; }
    std::array<float, 3> half_extents { 2.0F, 2.0F, 2.0F };
    cd::element_core::Pulse<cd::element_core::Spatial3D*> on_entered_pulse {};
    cd::element_core::Pulse<cd::element_core::Spatial3D*> on_exited_pulse {};
    [[nodiscard]] bool contains_point(std::array<float, 3> world_pt) const noexcept;
};

class CollisionHull3D : public cd::element_core::Spatial3D
{
public:
    enum class ShapeKind : std::uint8_t { kBox = 0, kSphere = 1, kCapsule = 2, kConvexHull = 3 };
    explicit CollisionHull3D(std::string n = "CollisionHull3D") : Spatial3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "CollisionHull3D"; }
    ShapeKind shape { ShapeKind::kBox };
    std::array<float, 3> dimensions { 1.0F, 1.0F, 1.0F };
};

class RayProbe3D : public cd::element_core::Spatial3D
{
public:
    explicit RayProbe3D(std::string n = "RayProbe3D") : Spatial3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "RayProbe3D"; }
    std::array<float, 3> direction { 0.0F, -1.0F, 0.0F };
    float max_distance { 10.0F };
};

}  // namespace cd::elements_dynamics

// =============================================================================
// 6. cd::elements_acoustics & cd::elements_wayfinding
// =============================================================================
namespace cd::elements_acoustics
{

struct BinauralFrame
{
    float left_gain { 1.0F };
    float right_gain { 1.0F };
    float itd_delay_seconds { 0.0F };
    float doppler_pitch_ratio { 1.0F };
};

class SoundSource3D : public cd::element_core::Spatial3D
{
public:
    explicit SoundSource3D(std::string n = "SoundSource3D") : Spatial3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "SoundSource3D"; }
    std::string audio_clip_path {};
    float volume_gain { 1.0F };
    float min_distance { 1.0F };
    float max_distance { 50.0F };
    bool enable_hrtf { true };
    bool enable_doppler { true };
    std::array<float, 3> velocity_mps { 0.0F, 0.0F, 0.0F };

    /// Evaluate MIT KEMAR Woodworth spherical-head ITD/ILD + 1/r attenuation +
    /// Doppler pitch shift relative to an ear position, right-axis, and velocity.
    [[nodiscard]] BinauralFrame evaluate_hrtf_and_doppler(
        std::array<float, 3> ear_pos,
        std::array<float, 3> ear_right_dir,
        std::array<float, 3> ear_vel_mps
    ) const noexcept;
};

class AudioEar3D : public cd::element_core::Spatial3D
{
public:
    explicit AudioEar3D(std::string n = "AudioEar3D") : Spatial3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "AudioEar3D"; }
    float master_gain { 1.0F };
};

class EchoChamber3D : public cd::element_core::Spatial3D
{
public:
    explicit EchoChamber3D(std::string n = "EchoChamber3D") : Spatial3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "EchoChamber3D"; }
    std::array<float, 3> half_extents { 6.0F, 3.0F, 6.0F };
    float reverb_decay_rt60_s { 1.8F };
    float wet_mix { 0.35F };
    float occlusion_lowpass_hz { 2400.0F };
};

}  // namespace cd::elements_acoustics

namespace cd::elements_wayfinding
{

class NavMeshZone3D : public cd::element_core::Spatial3D
{
public:
    explicit NavMeshZone3D(std::string n = "NavMeshZone3D") : Spatial3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "NavMeshZone3D"; }
    float agent_radius { 0.4F };
    float agent_max_slope_deg { 45.0F };
};

class WayfinderAgent3D : public cd::element_core::Spatial3D
{
public:
    explicit WayfinderAgent3D(std::string n = "WayfinderAgent3D") : Spatial3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "WayfinderAgent3D"; }
    float move_speed_mps { 4.5F };
    float avoidance_radius { 0.6F };
    std::array<float, 3> target_destination { 0.0F, 0.0F, 0.0F };
    void on_tick(float dt) override;
};

class SplineTrack3D : public cd::element_core::Spatial3D
{
public:
    explicit SplineTrack3D(std::string n = "SplineTrack3D") : Spatial3D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "SplineTrack3D"; }
    std::vector<std::array<float, 3>> control_points {};
    [[nodiscard]] std::array<float, 3> sample_catmull_rom(float u_normalized) const noexcept;
};

}  // namespace cd::elements_wayfinding

// =============================================================================
// 7. cd::elements_planar & cd::elements_ui
// =============================================================================
namespace cd::elements_planar
{

class QuadSprite2D : public cd::element_core::Planar2D
{
public:
    explicit QuadSprite2D(std::string n = "QuadSprite2D") : Planar2D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "QuadSprite2D"; }
    std::string texture_path {};
    std::array<float, 4> tint_rgba { 1.0F, 1.0F, 1.0F, 1.0F };
};

class Flipbook2D : public QuadSprite2D
{
public:
    explicit Flipbook2D(std::string n = "Flipbook2D") : QuadSprite2D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "Flipbook2D"; }
    std::uint32_t frame_count { 8 };
    float frames_per_second { 12.0F };
    std::uint32_t current_frame { 0 };
    void on_tick(float dt) override;
private:
    float accum_time_ { 0.0F };
};

class TileCanvas2D : public cd::element_core::Planar2D
{
public:
    explicit TileCanvas2D(std::string n = "TileCanvas2D") : Planar2D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "TileCanvas2D"; }
    std::uint32_t grid_cols { 32 };
    std::uint32_t grid_rows { 32 };
    std::vector<std::uint16_t> tile_ids {};
};

class Lens2D : public cd::element_core::Planar2D
{
public:
    explicit Lens2D(std::string n = "Lens2D") : Planar2D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "Lens2D"; }
    float zoom { 1.0F };
};

class KineticBody2D : public cd::element_core::Planar2D
{
public:
    explicit KineticBody2D(std::string n = "KineticBody2D") : Planar2D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "KineticBody2D"; }
    std::array<float, 2> velocity { 0.0F, 0.0F };
    void on_fixed_tick(float fixed_dt) override;
};

class SensorZone2D : public cd::element_core::Planar2D
{
public:
    explicit SensorZone2D(std::string n = "SensorZone2D") : Planar2D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "SensorZone2D"; }
    std::array<float, 2> half_extents { 16.0F, 16.0F };
};

class RadialLight2D : public cd::element_core::Planar2D
{
public:
    explicit RadialLight2D(std::string n = "RadialLight2D") : Planar2D(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "RadialLight2D"; }
    float radius_px { 128.0F };
    float intensity { 1.0F };
};

}  // namespace cd::elements_planar

namespace cd::elements_ui
{

class HudLayer : public cd::element_core::Element
{
public:
    explicit HudLayer(std::string n = "HudLayer") : Element(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "HudLayer"; }
    std::int32_t sort_order { 0 };
};

class UiFrame : public cd::element_core::Element
{
public:
    explicit UiFrame(std::string n = "UiFrame") : Element(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "UiFrame"; }
    std::array<float, 4> rect_xywh { 0.0F, 0.0F, 200.0F, 100.0F };
};

class UiCommandButton : public UiFrame
{
public:
    explicit UiCommandButton(std::string n = "UiCommandButton") : UiFrame(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "UiCommandButton"; }
    std::string label { "Button" };
    cd::element_core::Pulse<> on_pressed_pulse {};
};

class UiValueSlider : public UiFrame
{
public:
    explicit UiValueSlider(std::string n = "UiValueSlider") : UiFrame(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "UiValueSlider"; }
    float value { 0.5F };
    cd::element_core::Pulse<float> on_value_changed_pulse {};
};

class UiVectorBox : public UiFrame
{
public:
    explicit UiVectorBox(std::string n = "UiVectorBox") : UiFrame(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "UiVectorBox"; }
    std::array<float, 3> xyz { 0.0F, 0.0F, 0.0F };
};

class UiGlyphText : public UiFrame
{
public:
    explicit UiGlyphText(std::string n = "UiGlyphText") : UiFrame(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "UiGlyphText"; }
    std::string text {};
};

class UiGaugeBar : public UiFrame
{
public:
    explicit UiGaugeBar(std::string n = "UiGaugeBar") : UiFrame(std::move(n)) {}
    [[nodiscard]] std::string_view class_name() const noexcept override { return "UiGaugeBar"; }
    float current_value { 100.0F };
    float max_value { 100.0F };
};

}  // namespace cd::elements_ui
