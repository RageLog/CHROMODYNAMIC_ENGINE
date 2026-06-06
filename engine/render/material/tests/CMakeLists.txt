cd_add_test(material
  SOURCES test_material.cpp
  DEPS    cd::material cd::rhi cd::shader cd::core $<$<BOOL:${CD_ENABLE_VULKAN}>:cd::rhi_vulkan>
)

# M3 W1B Sprint-1: UI variant factory + spec tests.
cd_add_test(ui_variant
  SOURCES test_ui_variant.cpp
  DEPS    cd::material cd::rhi cd::shader cd::core $<$<BOOL:${CD_ENABLE_VULKAN}>:cd::rhi_vulkan>
)

# Phase 639 / T1.9: MaterialInstance metallic/roughness CPU accessors
# contract — see engine/render/material/README.md "G-buffer MRT channel
# contract" + docs/AUDIT/learned-lessons-curtain-reflection-2026-06-03.md.
cd_add_test(material_instance_mr_contract
  SOURCES test_material_instance_mr_contract.cpp
  DEPS    cd::material cd::rhi cd::core
)

# Phase 646 / T1.16: MaterialInstance alpha-mode predicate accessors
# (is_blend / is_mask / is_opaque) — prerequisite for T1.15 two-pass alpha
# render order. Downstream call sites branch with is_blend() / is_mask()
# instead of comparing the AlphaMode enum directly.
cd_add_test(material_instance_alpha_predicates
  SOURCES test_material_instance_alpha_predicates.cpp
  DEPS    cd::material cd::rhi cd::core
)

# Phase 657 / T1.12: cd::material::ray_hit_sample stub helper — the stable
# CPU-side contract the future RT closest-hit shader (or shader-record
# builder) will read so a chrome PBR sphere's reflection of a general-
# geometry hit (Sponza wall, curtain, vegetation) shows the prim's albedo
# instead of black. Two cases: synthetic glTF prim returns non-zero
# colour; inert MaterialInstance returns the neutral-grey fallback
# (preserves the M0 chrome reflection baseline so the user always sees
# SOMETHING in the chrome). See engine/render/material/README.md
# "RT closest-hit sample contract" + docs/AUDIT/learned-lessons-pbr-rt-
# and-curtain-alpha-2026-06-03.md (Bug A).
cd_add_test(rt_hit_general_geometry
  SOURCES test_rt_hit_general_geometry.cpp
  DEPS    cd::material cd::rhi cd::core
)

# Phase 759 / FINALE-5 W1 / A8: RT closest-hit shader live wire — chrome PBR
# sphere reflects ACTUAL surrounding geometry (Sponza walls + vegetation +
# curtains) instead of pure sky. Three cases:
#   1. Packer (CPU): MaterialInstance -> std430 RtMaterialRecord byte
#      layout matches the GLSL SSBO declared in kRtClosestHitGlsl.
#   2. Vulkan-gated (skipped without ICD or glslang): kRtClosestHitGlsl
#      compiles cleanly through glslang to SPIR-V; protects against
#      regression in the live closest-hit shader.
#   3. Vulkan-gated: 2-prim scene (chrome sphere + colored quad behind)
#      packed records upload cleanly into a Vulkan storage buffer; the
#      magenta quad albedo survives the round-trip end-to-end (so the
#      chrome reflection would not show pure sky -- the 2026-06-03 bug).
cd_add_test(rt_chrome_sphere_reflects_geometry
  SOURCES test_rt_chrome_sphere_reflects_geometry.cpp
  DEPS    cd::material cd::rhi cd::shader cd::core
          $<$<BOOL:${CD_ENABLE_VULKAN}>:cd::rhi_vulkan>
)
