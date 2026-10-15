// =============================================================================
// CHROMODYNAMIC — cd::elements_domain unit tests (FAZ 5 Wave 5.2)
// =============================================================================
#include <cd/elements_domain/DomainElements.hpp>

#include <gtest/gtest.h>

namespace
{

TEST(ElementsDomainWave52, VisualRadianceAetherAndOpticsReflection)
{
    cd::element_core::Stage stage("AtmosphereShowcase");
    auto& dome = stage.root().emplace_child<cd::elements_aether::CelestialDome3D>("SkyDome");
    auto& fog  = stage.root().emplace_child<cd::elements_aether::AetherFog3D>("MainFroxelFog");
    auto& sun  = stage.root().emplace_child<cd::elements_radiance::SolarLight3D>("Sun");
    auto& opt  = stage.root().emplace_child<cd::elements_optics::OpticsVolume3D>("GlobalOptics");
    auto& arm  = stage.root().emplace_child<cd::elements_visual::SpringArm3D>("BoomArm");

    EXPECT_EQ(dome.class_name(), "CelestialDome3D");
    EXPECT_EQ(fog.class_name(), "AetherFog3D");
    EXPECT_EQ(sun.class_name(), "SolarLight3D");
    EXPECT_EQ(opt.class_name(), "OpticsVolume3D");
    EXPECT_FLOAT_EQ(arm.compute_clipped_arm_length(2.0F), 1.75F);

    std::vector<cd::element_core::PropertyDesc> props;
    fog.reflect_properties(props);
    EXPECT_GE(props.size(), 4U);
}

TEST(ElementsDomainWave52, DynamicsAcousticsWayfindingAndPlanar2D)
{
    cd::elements_dynamics::ShatterMesh3D crate("VoronoiCrate");
    bool shattered = false;
    crate.on_shattered_pulse.connect([&](cd::elements_dynamics::ShatterMesh3D*) {
        shattered = true;
    });
    crate.apply_impact_impulse(20.0F);
    EXPECT_FALSE(shattered);
    crate.apply_impact_impulse(80.0F);
    EXPECT_TRUE(shattered);

    cd::elements_acoustics::SoundSource3D siren("Siren3D");
    siren.set_local_position(10.0F, 0.0F, 0.0F);
    siren.velocity_mps = { -30.0F, 0.0F, 0.0F };  // approaching ear at origin
    const auto hrtf = siren.evaluate_hrtf_and_doppler(
        { 0.0F, 0.0F, 0.0F }, { 1.0F, 0.0F, 0.0F }, { 0.0F, 0.0F, 0.0F });
    EXPECT_GT(hrtf.right_gain, hrtf.left_gain);
    EXPECT_GT(hrtf.itd_delay_seconds, 0.0F);
    EXPECT_GT(hrtf.doppler_pitch_ratio, 1.0F);

    cd::elements_wayfinding::WayfinderAgent3D agent("Scout");
    agent.target_destination = { 9.0F, 0.0F, 0.0F };
    agent.move_speed_mps = 3.0F;
    agent.on_tick(1.0F);
    EXPECT_NEAR(agent.local_position()[0], 3.0F, 1e-4F);

    cd::elements_planar::Flipbook2D sprite("HeroRun2D");
    sprite.frame_count = 4U;
    sprite.frames_per_second = 10.0F;
    sprite.on_tick(0.25F);
    EXPECT_EQ(sprite.current_frame, 2U);
}

}  // namespace
