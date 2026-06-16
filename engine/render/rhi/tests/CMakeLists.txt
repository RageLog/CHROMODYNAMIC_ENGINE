cd_add_test(rhi
  SOURCES test_rhi.cpp
  DEPS    cd::rhi cd::core
)

# Marathon Run 26 X6 slice: lock the RT descriptor ABI surface.
cd_add_test(rt_descriptors
  SOURCES test_rt_descriptors.cpp
  DEPS    cd::rhi cd::core
)

# Marathon Run 28 X4 slice: lock the Vulkan + D3D12 parity surface.
cd_add_test(backend_parity
  SOURCES test_backend_parity.cpp
  DEPS    cd::rhi cd::core
)

# phase377-B-infra2: IDevice::copy_image_to_buffer smoke tests (NullDevice).
# phase1127 (X4-B): + GPU clear-colour round-trip against the REAL Vulkan
# and (on Windows) D3D12 backends — each skips when no device is present.
if(WIN32)
  cd_add_test(image_readback
    SOURCES test_image_readback.cpp
    DEPS    cd::rhi_vulkan cd::rhi_d3d12 cd::rhi cd::core
  )
else()
  cd_add_test(image_readback
    SOURCES test_image_readback.cpp
    DEPS    cd::rhi_vulkan cd::rhi cd::core
  )
endif()

# phase400-M4H: D3D12 M4A-M4F surface contract parity test (CPU-only;
# no device instantiation — runnable in every CI lane).
cd_add_test(d3d12_parity_m4
  SOURCES test_d3d12_parity_m4.cpp
  DEPS    cd::rhi cd::core
)

# Backend-to-100 Wave 3a / A-INDIRECT: indirect draw/dispatch. Null reference
# records the no-op overrides; Vulkan (lavapipe/RTX) + D3D12 (WARP) record a
# real vkCmdDrawIndirect / ExecuteIndirect + vkCmdDispatchIndirect against a
# kIndirect buffer and submit (submit-clean + fence-signal assertion). SKIPs
# cleanly when no device is present.
# Backend-to-100 Wave 3a / A-QUERY: GPU timestamp query round-trip. Null returns
# kNotImplemented; Vulkan + D3D12 create a kTimestamp pool(2), bracket a copy,
# read back, and assert t1 >= t0. SKIPs when timestamp_queries is unavailable.
if(WIN32)
  cd_add_test(rhi_indirect
    SOURCES test_rhi_indirect.cpp
    DEPS    cd::rhi_vulkan cd::rhi_d3d12 cd::shader cd::rhi cd::core
  )
  cd_add_test(rhi_query
    SOURCES test_rhi_query.cpp
    DEPS    cd::rhi_vulkan cd::rhi_d3d12 cd::rhi cd::core
  )
  # Backend-to-100 Wave 3b / A-AS-FLAGS + A-COMPACTION + A-REFIT. FLAGS slice is
  # device-free; COMPACTION + REFIT build a BLAS/TLAS and inline-ray-query it
  # (GLSL on Vulkan, HLSL SM6.5 via compile_hlsl on D3D12). Needs the Vulkan +
  # D3D12 backends + cd::shader (glslang) + the DXC runtime DLLs.
  cd_add_test(rhi_as_flags_compaction_refit
    SOURCES test_rhi_as_flags_compaction_refit.cpp
    DEPS    cd::rhi_vulkan cd::rhi_d3d12 cd::shader cd::rhi cd::core
  )
  # rhi_indirect + rhi_as_flags_compaction_refit drive the DXC SM6.x path on
  # D3D12, so they need the same dxcompiler.dll/dxil.dll runtime.
  file(GLOB _cd_dxc_ind_list "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/dxcompiler.dll")
  list(SORT _cd_dxc_ind_list)
  if(_cd_dxc_ind_list AND TARGET cd_test_rhi_indirect)
    list(POP_BACK _cd_dxc_ind_list _cd_dxc_ind_dll)
    get_filename_component(_cd_dxc_ind_dir "${_cd_dxc_ind_dll}" DIRECTORY)
    add_custom_command(TARGET cd_test_rhi_indirect POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_cd_dxc_ind_dir}/dxcompiler.dll"
              "$<TARGET_FILE_DIR:cd_test_rhi_indirect>"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_cd_dxc_ind_dir}/dxil.dll"
              "$<TARGET_FILE_DIR:cd_test_rhi_indirect>")
  endif()
  if(_cd_dxc_ind_list AND TARGET cd_test_rhi_as_flags_compaction_refit)
    get_filename_component(_cd_dxc_asf_dir "${_cd_dxc_ind_dll}" DIRECTORY)
    add_custom_command(TARGET cd_test_rhi_as_flags_compaction_refit POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_cd_dxc_asf_dir}/dxcompiler.dll"
              "$<TARGET_FILE_DIR:cd_test_rhi_as_flags_compaction_refit>"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_cd_dxc_asf_dir}/dxil.dll"
              "$<TARGET_FILE_DIR:cd_test_rhi_as_flags_compaction_refit>")
  endif()
else()
  cd_add_test(rhi_indirect
    SOURCES test_rhi_indirect.cpp
    DEPS    cd::rhi_vulkan cd::shader cd::rhi cd::core
  )
  cd_add_test(rhi_query
    SOURCES test_rhi_query.cpp
    DEPS    cd::rhi_vulkan cd::rhi cd::core
  )
  cd_add_test(rhi_as_flags_compaction_refit
    SOURCES test_rhi_as_flags_compaction_refit.cpp
    DEPS    cd::rhi_vulkan cd::shader cd::rhi cd::core
  )
