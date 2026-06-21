# cd::nrc

## Purpose
Neural Radiance Cache (NRC) — learned implicit representation of ray-traced radiance fields using small neural networks. Accelerates monte-carlo path tracing via task-parallelizable inference, achieving interactive GI quality with orders of magnitude fewer samples than brute-force ray tracing.

## Namespace
`cd::<render>::nrc::`

## Public headers
- `include/cd/nrc/Nrc.hpp` — Network evaluation kernel, cache coherency helpers, input preparation

## Primary types
- `cd::nrc::Config` — MLP descriptor (hidden layers/width, learning rate)
- `cd::nrc::CpuReferenceMlp` — single-hidden-layer CPU reference network: `query()` (forward inference → RGB radiance) + `query_into()` (byte-identical scratch-reusing zero-heap inference) + `train_step()` (one SGD update toward a path-traced target) + `train_batch()` (mini-batch SGD — mean per-sample gradient, the GPU fully-fused backward shape)
- `cd::nrc::encode_input()` — stateless Müller §3.2 frequency/positional encoding lifting a raw `kRawSampleDim` (5) sample into the `kInputDim` feature vector
- `cd::nrc::kInputDim` (32) / `kOutputDim` (3) / `kRawSampleDim` (5) — feature/output/raw dimensions

## Usage example
```cpp
#include <cd/nrc/Nrc.hpp>

cd::nrc::Config cfg{};
cfg.learning_rate = 1e-2F;
cd::nrc::CpuReferenceMlp mlp(cfg);

std::array<float, cd::nrc::kInputDim> feat{ /* frequency-encoded pos/dir/mat */ };
const std::span<const float, cd::nrc::kInputDim> fs(feat);

// Online training against a path-tracer continuation, then query.
mlp.train_step(fs, /* target radiance */ cd::math::Vec3f{0.5F, 0.5F, 0.5F});
cd::math::Vec3f radiance = mlp.query(fs);
```

## Build/Test
```bash
cmake --build --preset ninja-debug --target cd_nrc
ctest --preset ninja-debug -R nrc
```

## Dependencies
- `cd::core` — engine types
- `cd::math` — vector/matrix math

## References
- **Müller et al. 2021**, "Neural Radiance Caching for Path Tracing" (TOG/SIGGRAPH)
- Research: `research/library/MANIFEST.csv` for linked PDF + methodology

## Notes
- INTERFACE (header-only) library. `CpuReferenceMlp` is the **verified contract**: forward + SGD backward math pinned exactly by `tests/test_nrc.cpp` (independent-reference forward agreement, ReLU clamp, determinism, single-sample overfit-to-~0 loss, learning-rate effect, zero-error no-op).
- **SEALED backends**: production Tiny CUDA NN / OneAPI / SPIR-V accelerators are documented stubs only — no code in this library. A usable GPU NRC (fully-fused tensor-core MLP, Adam, frequency encoding, per-frame online training) is a multi-month subsystem gated in `docs/ADR/ADR-20260616-band6-render-misc-scope.md` §4.
- Designed for hybrid rendering: combines fast NRC inference with occasional fine-quality sample refinement.
- Particularly effective for glossy-to-diffuse indirect illumination.
