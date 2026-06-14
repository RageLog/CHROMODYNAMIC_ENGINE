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