endif()

# Backend-to-100 Wave 3 / V-PIPECACHE: cross-backend device-level pipeline cache.
# Builds a real graphics + compute PSO on device A (GLSL -> SPIR-V on Vulkan /
# DXIL on D3D12), serialises the backend pipeline cache (VkPipelineCache /
# ID3D12PipelineLibrary) -> asserts the blob is non-empty + (Vulkan) carries a
# valid VkPipelineCacheHeader, then seeds device B with the blob and rebuilds the
# SAME pipelines -> asserts success. Null returns empty-blob + kNotImplemented.
# Needs the Vulkan + (Windows) D3D12 backends + cd::shader (glslang); D3D12 drives
# the DXC SM6.x path so it needs the dxcompiler.dll/dxil.dll runtime. SKIPs
# cleanly when no device / no glslang / no DXC DLL / no pipeline-library support.
if(WIN32)
  cd_add_test(rhi_pipeline_cache
    SOURCES test_rhi_pipeline_cache.cpp
    DEPS    cd::rhi_vulkan cd::rhi_d3d12 cd::shader cd::rhi cd::core
  )
  file(GLOB _cd_dxc_pc_list "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/dxcompiler.dll")
  list(SORT _cd_dxc_pc_list)
  if(_cd_dxc_pc_list AND TARGET cd_test_rhi_pipeline_cache)
    list(POP_BACK _cd_dxc_pc_list _cd_dxc_pc_dll)
    get_filename_component(_cd_dxc_pc_dir "${_cd_dxc_pc_dll}" DIRECTORY)
    add_custom_command(TARGET cd_test_rhi_pipeline_cache POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_cd_dxc_pc_dir}/dxcompiler.dll"
              "$<TARGET_FILE_DIR:cd_test_rhi_pipeline_cache>"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_cd_dxc_pc_dir}/dxil.dll"
              "$<TARGET_FILE_DIR:cd_test_rhi_pipeline_cache>")
  endif()
else()
  cd_add_test(rhi_pipeline_cache
    SOURCES test_rhi_pipeline_cache.cpp
    DEPS    cd::rhi_vulkan cd::shader cd::rhi cd::core
  )
endif()

# D16: cross-backend PIXEL-PARITY capstone. ONE engine GLSL textured/lit/
# depth-tested scene is compiled to SPIR-V (Vulkan) AND DXIL (D3D12) via the
# SAME create_shader_module(kGlsl) call, rendered to a 256x256 offscreen on
# BOTH devices, read back, and diffed. This is the integration net the per-
# feature WARP smokes can't be: it catches inter-backend disagreement (UV-origin
# flip / NDC-Y handedness / sRGB / winding) that each single-backend smoke
# passes. Needs both the Vulkan AND D3D12 backends + the GLSL->DXIL toolchain
# DLLs; SKIPs cleanly when either device or dxc is absent. Windows-only (D3D12).
if(WIN32 AND CD_ENABLE_VULKAN AND TARGET cd_rhi_vulkan)
  cd_add_test(backend_pixel_parity
    SOURCES test_backend_pixel_parity.cpp
    DEPS    cd::rhi_vulkan cd::rhi_d3d12 cd::shader cd::rhi cd::core
  )
  file(GLOB _cd_dxc_pix_list "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/dxcompiler.dll")
  list(SORT _cd_dxc_pix_list)
  if(_cd_dxc_pix_list AND TARGET cd_test_backend_pixel_parity)
    list(POP_BACK _cd_dxc_pix_list _cd_dxc_pix_dll)
    get_filename_component(_cd_dxc_pix_dir "${_cd_dxc_pix_dll}" DIRECTORY)
    add_custom_command(TARGET cd_test_backend_pixel_parity POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_cd_dxc_pix_dir}/dxcompiler.dll"
              "$<TARGET_FILE_DIR:cd_test_backend_pixel_parity>"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_cd_dxc_pix_dir}/dxil.dll"
              "$<TARGET_FILE_DIR:cd_test_backend_pixel_parity>")
  endif()
endif()

# phase656-rt-tlas-coverage-all-kinds: T1.11 — TLAS accepts all eligible
# geometry kinds.  Locks the tlas_eligible host-side contract so a glTF
# prim with default flags enters the TLAS instance list, a sphere prim
# does too, and a candidate with tlas_eligible=false is excluded.  No
# device instantiation needed (pure host-side filter).
cd_add_test(tlas_coverage
  SOURCES test_tlas_coverage.cpp
  DEPS    cd::rhi cd::core
)

# phase766 FINALE-6 W2B F5: D3D12 native mesh-shader path.
# Surface-contract checks runnable everywhere; Windows-gated functional
# probes (TEST: D3D12DeviceCreatesAndExposesMeshShaderFeatureBit and
# D3D12CreatePipelineRejectsMeshDescWhenUnsupported) link against
# cd::rhi_d3d12 and SKIP cleanly when no D3D12 adapter is present.
if(WIN32)
  cd_add_test(rhi_d3d12_mesh_shader
    SOURCES test_d3d12_mesh_shader.cpp
    DEPS    cd::rhi cd::rhi_d3d12 cd::core
  )
else()
  cd_add_test(rhi_d3d12_mesh_shader
    SOURCES test_d3d12_mesh_shader.cpp
    DEPS    cd::rhi cd::core
  )
endif()

