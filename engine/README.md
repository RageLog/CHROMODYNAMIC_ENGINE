# CHROMODYNAMIC engine/

97 cd_<lib> libraries under one umbrella; together they make up the engine subsystem stack. Every library is independently consumable (per CLAUDE.md §7 library-oriented principle) and standalone-testable (every lib ships its own gtest binary under tests/).

## Tiers

The DAG is layered foundation -> math/memory -> concurrency/io -> rhi/asset -> ecs/scene -> engine -> editor. Cross-tier upcalls forbidden.

### Foundation (engine/foundation/)

cd::core, cd::math, cd::log, cd::time, cd::diag, cd::concurrency, cd::config, cd::events, cd::io, cd::mem, cd::platform, cd::plugin, cd::profile, cd::vfs, cd::frame_timing, cd::foundation_utils, cd::bench. 

### Asset (engine/asset*)

cd::asset (PrimitiveMesh + Primitives), cd::asset_gltf (glTF + skinning bridge), cd::asset_image (BMP/PNG decode), cd::asset_json (small JSON), cd::asset_ktx2 (KTX2 reader), cd::asset_obj (OBJ minimal), cd::asset_pak (pack-file), cd::asset_streaming (AsyncStreamer), cd::asset_wav (WAV), cd::asset_cdmesh (project mesh format), cd::asset_cdtex (project texture format).

### Render (engine/render/)

cd::rhi (interface + handles), cd::rhi_vulkan (Vulkan backend), cd::camera, cd::material (Lit + StandardPbr + Skinned + AnalyticalSky + BRDF LUT), cd::ibl + cd::ibl_gpu, cd::brdf_ltc + cd::brdf_sheen_clearcoat + cd::brdf_sss, cd::light + cd::shadow + cd::atmosphere, cd::framegraph, cd::shader, cd::render, cd::velocity, cd::async_submit, and 11 post-* libraries (bloom, camera-fx, composite, dof, gtao, motion-blur, smaa, ssr, taa, plus volumetric_clouds + volumetric_fog), plus ddgi / restir_di / restir_gi / nrc (GI), gpu_particles, decal, light_shafts.

### World (engine/world/)

cd::ecs (minimal Entity/World), cd::scene (Scene atop ECS), cd::anim (Skeleton + Pose + Animation + GpuSkinning), cd::audio (Mixer + Compressor + Reverb + LowPass + Limiter + WASAPI/CoreAudio/ALSA backends), cd::physics (BuiltinPhysicsWorld), cd::net (Throttle + SnapshotBuffer + Loopback + UDP), cd::input, cd::world_container, cd::script.

### Texture synthesis + image-diff (engine/texture_synth, engine/imgdiff)

Procedural Earth bakes (albedo/normal/metallic-rough); FLIP/SSIM image-diff for golden-image tests.

### UI (engine/ui/)

cd::ui (engine UI primitives), cd::editor (EditHistory, TransformCommands, SelectionOutline, AxisGizmo, CommandPalette), cd::editor_ui (panels), cd::imgui (Context bridge).

### Runtime + script (engine/runtime, engine/script)

cd::runtime (boot lifecycle), cd::script (early; Lua/Wren/JS pending).

## Build + test

Every lib builds standalone via its CMakeLists.txt; cross-lib deps go via PUBLIC / PRIVATE / INTERFACE links (CLAUDE.md §6). The top-level `cmake --build --preset ninja-debug` builds everything; `ctest --preset ninja-debug --output-on-failure` runs the 96 test binaries.

## Per-library docs

Each cd_<lib> ships with a README.md describing purpose / namespace / headers / primary types / usage example / test command. Run 11 backfilled the Tier-1 subset (cd::core, cd::math, cd::concurrency, cd::frame_timing, cd::log); Run 12 will complete the remaining ~92 READMEs.

