# cd::asset_gltf

**Purpose**: glTF 2.0 + .glb loader. Bridges Khronos sample assets into engine-native cd::asset::PrimitiveMesh + skin + animation + material data. Used by hello_engine for its CesiumMan / DamagedHelmet / FlightHelmet / Duck auto-load demo.

**Namespace**: `cd::asset_gltf`.

**Headers**: `cd/asset_gltf/{GltfLoader,SkinnedMeshBridge,AssetLoader}.hpp`.

**Primary types**:
- `cd::asset_gltf::GltfScene` -- top-level aggregate { meshes, animations, skins, materials, textures, scenes }.
- `cd::asset_gltf::load_gltf(path)` -- file-path loader (fastgltf-backed); returns Result<GltfScene>.
- `cd::asset_gltf::load_gltf_from_memory(bytes, size, base_dir)` -- in-memory variant for embedded assets.
- `cd::asset_gltf::to_skeleton_bundle(scene, skin_index)` -- bridges the chosen skin into a cd::anim::Skeleton + node-to-joint map + joint remap for CPU LBS skinning.

**Test command**: `ctest --preset ninja-debug -R cd_test_asset_gltf --output-on-failure`.

**Notes**:
- fastgltf is vendored under `Dependencies/` -- the loader is C++23-aware (designated init, std::expected).
- hello_engine extracts its auto-load + baseColor-swap chain into `cd_sample::try_auto_load_gltf` (samples/engine/hello_engine/HelloGltf.hpp).
- Skin + animation bridging is opt-in: caller picks the skin index via to_skeleton_bundle so multi-skin assets stay manageable.