# phase1141 (X4-C1): GLSL -> SPIR-V -> HLSL -> DXIL chain + ADR S1
# ray-query translation spike. Windows links the d3d12 backend; the
# validation-only subset still runs elsewhere.
if(WIN32)
  cd_add_test(d3d12_shader_toolchain
    SOURCES test_d3d12_shader_toolchain.cpp
    DEPS    cd::rhi_d3d12 cd::shader cd::spirv_cross_glue cd::rhi cd::core
  )
  # DxcCreateInstance is IMPORT-linked (rhi_d3d12 links dxcompiler.lib),
  # so any binary that pulls compile_hlsl needs dxcompiler.dll at LOAD
  # time — a missing DLL pops a MODAL system-error dialog and hangs
  # ctest to timeout. dxil.dll is the DXIL signer. Copy the newest
  # Windows SDK pair next to the test (SDK bin is not on PATH).
  file(GLOB _cd_dxc_list "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/dxcompiler.dll")
  list(SORT _cd_dxc_list)
  if(_cd_dxc_list)
    list(POP_BACK _cd_dxc_list _cd_dxc_dll)
    get_filename_component(_cd_dxc_dir "${_cd_dxc_dll}" DIRECTORY)
    add_custom_command(TARGET cd_test_d3d12_shader_toolchain POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_cd_dxc_dir}/dxcompiler.dll"
              "$<TARGET_FILE_DIR:cd_test_d3d12_shader_toolchain>"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_cd_dxc_dir}/dxil.dll"
              "$<TARGET_FILE_DIR:cd_test_d3d12_shader_toolchain>")
  endif()
endif()

# ADR-20260614-d3d12-binding-model: DXIL-reflection verification of the
# space-per-set + push_constant remap. Compiles an engine-style GLSL with a
# set-0 resource, a set-1 bindless resource, and a push_constant block through
# the GLSL->SPIR-V->HLSL->DXIL chain, then reflects the DXIL (no GPU) to assert
# each binding lands at the register+space the D3D12 root signature declares.
if(WIN32)
  cd_add_test(d3d12_binding_reflection
    SOURCES test_d3d12_binding_reflection.cpp
    DEPS    cd::rhi_d3d12 cd::shader cd::spirv_cross_glue cd::rhi cd::core
  )
  # cd::rhi_d3d12 links d3d12/dxcompiler/dxguid PRIVATE, so the symbols don't
  # propagate to this test. It calls DxcCreateInstance + IID_PPV_ARGS for
  # ID3D12ShaderReflection directly, plus D3D12SerializeRootSignature +
  # D3D12CreateRootSignatureDeserializer (root-sig SIDE bindless-range check),
  # so link the SDK import libs explicitly.
  if(TARGET cd_test_d3d12_binding_reflection)
    target_link_libraries(cd_test_d3d12_binding_reflection PRIVATE d3d12 dxcompiler dxguid)
  endif()
  # Same DXC runtime dependency as the toolchain test: copy dxcompiler.dll +
  # dxil.dll next to the binary so DxcCreateInstance / CreateReflection resolve
  # at LOAD time (a missing DLL pops a modal dialog and hangs ctest).
  file(GLOB _cd_dxc_refl_list "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/dxcompiler.dll")
  list(SORT _cd_dxc_refl_list)
  if(_cd_dxc_refl_list)
    list(POP_BACK _cd_dxc_refl_list _cd_dxc_refl_dll)
    get_filename_component(_cd_dxc_refl_dir "${_cd_dxc_refl_dll}" DIRECTORY)
    add_custom_command(TARGET cd_test_d3d12_binding_reflection POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_cd_dxc_refl_dir}/dxcompiler.dll"
              "$<TARGET_FILE_DIR:cd_test_d3d12_binding_reflection>"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_cd_dxc_refl_dir}/dxil.dll"
              "$<TARGET_FILE_DIR:cd_test_d3d12_binding_reflection>")
  endif()
endif()

# phase1115 (X1-FU-F step 1) — IDrawRecorder split + parallel-pass
# recorder contract pins on the Null reference implementation.
cd_add_test(parallel_recorder
  SOURCES test_parallel_recorder.cpp
  DEPS    cd::rhi cd::core
)

# phase M3 (Metal backend): GLSL -> SPIR-V -> MSL host-side shader toolchain.
# PURE host-side (MSL is text; no MTLDevice) so it runs on EVERY CI lane
# including Windows. No DXC/Metal runtime DLL needed. SKIPs cleanly when the
# engine is built without glslang (CD_ENABLE_GLSLANG=OFF).
cd_add_test(metal_shader_toolchain
  SOURCES test_metal_shader_toolchain.cpp
  DEPS    cd::rhi_metal_shader cd::shader cd::spirv_cross_glue cd::gluon cd::rhi cd::core
)

# phase1186 (D3 + D5): D3D12 begin_render_pass depth/MRT + PSO depth-compare
# GPU smokes. Builds GLSL through the device DXIL path (so it needs the same
# dxcompiler.dll/dxil.dll runtime as the toolchain test) and runs a real
# render+readback on WARP. SKIPs cleanly when no D3D12 adapter or no DXC DLL.
if(WIN32)
  cd_add_test(d3d12_depth_mrt
    SOURCES test_d3d12_depth_mrt.cpp
    DEPS    cd::rhi_d3d12 cd::shader cd::rhi cd::core
  )
  file(GLOB _cd_dxc_dm_list "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/dxcompiler.dll")
  list(SORT _cd_dxc_dm_list)
  if(_cd_dxc_dm_list AND TARGET cd_test_d3d12_depth_mrt)
    list(POP_BACK _cd_dxc_dm_list _cd_dxc_dm_dll)
    get_filename_component(_cd_dxc_dm_dir "${_cd_dxc_dm_dll}" DIRECTORY)
    add_custom_command(TARGET cd_test_d3d12_depth_mrt POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_cd_dxc_dm_dir}/dxcompiler.dll"
              "$<TARGET_FILE_DIR:cd_test_d3d12_depth_mrt>"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_cd_dxc_dm_dir}/dxil.dll"
              "$<TARGET_FILE_DIR:cd_test_d3d12_depth_mrt>")
  endif()
