// =============================================================================
// CHROMODYNAMIC — apps/editor/tests/test_splash_audio_cue.cpp
// Phase 775 / FINALE-8 W1B H2 — Synthetic tests for the SplashAudioCue.
//
// All tests use the null audio backend (make_null_audio_backend) so they
// run correctly in headless CI without a real audio device.
//
// Test suite: SplashAudioCue
//
//   Synthesize_NonZeroAtExpectedTiming
//     The synthesized PCM vector has kTotalSamples entries and the samples
//     in the sustain region (expected peak amplitude) are non-zero.
//
//   Synthesize_AttackSamplesRampUp
//     The envelope during the attack region ([0, kAttackEnd)) rises
//     monotonically from near-zero toward kPeakAmplitude.
//
//   Synthesize_ReleaseSamplesDecayToZero
//     The last sample in the clip is near zero (release has decayed away).
//
//   TriggerBeforeThresholdDoesNotFire
//     trigger_if_needed() returns false and fired() stays false when
//     elapsed_ms < kChimeTriggerMs.
//
//   TriggerAtThresholdFires
//     trigger_if_needed() returns true on the first call at or after
//     kChimeTriggerMs and creates a clip + voice on the null backend.
//
//   TriggerIsIdempotentAfterFirstFire
//     Subsequent calls to trigger_if_needed() return false even when
//     elapsed_ms > kChimeTriggerMs.
//
//   TriggerWithNullBackendSetsFireButNotChimed
//     When backend == nullptr the cue sets fired() == true but
//     chimed() == false (graceful skip).
// =============================================================================

#include "../SplashAudioCue.hpp"
#include "../StudioCustomMeshLoader.hpp"
#include "../StudioNodeCatalog.hpp"
#include <cd/audio/IAudioBackend.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>

