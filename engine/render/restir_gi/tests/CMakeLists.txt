cd_add_test(restir_gi
  SOURCES test_restir_gi.cpp
  DEPS    cd::restir_gi
)

# Floor-raise marathon (38 -> ~70) -- host-side ReSTIR-GI reservoir math
# edge / negative / invariant coverage (final_weight / update / combine /
# clamp_history / temporal_blend / invalidate boundaries + the combine
# M-cancellation identity + GLSL string presence + DEFERRED-TRACE seal locks).
# CPU-only header math, runs everywhere (no Vulkan ICD needed). ADD-ONLY: the
# deferred-trace GI sample CS is sealed out of charter (real ray-query gather
# is a multi-week RHI-dispatch subsystem, the cd::restir_di::DispatchPass
# pattern) -- these tests pin actual reservoir behaviour only.
cd_add_test(restir_gi_reservoir_math
  SOURCES test_restir_gi_reservoir_math.cpp
  DEPS    cd::restir_gi
)