endif()

# phase1191 (D11): D3D12 begin_parallel_render_pass / IParallelPassRecorder
# GPU correctness smoke. Renders K colored quads SERIALLY and via a PARALLEL
# render pass (K draws split across L lanes, recorded from worker threads,
# joined in lane order) and asserts the two readback images are BYTE-IDENTICAL.
# Same GLSL->DXIL device path + WARP render/readback as the depth/MRT test;
# SKIPs cleanly when no D3D12 adapter or no DXC DLL.
if(WIN32)
  cd_add_test(d3d12_parallel_pass
    SOURCES test_d3d12_parallel_pass.cpp
    DEPS    cd::rhi_d3d12 cd::shader cd::rhi cd::core
  )
  file(GLOB _cd_dxc_pp_list "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/dxcompiler.dll")
  list(SORT _cd_dxc_pp_list)
  if(_cd_dxc_pp_list AND TARGET cd_test_d3d12_parallel_pass)
    list(POP_BACK _cd_dxc_pp_list _cd_dxc_pp_dll)
    get_filename_component(_cd_dxc_pp_dir "${_cd_dxc_pp_dll}" DIRECTORY)
    add_custom_command(TARGET cd_test_d3d12_parallel_pass POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_cd_dxc_pp_dir}/dxcompiler.dll"
              "$<TARGET_FILE_DIR:cd_test_d3d12_parallel_pass>"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_cd_dxc_pp_dir}/dxil.dll"
              "$<TARGET_FILE_DIR:cd_test_d3d12_parallel_pass>")
  endif()
endif()

# phase1187 (D4 + D6): D3D12 PSO per-attachment blend + per-binding vertex
# stride GPU smokes. Same GLSL->DXIL device path + WARP render/readback as the
# depth/MRT test; SKIPs cleanly when no D3D12 adapter or no DXC DLL.
if(WIN32)
  cd_add_test(d3d12_blend_stride
    SOURCES test_d3d12_blend_stride.cpp
    DEPS    cd::rhi_d3d12 cd::shader cd::rhi cd::core
  )
  file(GLOB _cd_dxc_bs_list "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/dxcompiler.dll")
  list(SORT _cd_dxc_bs_list)
  if(_cd_dxc_bs_list AND TARGET cd_test_d3d12_blend_stride)
    list(POP_BACK _cd_dxc_bs_list _cd_dxc_bs_dll)
    get_filename_component(_cd_dxc_bs_dir "${_cd_dxc_bs_dll}" DIRECTORY)
    add_custom_command(TARGET cd_test_d3d12_blend_stride POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_cd_dxc_bs_dir}/dxcompiler.dll"
              "$<TARGET_FILE_DIR:cd_test_d3d12_blend_stride>"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_cd_dxc_bs_dir}/dxil.dll"
              "$<TARGET_FILE_DIR:cd_test_d3d12_blend_stride>")
  endif()
endif()

# phase1190 (D10 + B1b): D3D12 bindless runtime + static-sampler-half GPU
# smokes. Three textures written to bindless slots are dynamic-indexed by a
# shader (register(t0, space1)[push_const]); a classic textured shader samples
# via the now-backed static sampler. Same GLSL->DXIL device path + WARP
# render/readback as the depth/MRT test. The structural (feature-gate +
# write-slot validation) case runs draw-less; the dynamic-index + sampler draws
# SKIP cleanly when no D3D12 adapter, no DXC DLL, or SM6 indexing is
# unsupported on this WARP.
if(WIN32)
  cd_add_test(d3d12_bindless_sampler
    SOURCES test_d3d12_bindless_sampler.cpp
    DEPS    cd::rhi_d3d12 cd::shader cd::rhi cd::core
  )
  file(GLOB _cd_dxc_bl_list "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/dxcompiler.dll")
  list(SORT _cd_dxc_bl_list)
  if(_cd_dxc_bl_list AND TARGET cd_test_d3d12_bindless_sampler)
    list(POP_BACK _cd_dxc_bl_list _cd_dxc_bl_dll)
    get_filename_component(_cd_dxc_bl_dir "${_cd_dxc_bl_dll}" DIRECTORY)
    add_custom_command(TARGET cd_test_d3d12_bindless_sampler POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_cd_dxc_bl_dir}/dxcompiler.dll"
              "$<TARGET_FILE_DIR:cd_test_d3d12_bindless_sampler>"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_cd_dxc_bl_dir}/dxil.dll"
              "$<TARGET_FILE_DIR:cd_test_d3d12_bindless_sampler>")
  endif()
endif()