namespace
{

// ---------------------------------------------------------------------------
// Synthesize_NonZeroAtExpectedTiming
// ---------------------------------------------------------------------------
TEST(SplashAudioCue, Synthesize_NonZeroAtExpectedTiming)
{
    using Cue = cd::editor::SplashAudioCue;

    const std::vector<float> pcm = Cue::synthesize();

    ASSERT_EQ(pcm.size(), Cue::kTotalSamples);

    // Sustain region is [kDecayEnd, kSustainEnd).  The sine oscillates so
    // some individual samples may pass through zero; check that at least
    // one sample in the sustain band is non-zero with meaningful amplitude.
    float sustain_peak = 0.0F;
    for (std::size_t i = Cue::kDecayEnd; i < Cue::kSustainEnd; ++i)
        sustain_peak = std::max(sustain_peak, std::fabs(pcm[i]));

    EXPECT_GT(sustain_peak, 0.0F)
        << "sustain region should contain non-zero amplitude samples";

    // The sustain amplitude should be close to kPeakAmplitude * kSustainLevel.
    const float expected_peak = Cue::kPeakAmplitude * Cue::kSustainLevel;
    EXPECT_NEAR(sustain_peak, expected_peak, expected_peak * 0.05F)
        << "sustain peak should be within 5% of kPeakAmplitude * kSustainLevel";
}

// ---------------------------------------------------------------------------
// Synthesize_AttackSamplesRampUp
// ---------------------------------------------------------------------------
TEST(SplashAudioCue, Synthesize_AttackSamplesRampUp)
{
    using Cue = cd::editor::SplashAudioCue;
    const std::vector<float> pcm = Cue::synthesize();

    // Compare absolute amplitudes at the very start vs. near the end of attack.
    // The sine oscillation means we cannot compare consecutive samples; compare
    // the max absolute value in the first tenth of the attack against the last
    // tenth — it should be larger at the end.
    const std::size_t tenth = Cue::kAttackEnd / 10U;

    float early_max = 0.0F;
    for (std::size_t i = 0; i < tenth; ++i)
        early_max = std::max(early_max, std::fabs(pcm[i]));

    float late_max = 0.0F;
    for (std::size_t i = Cue::kAttackEnd - tenth; i < Cue::kAttackEnd; ++i)
        late_max = std::max(late_max, std::fabs(pcm[i]));

    EXPECT_GT(late_max, early_max)
        << "amplitude envelope should be larger near end of attack than at start";
}

// ---------------------------------------------------------------------------
// Synthesize_ReleaseSamplesDecayToZero
// ---------------------------------------------------------------------------
TEST(SplashAudioCue, Synthesize_ReleaseSamplesDecayToZero)
{
    using Cue = cd::editor::SplashAudioCue;
    const std::vector<float> pcm = Cue::synthesize();

    // The very last sample should be near zero (release fully decayed).
    const float last = std::fabs(pcm.back());
    // The release factor at the final sample index (kTotalSamples - 1):
    //   t = (kTotalSamples - 1 - kSustainEnd) / (kTotalSamples - kSustainEnd)
    // which approaches 1 as i -> kTotalSamples. The amplitude is
    //   kPeakAmplitude * kSustainLevel * (1 - t), approaching 0.
    // Allow a tolerance of one full peak level (sine may land at any phase).
    EXPECT_LT(last, Cue::kPeakAmplitude * Cue::kSustainLevel * 0.1F + 1e-6F)
        << "last sample should be near zero (release fully decayed)";
}

// ---------------------------------------------------------------------------
// TriggerBeforeThresholdDoesNotFire
// ---------------------------------------------------------------------------
TEST(SplashAudioCue, TriggerBeforeThresholdDoesNotFire)
{
    using Cue = cd::editor::SplashAudioCue;

    auto backend = cd::audio::make_null_audio_backend();
    Cue cue {};

    const bool fired = cue.trigger_if_needed(
        Cue::kChimeTriggerMs - 1.0, backend.get());

    EXPECT_FALSE(fired)    << "should not fire before kChimeTriggerMs";
    EXPECT_FALSE(cue.fired()) << "fired() should stay false";
    EXPECT_FALSE(cue.chimed());
    EXPECT_EQ(backend->clip_count(), 0U)
        << "no clip should be created before threshold";
}

// ---------------------------------------------------------------------------
// TriggerAtThresholdFires
// ---------------------------------------------------------------------------
TEST(SplashAudioCue, TriggerAtThresholdFires)
{
    using Cue = cd::editor::SplashAudioCue;

    auto backend = cd::audio::make_null_audio_backend();
    Cue cue {};

    const bool fired = cue.trigger_if_needed(
        Cue::kChimeTriggerMs, backend.get());

    EXPECT_TRUE(fired)     << "should return true on first trigger at threshold";
    EXPECT_TRUE(cue.fired())  << "fired() should be true";
    EXPECT_TRUE(cue.chimed()) << "chimed() should be true (null backend accepts clip+play)";

    // Null backend stores the clip and voice.
    EXPECT_EQ(backend->clip_count(), 1U)
        << "one clip should be created on trigger";
    EXPECT_EQ(backend->voice_count(), 1U)
        << "one voice should be playing";
}

// ---------------------------------------------------------------------------
// TriggerIsIdempotentAfterFirstFire
// ---------------------------------------------------------------------------
TEST(SplashAudioCue, TriggerIsIdempotentAfterFirstFire)
{
    using Cue = cd::editor::SplashAudioCue;

    auto backend = cd::audio::make_null_audio_backend();
    Cue cue {};

    // First call fires.
    cue.trigger_if_needed(Cue::kChimeTriggerMs, backend.get());
    ASSERT_TRUE(cue.fired());

    // Subsequent calls should be no-ops.
    const bool fired2 = cue.trigger_if_needed(
        Cue::kChimeTriggerMs + 100.0, backend.get());
    const bool fired3 = cue.trigger_if_needed(
        Cue::kChimeTriggerMs + 200.0, backend.get());

    EXPECT_FALSE(fired2) << "second call should return false (idempotent)";
    EXPECT_FALSE(fired3) << "third call should return false (idempotent)";

    // No additional clips or voices should be created.
    EXPECT_EQ(backend->clip_count(), 1U)
        << "only one clip should exist after repeated triggers";
    EXPECT_EQ(backend->voice_count(), 1U)
        << "only one voice should exist after repeated triggers";
}

// ---------------------------------------------------------------------------
// TriggerWithNullBackendSetsFireButNotChimed
// ---------------------------------------------------------------------------
TEST(SplashAudioCue, TriggerWithNullBackendSetsFireButNotChimed)
{
    using Cue = cd::editor::SplashAudioCue;

    Cue cue {};

    // Pass nullptr — device unavailable path.
    const bool fired = cue.trigger_if_needed(Cue::kChimeTriggerMs, nullptr);

    EXPECT_FALSE(fired)    << "returns false when backend is null";
    EXPECT_TRUE(cue.fired())  << "fired() should be true (attempted)";
    EXPECT_FALSE(cue.chimed()) << "chimed() should stay false (no backend)";
}

TEST(StudioCustomMeshLoader, ParsesValidObjWithQuadsAndNormalizesBounds)
{
    const auto tmp_path = (std::filesystem::temp_directory_path() / "cd_test_custom_mesh.obj").string();
    {
        std::ofstream out(tmp_path);
        out << "# Test Quad at large offset\n"
            << "v 100.0 100.0 50.0\n"
            << "v 120.0 100.0 50.0\n"
            << "v 120.0 120.0 50.0\n"
            << "v 100.0 120.0 50.0\n"
            << "f 1 2 3 4\n";
    }
    cd::asset::PrimitiveMesh mesh {};
    ASSERT_TRUE(cd::studio::parse_wavefront_obj_mesh(tmp_path, mesh));
    EXPECT_EQ(mesh.vertices.size(), 6U);
    ASSERT_TRUE(cd::studio::normalize_and_validate_custom_mesh(mesh));
    for (const auto& v : mesh.vertices)
    {
        EXPECT_LE(std::fabs(v.pos[0]), 0.65F);
        EXPECT_LE(std::fabs(v.pos[1]), 0.65F);
        EXPECT_NEAR(v.normal[2], 1.0F, 1e-4F);
    }
    std::error_code ec {};
    std::filesystem::remove(tmp_path, ec);
}

TEST(StudioCustomMeshLoader, RejectsEmptyOrMalformedObjFilesGracefully)
{
    const auto empty_path = (std::filesystem::temp_directory_path() / "cd_test_empty.obj").string();
    {
        std::ofstream out(empty_path);
    }
    cd::asset::PrimitiveMesh mesh {};
    EXPECT_FALSE(cd::studio::parse_wavefront_obj_mesh(empty_path, mesh));
    EXPECT_FALSE(cd::studio::parse_wavefront_obj_mesh("non_existent_path_xyz_404.obj", mesh));
    EXPECT_FALSE(cd::studio::normalize_and_validate_custom_mesh(mesh));
    std::error_code ec {};
    std::filesystem::remove(empty_path, ec);
}

TEST(StudioNodeCatalog, HasExact219GodotAnd31SotaMaking250Total)
{
    using namespace cd::studio;
    EXPECT_EQ(kStudioGodotVideoNodeCount, 219U);
    EXPECT_EQ(kStudioNextGenSotaNodeCount, 31U);
    EXPECT_EQ(kStudioTotalCatalogCount, 250U);
    EXPECT_EQ(kStudioUniversalNodeCatalog.size(), 250U);

    EXPECT_EQ(count_studio_nodes_in_category(StudioNodeCategory::CoreSystem), 21U);
    EXPECT_EQ(count_studio_nodes_in_category(StudioNodeCategory::Node2D), 50U);
    EXPECT_EQ(count_studio_nodes_in_category(StudioNodeCategory::Node3D), 88U);
    EXPECT_EQ(count_studio_nodes_in_category(StudioNodeCategory::ControlUI), 60U);
    EXPECT_EQ(count_studio_nodes_in_category(StudioNodeCategory::NextGenSOTA), 31U);

    for (std::size_t i = 0; i < kStudioUniversalNodeCatalog.size(); ++i)
    {
        const auto& d = kStudioUniversalNodeCatalog[i];
        EXPECT_EQ(d.id, static_cast<std::uint16_t>(i));
        ASSERT_NE(d.godot_name, nullptr);
        ASSERT_NE(d.chroma_name, nullptr);
        ASSERT_NE(d.video_timestamp, nullptr);
        ASSERT_NE(d.ux_upgrade, nullptr);
        ASSERT_NE(d.tech_upgrade, nullptr);
        EXPECT_FALSE(std::string_view(d.godot_name).empty()) << "Empty godot_name at id " << i;
        EXPECT_FALSE(std::string_view(d.chroma_name).empty()) << "Empty chroma_name at id " << i;
        EXPECT_FALSE(std::string_view(d.video_timestamp).empty()) << "Empty video_timestamp at id " << i;
        EXPECT_FALSE(std::string_view(d.ux_upgrade).empty()) << "Empty ux_upgrade at id " << i;
        EXPECT_FALSE(std::string_view(d.tech_upgrade).empty()) << "Empty tech_upgrade at id " << i;
        EXPECT_STREQ(d.godot_equivalent(), d.godot_name);
    }
}

TEST(StudioNodeCatalog, FindsAllIconicGodot219AndNextGenSota31Nodes)
{
    using namespace cd::studio;
    constexpr std::string_view kRequiredLookups[] {
        "Node", "Viewport", "SubViewportContainer", "WorldEnvironment", "AnimationPlayer",
        "AnimationTree", "AudioStreamPlayer", "Timer", "HTTPRequest", "MultiplayerSpawner",
        "MultiplayerSynchronizer", "Node2D", "Sprite2D", "CharacterBody2D", "RigidBody2D",
        "TileMap", "Camera2D", "CanvasGroup", "BackBufferCopy", "ParallaxBackground",
        "Node3D", "MeshInstance3D", "CharacterBody3D", "RigidBody3D", "StaticBody3D",
        "Area3D", "VehicleBody3D", "SoftBody3D", "SpringArm3D", "OmniLight3D",
        "SpotLight3D", "DirectionalLight3D", "VoxelGI", "LightmapGI", "ReflectionProbe",
        "Decal", "FogVolume", "GPUParticles3D", "GPUParticlesAttractor3D", "CSGBox3D",
        "CSGCombiner3D", "Skeleton3D", "SkeletonIK3D", "NavigationRegion3D", "NavigationAgent3D",
        "GridMap", "XROrigin3D", "PhysicalBone3D", "RayCast3D", "ShapeCast3D",
        "Control", "Button", "GraphEdit", "GraphNode", "RichTextLabel",
        "SubViewport", "Tree", "ItemList", "TabContainer", "SplitContainer",
        "ColorPicker", "CodeEdit"
    };

    for (const auto name : kRequiredLookups)
    {
        const auto* desc = find_studio_node(name);
        ASSERT_NE(desc, nullptr) << "Failed to find required Godot node: " << name;
        EXPECT_FALSE(std::string_view(desc->chroma_name).empty());
    }

    // Verify all 219 Godot nodes (IDs 0..218) and 31 Next-Gen SOTA nodes (IDs 219..249) are searchable
    for (std::uint16_t id = 0; id < 250; ++id)
    {
        const auto* desc = find_studio_node_by_id(id);
        ASSERT_NE(desc, nullptr);
        EXPECT_EQ(find_studio_node_by_chroma_name(desc->chroma_name), desc);
        EXPECT_NE(find_studio_node(desc->godot_name), nullptr);
        if (id >= 219)
            EXPECT_EQ(desc->category, StudioNodeCategory::NextGenSOTA);
    }

    // Edge cases
    EXPECT_EQ(find_studio_node_by_id(250), nullptr);
    EXPECT_EQ(find_studio_node_by_id(65535), nullptr);
    EXPECT_EQ(find_studio_node(""), nullptr);
    EXPECT_EQ(find_studio_node("__non_existent_node_xyz__"), nullptr);
    EXPECT_EQ(filter_studio_nodes(-1, "").size(), 250U);
    EXPECT_EQ(filter_studio_nodes(0, "").size(), 21U);
    EXPECT_EQ(filter_studio_nodes(1, "").size(), 50U);
    EXPECT_EQ(filter_studio_nodes(2, "").size(), 88U);
    EXPECT_EQ(filter_studio_nodes(3, "").size(), 60U);
    EXPECT_EQ(filter_studio_nodes(4, "").size(), 31U);
    EXPECT_TRUE(filter_studio_nodes(-1, "__non_existent_node_xyz__").empty());
}

TEST(StudioNodeCatalog, SpawnsNestsFoldsAndSurvivesJsonRoundTripForAll250Nodes)
{
    using namespace cd::studio;
    StudioState state {};

    // 1. Spawn all 250 nodes as Root Elements/Vessels
    for (std::uint16_t id = 0; id < 250; ++id)
    {
        ASSERT_TRUE(apply_catalog_node_to_studio_state(
            state, id, CatalogComposeMode::SpawnRootElement));
        ASSERT_EQ(state.selected_idx, static_cast<int>(id));
        const auto& ent = state.entities[static_cast<std::size_t>(state.selected_idx)];
        EXPECT_EQ(ent.catalog_node_id, id);
        EXPECT_EQ(ent.parent_entity_idx, -1);
        ASSERT_EQ(ent.folded_capsule_ids.size(), 1U);
        EXPECT_EQ(ent.folded_capsule_ids[0], id);
    }
    EXPECT_EQ(state.entities.size(), 250U);

    // 2. Select CharacterBody3D (#117) -> nest SpringArm3D (#120) -> nest Camera3D (#119) as grandchild
    state.selected_idx = 117;
    ASSERT_TRUE(apply_catalog_node_to_studio_state(
        state, 120, CatalogComposeMode::NestAsChildElement));
    ASSERT_EQ(state.selected_idx, 250);
    EXPECT_EQ(state.entities[250].parent_entity_idx, 117);
    EXPECT_EQ(state.entities[250].godot_name, "SpringArm3D");
    EXPECT_EQ(studio_state_entity_depth(state, 117), 0U);
    EXPECT_EQ(studio_state_entity_depth(state, 250), 1U);

    ASSERT_TRUE(apply_catalog_node_to_studio_state(
        state, 119, CatalogComposeMode::NestAsChildElement));
    ASSERT_EQ(state.selected_idx, 251);
    EXPECT_EQ(state.entities[251].parent_entity_idx, 250);
    EXPECT_EQ(studio_state_entity_depth(state, 251), 2U);

    // 3. Fold Camera3D (#119), UtilityGOAPBrainAI (#234), and ThinFilmIridescenceCoat3D (#223) onto #117
    state.selected_idx = 117;
    EXPECT_TRUE(apply_catalog_node_to_studio_state(state, 119, CatalogComposeMode::FoldAsInlineCapsule));
    EXPECT_TRUE(apply_catalog_node_to_studio_state(state, 234, CatalogComposeMode::FoldAsInlineCapsule));
    EXPECT_TRUE(apply_catalog_node_to_studio_state(state, 223, CatalogComposeMode::FoldAsInlineCapsule));
    // Duplicate fold of #234 should be idempotent
    EXPECT_TRUE(apply_catalog_node_to_studio_state(state, 234, CatalogComposeMode::FoldAsInlineCapsule));
    EXPECT_EQ(state.entities[117].folded_capsule_ids.size(), 4U);
    EXPECT_TRUE(has_folded_capsule(state.entities[117], 117));
    EXPECT_TRUE(has_folded_capsule(state.entities[117], 119));
    EXPECT_TRUE(has_folded_capsule(state.entities[117], 234));
    EXPECT_TRUE(has_folded_capsule(state.entities[117], 223));
    EXPECT_GT(state.entities[117].iridescence_nm, 0.0F);

    // 4. Out-of-range node ID returns false safely
    EXPECT_FALSE(apply_catalog_node_to_studio_state(state, 999, CatalogComposeMode::SpawnRootElement));

    // 5. JSON scene save/load round-trip preserves all 252 entities, parent-child links, and folded capsules
    const std::string json_blob = serialize_studio_state_json(state, false);
    EXPECT_FALSE(json_blob.empty());

    StudioState restored {};
    ASSERT_TRUE(deserialize_studio_state_json(json_blob, restored));
    ASSERT_EQ(restored.entities.size(), state.entities.size());
    EXPECT_EQ(restored.selected_idx, state.selected_idx);
    EXPECT_EQ(restored.active_env_flags, state.active_env_flags);
    EXPECT_EQ(restored.spawned_lights, state.spawned_lights);
    EXPECT_EQ(restored.planar_2d_count, state.planar_2d_count);
    EXPECT_EQ(restored.chroma_ui_count, state.chroma_ui_count);
    EXPECT_EQ(restored.sota_compute_count, state.sota_compute_count);

    EXPECT_EQ(restored.entities[117].godot_name, "CharacterBody3D");
    EXPECT_EQ(restored.entities[117].chroma_name, "AvatarVessel3D");
    EXPECT_EQ(restored.entities[117].folded_capsule_ids.size(), 4U);
    EXPECT_TRUE(has_folded_capsule(restored.entities[117], 234));
    EXPECT_TRUE(has_folded_capsule(restored.entities[117], 223));
    EXPECT_EQ(restored.entities[250].parent_entity_idx, 117);
    EXPECT_EQ(restored.entities[251].parent_entity_idx, 250);
    EXPECT_EQ(studio_state_entity_depth(restored, 251), 2U);
}

}  // namespace

