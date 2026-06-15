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