# phase1190 B6 follow-up: UNIFIED descriptor-heap CO-BIND smoke. One draw binds a
# CLASSIC set-0 (combined sampler2D + static sampler) AND the BINDLESS set-1 array
# (sampler2D[]) and reads BOTH — proving the per-set ring region and the bindless
# region share ONE shader-visible CBV/SRV/UAV heap (D3D12 allows only one such
# heap per draw, so separate heaps would silently unbind one). Same GLSL->DXIL
# device path + WARP render/readback as the bindless/sampler smoke; SKIPs cleanly
# when no D3D12 adapter, no DXC DLL, or SM6 indexing is unsupported on this WARP.
if(WIN32)
  cd_add_test(d3d12_cobind_heap
    SOURCES test_d3d12_cobind_heap.cpp
    DEPS    cd::rhi_d3d12 cd::shader cd::rhi cd::core
  )
  file(GLOB _cd_dxc_co_list "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/dxcompiler.dll")
  list(SORT _cd_dxc_co_list)
  if(_cd_dxc_co_list AND TARGET cd_test_d3d12_cobind_heap)
    list(POP_BACK _cd_dxc_co_list _cd_dxc_co_dll)
    get_filename_component(_cd_dxc_co_dir "${_cd_dxc_co_dll}" DIRECTORY)
    add_custom_command(TARGET cd_test_d3d12_cobind_heap POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_cd_dxc_co_dir}/dxcompiler.dll"
              "$<TARGET_FILE_DIR:cd_test_d3d12_cobind_heap>"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_cd_dxc_co_dir}/dxil.dll"
              "$<TARGET_FILE_DIR:cd_test_d3d12_cobind_heap>")
  endif()
endif()

# D8 + D7 + D9: D3D12 DXR command-recording path (bind_rt_pipeline /
# dispatch_rays / acceleration_structure_barrier). Structural DISPATCH_RAYS_DESC
# field-math + SBT-alignment assertions run on EVERY CI lane (no device); the
# Windows + DXR-gated functional probe records the three commands against the
# real device and submits (honest-SKIP when the adapter lacks
# D3D12_RAYTRACING_TIER >= 1.0 per ROADMAP §5). The functional probe compiles a
# tiny RT DXIL library via compile_hlsl (SM6.5), so it needs the same
# dxcompiler.dll/dxil.dll runtime as the toolchain test.
if(WIN32)
  cd_add_test(d3d12_dxr_command
    SOURCES test_d3d12_dxr_command.cpp
    DEPS    cd::rhi_d3d12 cd::rhi cd::core
  )
  file(GLOB _cd_dxc_rt_list "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/dxcompiler.dll")
  list(SORT _cd_dxc_rt_list)
  if(_cd_dxc_rt_list AND TARGET cd_test_d3d12_dxr_command)
    list(POP_BACK _cd_dxc_rt_list _cd_dxc_rt_dll)
    get_filename_component(_cd_dxc_rt_dir "${_cd_dxc_rt_dll}" DIRECTORY)
    add_custom_command(TARGET cd_test_d3d12_dxr_command POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_cd_dxc_rt_dir}/dxcompiler.dll"
              "$<TARGET_FILE_DIR:cd_test_d3d12_dxr_command>"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_cd_dxc_rt_dir}/dxil.dll"
              "$<TARGET_FILE_DIR:cd_test_d3d12_dxr_command>")
  endif()
else()
  cd_add_test(d3d12_dxr_command
    SOURCES test_d3d12_dxr_command.cpp
    DEPS    cd::rhi cd::core
  )
endif()

# parity1121 STRAND 2: D3D12 FACE-CULL parity under the NEGATIVE-HEIGHT viewport
# (phase1196). The neg-height viewport that makes D3D12 match Vulkan's +Y-down
# NDC FLIPS the window-space winding the rasterizer uses for facing, so
# FrontCounterClockwise must be INVERTED vs the engine descriptor to keep cull
# semantics backend-identical. Renders a front-wound + a reversed triangle on
# WARP under cull=kNone/kBack/kFront and asserts the engine's front_face=kCW
# back-cull keeps intended-front geometry VISIBLE (and culls the reversed one).
# Same GLSL->DXIL device path + DXC runtime as the depth/MRT smoke; SKIPs
# cleanly when no D3D12 adapter / no glslang / no DXC DLL.
if(WIN32)
  cd_add_test(d3d12_face_cull_parity
    SOURCES test_d3d12_face_cull_parity.cpp
    DEPS    cd::rhi_d3d12 cd::shader cd::rhi cd::core
  )
  file(GLOB _cd_dxc_fc_list "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/dxcompiler.dll")
  list(SORT _cd_dxc_fc_list)
  if(_cd_dxc_fc_list AND TARGET cd_test_d3d12_face_cull_parity)
    list(POP_BACK _cd_dxc_fc_list _cd_dxc_fc_dll)
    get_filename_component(_cd_dxc_fc_dir "${_cd_dxc_fc_dll}" DIRECTORY)
    add_custom_command(TARGET cd_test_d3d12_face_cull_parity POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_cd_dxc_fc_dir}/dxcompiler.dll"
              "$<TARGET_FILE_DIR:cd_test_d3d12_face_cull_parity>"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_cd_dxc_fc_dir}/dxil.dll"
              "$<TARGET_FILE_DIR:cd_test_d3d12_face_cull_parity>")
  endif()
endif()

