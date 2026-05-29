# cd::editor_panel

## Purpose
Composite post-processing fx preset library: headless data (presets, bindings) + optional ImGui widget helpers for editor panels. Decouples preset definitions from UI framework, enabling use in both headless servers and interactive editors.

## Namespace
`cd::<ui>::editor_panel::`

## Public headers
- `include/cd/editor_panel/CompositePresets.hpp` — Preset data (bloom, TAA, tonemapper settings)
- `include/cd/editor_panel/CompositeFxBinding.hpp` — Binding layer (preset ↔ RHI parameter)
- `include/cd/editor_panel/CompositePresetButtonsImGui.hpp` — ImGui button + dropdown (optional)

## Primary types
- `EditorPanel::CompositePreset` — Named bundle of post-fx parameters (struct of floats/enums)
- `EditorPanel::FxBinding` — Converter from preset to GPU push constants
- `EditorPanel::PresetLoader` — File I/O for preset persistence

## Usage example
```cpp
#include <cd/editor_panel/CompositePresets.hpp>
#include <cd/editor_panel/CompositeFxBinding.hpp>

// Load preset from disk.
auto preset = cd::editor_panel::load_composite_preset("cinematic.preset");

// Apply to render pass.
cd::editor_panel::FxBinding binding;
auto gpu_params = binding.to_push_constants(preset);
framegraph.set_post_fx_params(gpu_params);

// Optional: ImGui selector (only if consumer links cd::imgui_backend).
// #include <cd/editor_panel/CompositePresetButtonsImGui.hpp>
// if (cd::editor_panel::imgui_preset_selector("##fx", current_preset)) {
//   apply_preset(current_preset);
// }
```

## Build/Test
```bash
cmake --build --preset ninja-debug --target cd_editor_panel
ctest --preset ninja-debug -R editor_panel
```

## Dependencies
- `cd::core` — engine types
- (Optional) `cd::imgui_backend` — only for ImGui button headers

## References
- Preset management: Unreal Material Instance Editor
- Data-driven configuration: ImGui's ini file pattern

## Notes
- Header-only INTERFACE library.
- Headless preset definitions can be used in servers, CLI tools, or batch processing.
- ImGui integrations marked as optional so non-ImGui consumers don't pull the dependency.
- Preset format: plain text or JSON (serialized via reflection).