# parity1121 WAVE A1 / D3: D3D12 DeviceFeatures parity. Pure CPU-side device
# introspection (no shaders / no DXC) — asserts the eight previously-unset
# DeviceFeatures flags (ray_query / geometry_shader / tessellation_shader /
# sampler_anisotropy / depth_clamp / dual_source_blend / timestamp_queries /
# pipeline_statistics_queries) are all true on a FL11+ WARP/hardware device,
# mirroring the Vulkan reference. Windows links the real d3d12 backend (SKIPs
# when no adapter); off-Windows compiles a trivial skip so the name exists in
# every CI lane.
if(WIN32)
  cd_add_test(d3d12_device_features
    SOURCES test_d3d12_device_features.cpp
    DEPS    cd::rhi_d3d12 cd::rhi cd::core
  )
else()
  cd_add_test(d3d12_device_features
    SOURCES test_d3d12_device_features.cpp
    DEPS    cd::rhi cd::core
  )
endif()

# parity1121 WAVE A1 / D1: D3D12 MSAA SampleCount parity. Creates a 4x MSAA
# colour texture + a 4x graphics PSO through the RHI and renders one triangle
# on WARP; D3D12 enforces the PSO/RTV sample-count match at execution, so a
# temp-reverted create_texture / PSO-builder fix removes the device (detected
# via the SubmitDesc Result + signal-semaphore, never a hang). Same GLSL->DXIL
# device path + DXC runtime as the depth/MRT smoke; SKIPs cleanly when no
# adapter / no glslang / no DXC DLL.
if(WIN32)
  cd_add_test(d3d12_msaa_parity
    SOURCES test_d3d12_msaa_parity.cpp
    DEPS    cd::rhi_d3d12 cd::shader cd::rhi cd::core
  )
  file(GLOB _cd_dxc_ms_list "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/dxcompiler.dll")
  list(SORT _cd_dxc_ms_list)
  if(_cd_dxc_ms_list AND TARGET cd_test_d3d12_msaa_parity)
    list(POP_BACK _cd_dxc_ms_list _cd_dxc_ms_dll)
    get_filename_component(_cd_dxc_ms_dir "${_cd_dxc_ms_dll}" DIRECTORY)
    add_custom_command(TARGET cd_test_d3d12_msaa_parity POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_cd_dxc_ms_dir}/dxcompiler.dll"
              "$<TARGET_FILE_DIR:cd_test_d3d12_msaa_parity>"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_cd_dxc_ms_dir}/dxil.dll"
              "$<TARGET_FILE_DIR:cd_test_d3d12_msaa_parity>")
  endif()
endif()

# parity1121 WAVE A1 / D4: D3D12 kStorageImage UAV ViewDimension parity. A
# compute shader declaring RWTexture3D is bound (via a kStorageImage descriptor
# write) to a 3D storage texture and dispatched on WARP; D3D12 enforces the
# UAV-dimension / shader-resource match, so a temp-reverted fix (TEXTURE2D UAV
# on the 3D resource) removes the device (detected via SubmitDesc Result +
# signal-semaphore, never a hang). Same GLSL->DXIL device path + DXC runtime as
# the depth/MRT smoke; SKIPs cleanly when no adapter / no glslang / no DXC DLL /
# compute PSO unsupported.
if(WIN32)
  cd_add_test(d3d12_storage_image_3d
    SOURCES test_d3d12_storage_image_3d.cpp
    DEPS    cd::rhi_d3d12 cd::shader cd::rhi cd::core
  )
  file(GLOB _cd_dxc_si_list "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/dxcompiler.dll")
  list(SORT _cd_dxc_si_list)
  if(_cd_dxc_si_list AND TARGET cd_test_d3d12_storage_image_3d)
    list(POP_BACK _cd_dxc_si_list _cd_dxc_si_dll)
    get_filename_component(_cd_dxc_si_dir "${_cd_dxc_si_dll}" DIRECTORY)
    add_custom_command(TARGET cd_test_d3d12_storage_image_3d POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_cd_dxc_si_dir}/dxcompiler.dll"
              "$<TARGET_FILE_DIR:cd_test_d3d12_storage_image_3d>"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_cd_dxc_si_dir}/dxil.dll"
              "$<TARGET_FILE_DIR:cd_test_d3d12_storage_image_3d>")
  endif()
endif()

# FINISH-ALL-BACKEND Wave A2 / D2: D3D12 custom-sampler BINDABILITY parity vs
# Vulkan. A SEPARATE texture2D + sampler (the sampler half is a real kSampler
# binding — the descriptor the bug ignored) samples a sharp 2x2 checkerboard on
# WARP; a POINT vs LINEAR sampler must DIFFER (filter case) and WRAP vs CLAMP
# must DIFFER (address case) — both identical under the pre-fix silent-ignore
# (everything resolved to a baked LINEAR-clamp default). Same GLSL->DXIL device
# path + DXC runtime as the depth/MRT smoke; SKIPs cleanly when no adapter / no
# glslang / no DXC DLL.
if(WIN32)
  cd_add_test(d3d12_sampler_bind_parity
    SOURCES test_d3d12_sampler_bind_parity.cpp
    DEPS    cd::rhi_d3d12 cd::shader cd::rhi cd::core
  )
  file(GLOB _cd_dxc_sb_list "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/dxcompiler.dll")
  list(SORT _cd_dxc_sb_list)
  if(_cd_dxc_sb_list AND TARGET cd_test_d3d12_sampler_bind_parity)
    list(POP_BACK _cd_dxc_sb_list _cd_dxc_sb_dll)
    get_filename_component(_cd_dxc_sb_dir "${_cd_dxc_sb_dll}" DIRECTORY)
    add_custom_command(TARGET cd_test_d3d12_sampler_bind_parity POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_cd_dxc_sb_dir}/dxcompiler.dll"
              "$<TARGET_FILE_DIR:cd_test_d3d12_sampler_bind_parity>"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_cd_dxc_sb_dir}/dxil.dll"
              "$<TARGET_FILE_DIR:cd_test_d3d12_sampler_bind_parity>")
  endif()
endif()

# phase1188 (D13 + D14): D3D12 DeviceLimits fill + PIX debug-group markers.
# Pure CPU-side limits read + an empty command list push/pop — no shaders or
# DXC runtime needed. Windows links the real d3d12 backend (SKIPs when no
# adapter); off-Windows compiles a trivial skip so the test name exists in
# every CI lane.
if(WIN32)
  cd_add_test(d3d12_limits_debug_group
    SOURCES test_d3d12_limits_debug_group.cpp
    DEPS    cd::rhi_d3d12 cd::rhi cd::core
  )
else()
  cd_add_test(d3d12_limits_debug_group
    SOURCES test_d3d12_limits_debug_group.cpp
    DEPS    cd::rhi cd::core
  )
endif()

# Backend-to-100 Wave 0a / V-DBGDEPTH: Vulkan analog of the D3D12 debug-group
# test. Locks the new VulkanCommandBuffer::debug_group_depth() override (push
# ++ / guarded pop -- / begin() reset) that replaced the inherited 0-stub. Pure
# host-side depth-counter check on the real Vulkan command buffer — no shaders,
# no submit, no DXC. Links the real Vulkan backend; honest GTEST_SKIP when no
# ICD is present. Guarded on the Vulkan target so non-Vulkan lanes still build.
if(TARGET cd_rhi_vulkan)
  cd_add_test(vulkan_debug_group_depth
    SOURCES test_vulkan_debug_group_depth.cpp
    DEPS    cd::rhi_vulkan cd::rhi cd::core
  )
endif()

# =============================================================================
# Backend-to-100 Wave 1 / C-D3D12-FIXES: one focused fail-on-revert gtest per
# A.3 D3D12 implement-slice fix. The three DEVICE-ONLY tests (no shaders, no
# DXC) cover the create-path fixes; the two SHADER tests run a real WARP
# render/dispatch and need the same dxcompiler.dll/dxil.dll runtime as the
# depth/MRT smoke. All SKIP cleanly when no D3D12 adapter is present.
# =============================================================================

# D-FORMAT-MAP: full Format -> DXGI_FORMAT table coverage (device-only).
# D-ROOTCOST: push-constant root-cost overflow rejection (device-only).
# D-BLAS-INDEXED: indexed-BLAS index-buffer honored (device-only, DXR-gated).
# D-HDR-SWAPCHAIN + D-SWAPCHAIN-RESIZE: colour-space + ResizeBuffers (device +
# a hidden Win32 window; links d3d12/dxgi import libs for the direct
# CheckColorSpaceSupport probe).
if(WIN32)
  cd_add_test(d3d12_format_map
    SOURCES test_d3d12_format_map.cpp
    DEPS    cd::rhi_d3d12 cd::rhi cd::core
  )
  cd_add_test(d3d12_root_cost
    SOURCES test_d3d12_root_cost.cpp
    DEPS    cd::rhi_d3d12 cd::rhi cd::core
  )
  cd_add_test(d3d12_blas_indexed
    SOURCES test_d3d12_blas_indexed.cpp
    DEPS    cd::rhi_d3d12 cd::rhi cd::core
  )
  cd_add_test(d3d12_swapchain
    SOURCES test_d3d12_swapchain.cpp
    DEPS    cd::rhi_d3d12 cd::rhi cd::core
  )
  if(TARGET cd_test_d3d12_swapchain)
    target_link_libraries(cd_test_d3d12_swapchain PRIVATE d3d12 dxgi)
  endif()
endif()

# =============================================================================
# Backend-to-100 Wave 3c — command-buffer hardening + Vulkan render-pass/copy.
# =============================================================================

# A-BINDPOINT + A-RESULT-DIAG: explicit per-CB bind point + queryable recording-
# error flag. The bind-point test records a graphics render pass THEN a compute
# dispatch with its own descriptor set and asserts (via readback) the compute set
# reached the dispatch (regression-locks the phase1213 DDGI bug class); it drives
# the GLSL->DXIL device path on D3D12 so it needs the dxcompiler.dll/dxil.dll
# runtime. The recording-error + Null cases are CPU-side. SKIPs cleanly when no
# device / no glslang / no DXC DLL / compute unsupported.
if(WIN32)
  cd_add_test(rhi_bindpoint
    SOURCES test_rhi_bindpoint.cpp
    DEPS    cd::rhi_vulkan cd::rhi_d3d12 cd::shader cd::rhi cd::core
  )
  file(GLOB _cd_dxc_bp_list "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/dxcompiler.dll")
  list(SORT _cd_dxc_bp_list)
  if(_cd_dxc_bp_list AND TARGET cd_test_rhi_bindpoint)
    list(POP_BACK _cd_dxc_bp_list _cd_dxc_bp_dll)
    get_filename_component(_cd_dxc_bp_dir "${_cd_dxc_bp_dll}" DIRECTORY)
    add_custom_command(TARGET cd_test_rhi_bindpoint POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_cd_dxc_bp_dir}/dxcompiler.dll"
              "$<TARGET_FILE_DIR:cd_test_rhi_bindpoint>"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_cd_dxc_bp_dir}/dxil.dll"
              "$<TARGET_FILE_DIR:cd_test_rhi_bindpoint>")
  endif()
else()
  cd_add_test(rhi_bindpoint
    SOURCES test_rhi_bindpoint.cpp
    DEPS    cd::rhi_vulkan cd::shader cd::rhi cd::core
  )
endif()

# V-MSAA-RESOLVE + V-COPY-IMG: render-pass MSAA resolve attachment +
# copy_texture_to_texture (image→image). No shaders (clear + copies only), so no
# DXC runtime needed. Links the Vulkan + (Windows) D3D12 backends; SKIPs cleanly
# when no device / 4x MSAA unsupported.
if(WIN32)
  cd_add_test(rhi_msaa_copyimg
    SOURCES test_rhi_msaa_copyimg.cpp
    DEPS    cd::rhi_vulkan cd::rhi_d3d12 cd::rhi cd::core
  )
else()
  cd_add_test(rhi_msaa_copyimg
    SOURCES test_rhi_msaa_copyimg.cpp
    DEPS    cd::rhi_vulkan cd::rhi cd::core
  )
endif()

# =============================================================================
# Backend-to-100 Wave 4b / C-METAL-TIER2 + C-METAL-CULL: the Metal GPU test
# suite, AUTHORED windows-now + RUN mac-gated. The Metal backend (cd_rhi_metal,
# Objective-C++ .mm) compiles + runs ONLY on Apple with CD_RHI_METAL_ENABLED=ON
# (auto-forced OFF on Windows — CMakeLists.txt:180-193). Each test source is
# gated:
#   * #if defined(__APPLE__) && defined(CD_RHI_METAL_ENABLED)  — real MTLDevice
#     assertions (needs the .mm symbols + the host MSL toolchain).
#   * #else                                                    — a skip-stub
#     TEST that GTEST_SKIPs "Metal backend disabled on this platform
#     (Apple-only)". References NO Metal symbols, so it compiles + links with
#     just gtest.
#
# Registration mirrors the d3d12_device_features split: on Apple WITH the .mm
# backend built we link the full Metal chain (cd::rhi_metal + the host MSL
# toolchain + cd::shader); everywhere else (Windows here) we link only
# cd::rhi + cd::core so the skip-stub builds clean under -Werror and the test
# REGISTERS + RUNs as SKIPPED. CD_RHI_METAL_ENABLED is PUBLIC on cd_rhi_metal,
# so linking it propagates the gate macro that unlocks the __APPLE__ body.
set(_CD_METAL_TESTS
  metal_device      test_metal_device.cpp
  metal_buffer      test_metal_buffer.cpp
  metal_texture     test_metal_texture.cpp
  metal_pipeline    test_metal_pipeline.cpp
  metal_shader      test_metal_shader.cpp
  metal_descriptor  test_metal_descriptor.cpp
  metal_barrier     test_metal_barrier.cpp
  metal_swapchain   test_metal_swapchain.cpp
  metal_rt          test_metal_rt.cpp
  metal_face_cull_parity test_metal_face_cull_parity.cpp
)
while(_CD_METAL_TESTS)
  list(POP_FRONT _CD_METAL_TESTS _cd_metal_name _cd_metal_src)
  if(APPLE AND CD_RHI_METAL_ENABLED)
    # The __APPLE__ body names create_metal_device + the IDevice surface + the
    # in-device GLSL->MSL path: link the .mm backend + the host toolchain.
    cd_add_test(${_cd_metal_name}
      SOURCES ${_cd_metal_src}
      DEPS    cd::rhi_metal cd::rhi_metal_shader cd::shader cd::rhi cd::core
    )
  else()
    # Windows / non-Apple: the #else skip-stub references no Metal symbols.
    cd_add_test(${_cd_metal_name}
      SOURCES ${_cd_metal_src}
      DEPS    cd::rhi cd::core
    )
  endif()
endwhile()

# D-SRV-MS: sample a 4x-MSAA texture (Texture2DMS SRV). Real WARP render +
# sampling pass; needs the GLSL->DXIL DXC runtime.
# D-MIPSTATE: per-subresource state — UAV-write mip 1 while mip 0 is shader-
# resource. Real WARP compute dispatch; needs the GLSL->DXIL DXC runtime.
if(WIN32)
  cd_add_test(d3d12_srv_ms
    SOURCES test_d3d12_srv_ms.cpp
    DEPS    cd::rhi_d3d12 cd::shader cd::rhi cd::core
  )
  cd_add_test(d3d12_mip_state
    SOURCES test_d3d12_mip_state.cpp
    DEPS    cd::rhi_d3d12 cd::shader cd::rhi cd::core
  )
  file(GLOB _cd_dxc_w1_list "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/dxcompiler.dll")
  list(SORT _cd_dxc_w1_list)
  if(_cd_dxc_w1_list)
    list(POP_BACK _cd_dxc_w1_list _cd_dxc_w1_dll)
    get_filename_component(_cd_dxc_w1_dir "${_cd_dxc_w1_dll}" DIRECTORY)
    foreach(_cd_w1_t cd_test_d3d12_srv_ms cd_test_d3d12_mip_state)
      if(TARGET ${_cd_w1_t})
        add_custom_command(TARGET ${_cd_w1_t} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
                  "${_cd_dxc_w1_dir}/dxcompiler.dll"
                  "$<TARGET_FILE_DIR:${_cd_w1_t}>"
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
                  "${_cd_dxc_w1_dir}/dxil.dll"
                  "$<TARGET_FILE_DIR:${_cd_w1_t}>")
      endif()
    endforeach()
  endif()
endif()
