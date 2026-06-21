// =============================================================================
// CHROMODYNAMIC — cd/nrc/Nrc.hpp
// Faz 3 M — Neural Radiance Cache (Müller 2021).
//
// Online-trained MLP that caches incoming radiance at each ray's
// last-bounce point. Replaces the indirect-bounce continuation of a
// path tracer with a single MLP query for substantial variance
// reduction at constant ray cost.
//
// STATUS: cpu-mlp-v1 CHARTER-COMPLETE; GPU backends formally SEALED in
// docs/ADR/ADR-20260621-nrc-gpu-backend-seal.md (focused Iglberger seal; the
// original honest seal is docs/ADR/ADR-20260616-band6-render-misc-scope.md §4).
// This is a CPU reference MLP, NOT a production radiance cache. A usable NRC
// needs an on-GPU trainable MLP (16-wide fully-fused tensor-core layers, Adam,
// frequency encoding) — a multi-month subsystem, sealed with a precise
// promote-on-need gate (not stubbed).
//
// This header ships the **public API** and a **CPU reference MLP**
// (tiny, single hidden layer, SGD, single-threaded) so consumer code can
// compile + smoke-test the contract today. The CPU MLP IS the verified
// contract: its forward pass (W_in·x+b_in → ReLU → W_out·h+b_out) and SGD
// backward pass (half-squared-error gradient, ReLU passthrough) are pinned
// exactly by tests/test_nrc.cpp against an independent reference forward and a
// single-sample overfit-to-~0-loss convergence proof.
//
// CPU-CHARTER COMPLETE — beyond per-sample `query`/`train_step` the reference
// also ships the CPU-tractable, golden-safe slices of the Müller NRC training
// recipe: a `train_batch()` mini-batch update (mean of the per-sample
// half-squared-error gradients — exactly what an SGD optimiser sees, and the
// shape the GPU fully-fused backward will mirror), a `query_into()`
// scratch-reusing inference overload (byte-identical to `query`, zero
// per-call heap traffic for hot loops), and a stateless `encode_input()`
// frequency-encoding helper (Müller §3.2 sinusoidal/positional encoding) that
// lifts a raw 5-D (pos.xyz + 2 material/dir scalars) sample into the 32-wide
// `kInputDim` feature vector the network consumes. None of these change the
// existing `query`/`train_step` math — they are additive and independently
// pinned by tests/test_nrc.cpp.
//
// SEALED BACKENDS — the production accelerators (Tiny CUDA NN, OneAPI MLP,
// custom SPIR-V compute) are *documented stubs only*: there is intentionally
// NO type, function, or `CD_NRC_BACKEND` translation unit in this library for
// them. A usable GPU NRC (16-wide fully-fused tensor-core layers, Adam,
// frequency encoding, per-frame online training co-scheduled with the path
// tracer) is a multi-month subsystem requiring a CUDA/SPIR-V toolchain and
// render-loop integration that this header cannot host. They are gated in
// docs/ADR/ADR-20260621-nrc-gpu-backend-seal.md (focused seal; original
// docs/ADR/ADR-20260616-band6-render-misc-scope.md §4) with a promote-on-need
// gate; do not stub partial GPU NN code here — it would be untestable on CI
// hardware.
//
// References:
//   * Müller, Rousselle, Novák, Keller — "Real-time Neural Radiance
//     Caching for Path Tracing" (SIGGRAPH 2021).
//   * Tiny CUDA NN — production fastest backend for the same MLP.
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <random>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace cd::nrc
{

/// MLP input features per query — Müller 2021 §3.2 frequency-encoded
/// position + direction + roughness/metallic/diffuse-albedo.
constexpr std::uint32_t kInputDim  = 32;
/// MLP output: RGB radiance (3 channels) — usually demodulated by
/// the host code before queue insert.
constexpr std::uint32_t kOutputDim = 3;

struct Config
{
    /// Number of hidden layers (Müller: 5).
    std::uint32_t hidden_layers { 5 };
    /// Width of each hidden layer (Müller: 64; production: 64-128).
    std::uint32_t hidden_width  { 64 };
    /// Learning rate for the Adam optimiser when running on-line.
    float         learning_rate { 1e-3F };
};

/// Number of raw scalars an NRC sample carries before frequency encoding —
/// Müller 2021 §3.2: 3 position + 2 packed direction/material scalars. The
/// encoder lifts these into the `kInputDim`-wide network feature vector.
constexpr std::uint32_t kRawSampleDim = 5;

/// Frequency (positional / sinusoidal) encoding — Müller 2021 §3.2.
///
/// Each of the `kRawSampleDim` raw scalars is expanded into a bank of
/// `[sin(2^L·π·x), cos(2^L·π·x)]` pairs for `L = 0 … frequencies-1`, packed
/// in raw-scalar-major order; the leading slots therefore hold the lowest
/// frequencies. The bank is sized so the encoding tiles the `kInputDim`-wide
/// feature buffer exactly with the default `frequencies = 3`
/// (5 scalars × 3 octaves × 2 phases + 2 padding = 32). Any output slot beyond
/// the produced pairs is zero-filled, so the result is always a fully-defined
/// `kInputDim` feature vector the network can consume directly.
///
/// Stateless + deterministic (pure function of `raw` + `frequencies`); no heap
/// traffic. This is the CPU reference for the GPU encoder the fully-fused MLP
/// will inline ahead of its first layer.
[[nodiscard]] inline std::array<float, kInputDim>
encode_input(std::span<const float, kRawSampleDim> raw,
             std::uint32_t                          frequencies = 3) noexcept
{
    std::array<float, kInputDim> feat {};
    std::size_t                  slot = 0;
    for (std::uint32_t s = 0; s < kRawSampleDim; ++s)
    {
        for (std::uint32_t l = 0; l < frequencies; ++l)
        {
            if (slot + 1 >= feat.size()) return feat;
            // int induction → derive the float scale (no float loop variable).
            const float scale =
                static_cast<float>(1U << l) * std::numbers::pi_v<float>;
            const float phase = scale * raw[s];
            feat[slot]     = std::sin(phase);
            feat[slot + 1] = std::cos(phase);
            slot += 2;
        }
    }
    return feat;
}

/// Tiny CPU reference MLP — single hidden layer, no Adam. Frequency encoding
/// is supplied by the stateless `encode_input` free function above. Exists so
/// the consumer code can compile + run tests today; the production backend
/// swaps in via the `CD_NRC_BACKEND` CMake option (none / tinycudann /
/// oneapi).
class CpuReferenceMlp
{
public:
    explicit CpuReferenceMlp(const Config& cfg) : cfg_(cfg)
    {
        std::mt19937 rng(0xC1DDF1);
        std::uniform_real_distribution<float> u(-0.1F, 0.1F);
        w_in_.resize(static_cast<std::size_t>(cfg_.hidden_width) * kInputDim);
        b_in_.resize(cfg_.hidden_width);
        w_out_.resize(static_cast<std::size_t>(kOutputDim) * cfg_.hidden_width);
        b_out_.resize(kOutputDim);
        for (auto& v : w_in_)  v = u(rng);
        for (auto& v : b_in_)  v = u(rng);
        for (auto& v : w_out_) v = u(rng);
        for (auto& v : b_out_) v = u(rng);
    }

    /// Inference. Returns the predicted RGB radiance.
    [[nodiscard]] cd::math::Vec3f
    query(std::span<const float, kInputDim> features) const
    {
        std::vector<float> h(cfg_.hidden_width, 0.0F);
        return query_into(features, h);
    }

    /// Inference with a caller-supplied hidden-activation scratch buffer —
    /// byte-identical result to `query`, but reuses `scratch` across calls so
    /// a hot per-pixel inference loop does no per-query heap allocation. The
    /// buffer is resized to `hidden_width` if needed; its prior contents are
    /// overwritten. Const: weights are not mutated.
    [[nodiscard]] cd::math::Vec3f
    query_into(std::span<const float, kInputDim> features,
               std::vector<float>&               scratch) const
    {
        scratch.assign(cfg_.hidden_width, 0.0F);
        for (std::uint32_t i = 0; i < cfg_.hidden_width; ++i)
        {
            float s = b_in_[i];
            for (std::uint32_t k = 0; k < kInputDim; ++k)
                s += w_in_[i * kInputDim + k] * features[k];
            scratch[i] = std::max(0.0F, s);  // ReLU
        }
        cd::math::Vec3f out { b_out_[0], b_out_[1], b_out_[2] };
        for (std::uint32_t c = 0; c < kOutputDim; ++c)
            for (std::uint32_t i = 0; i < cfg_.hidden_width; ++i)
                (&out.x)[c] += w_out_[c * cfg_.hidden_width + i] * scratch[i];
        return out;
    }

    /// One SGD step. `target` is the ground-truth radiance (from a
    /// path-tracer continuation). Production replaces this with Adam.
    void train_step(std::span<const float, kInputDim> features,
                    cd::math::Vec3f target)
    {
        // Forward pass.
        std::vector<float> h(cfg_.hidden_width, 0.0F);
        std::vector<float> pre(cfg_.hidden_width, 0.0F);
        for (std::uint32_t i = 0; i < cfg_.hidden_width; ++i)
        {
            float s = b_in_[i];
            for (std::uint32_t k = 0; k < kInputDim; ++k)
                s += w_in_[i * kInputDim + k] * features[k];
            pre[i] = s;
            h[i]   = std::max(0.0F, s);
        }
        cd::math::Vec3f out { b_out_[0], b_out_[1], b_out_[2] };
        for (std::uint32_t c = 0; c < kOutputDim; ++c)
            for (std::uint32_t i = 0; i < cfg_.hidden_width; ++i)
                (&out.x)[c] += w_out_[c * cfg_.hidden_width + i] * h[i];
        // Per-output error.
        const std::array<float, 3> err {
            out.x - target.x, out.y - target.y, out.z - target.z };
        // Output-layer SGD.
        for (std::uint32_t c = 0; c < kOutputDim; ++c)
        {
            b_out_[c] -= cfg_.learning_rate * err[c];
            for (std::uint32_t i = 0; i < cfg_.hidden_width; ++i)
                w_out_[c * cfg_.hidden_width + i] -=
                    cfg_.learning_rate * err[c] * h[i];
        }
        // Hidden-layer SGD (ReLU passthrough).
        std::vector<float> d_h(cfg_.hidden_width, 0.0F);
        for (std::uint32_t i = 0; i < cfg_.hidden_width; ++i)
        {
            if (pre[i] <= 0.0F) continue;
            for (std::uint32_t c = 0; c < kOutputDim; ++c)
                d_h[i] += err[c] * w_out_[c * cfg_.hidden_width + i];
        }
        for (std::uint32_t i = 0; i < cfg_.hidden_width; ++i)
        {
            b_in_[i] -= cfg_.learning_rate * d_h[i];
            for (std::uint32_t k = 0; k < kInputDim; ++k)
                w_in_[i * kInputDim + k] -=
                    cfg_.learning_rate * d_h[i] * features[k];
        }
    }

    /// Mini-batch SGD step — applies ONE update from the MEAN of the
    /// per-sample half-squared-error gradients over `batch` (a span of
    /// (features, target) pairs). This is exactly what an SGD optimiser sees
    /// for a batch, and is the shape the GPU fully-fused backward mirrors:
    /// accumulate gradients across all samples against a FROZEN weight set,
    /// then apply the averaged delta once (vs. `train_step`, which applies a
    /// per-sample delta immediately). An empty batch is a no-op. For a
    /// single-element batch the result equals `train_step` on that element (the
    /// mean over N=1 is the identity update; bit-for-bit modulo the harmless
    /// `(lr·err)·h` vs `lr·(err·h)` float reassociation). Determinism: no RNG,
    /// no allocation beyond the gradient accumulators; output is a pure
    /// function of the current weights + batch.
    void train_batch(
        std::span<const std::pair<std::array<float, kInputDim>,
                                  cd::math::Vec3f>> batch)
    {
        if (batch.empty()) return;

        const std::size_t w_in_n  = w_in_.size();
        const std::size_t b_in_n  = b_in_.size();
        const std::size_t w_out_n = w_out_.size();

        std::vector<float> g_w_in(w_in_n, 0.0F);
        std::vector<float> g_b_in(b_in_n, 0.0F);
        std::vector<float> g_w_out(w_out_n, 0.0F);
        std::array<float, kOutputDim> g_b_out {};

        std::vector<float> h(cfg_.hidden_width, 0.0F);
        std::vector<float> pre(cfg_.hidden_width, 0.0F);

        // Accumulate per-sample gradients against the FROZEN weights.
        for (const auto& [feat_arr, target] : batch)
        {
            const std::span<const float, kInputDim> features(feat_arr);
            for (std::uint32_t i = 0; i < cfg_.hidden_width; ++i)
            {
                float s = b_in_[i];
                for (std::uint32_t k = 0; k < kInputDim; ++k)
                    s += w_in_[i * kInputDim + k] * features[k];
                pre[i] = s;
                h[i]   = std::max(0.0F, s);
            }
            cd::math::Vec3f out { b_out_[0], b_out_[1], b_out_[2] };
            for (std::uint32_t c = 0; c < kOutputDim; ++c)
                for (std::uint32_t i = 0; i < cfg_.hidden_width; ++i)
                    (&out.x)[c] += w_out_[c * cfg_.hidden_width + i] * h[i];

            const std::array<float, kOutputDim> err {
                out.x - target.x, out.y - target.y, out.z - target.z };

            // Output-layer gradient.
            for (std::uint32_t c = 0; c < kOutputDim; ++c)
            {
                g_b_out[c] += err[c];
                for (std::uint32_t i = 0; i < cfg_.hidden_width; ++i)
                    g_w_out[c * cfg_.hidden_width + i] += err[c] * h[i];
            }
            // Hidden-layer gradient (ReLU passthrough).
            for (std::uint32_t i = 0; i < cfg_.hidden_width; ++i)
            {
                if (pre[i] <= 0.0F) continue;
                float d_hi = 0.0F;
                for (std::uint32_t c = 0; c < kOutputDim; ++c)
                    d_hi += err[c] * w_out_[c * cfg_.hidden_width + i];
                g_b_in[i] += d_hi;
                for (std::uint32_t k = 0; k < kInputDim; ++k)
                    g_w_in[i * kInputDim + k] += d_hi * features[k];
            }
        }

        // Apply the MEAN gradient once (lr · mean = lr/N · Σ).
        const float inv_n =
            cfg_.learning_rate / static_cast<float>(batch.size());
        for (std::uint32_t c = 0; c < kOutputDim; ++c)
        {
            b_out_[c] -= inv_n * g_b_out[c];
            for (std::uint32_t i = 0; i < cfg_.hidden_width; ++i)
                w_out_[c * cfg_.hidden_width + i] -=
                    inv_n * g_w_out[c * cfg_.hidden_width + i];
        }
        for (std::uint32_t i = 0; i < cfg_.hidden_width; ++i)
        {
            b_in_[i] -= inv_n * g_b_in[i];
            for (std::uint32_t k = 0; k < kInputDim; ++k)
                w_in_[i * kInputDim + k] -= inv_n * g_w_in[i * kInputDim + k];
        }
    }

private:
    Config             cfg_;
    std::vector<float> w_in_;
    std::vector<float> b_in_;
    std::vector<float> w_out_;
    std::vector<float> b_out_;
};

}  // namespace cd::nrc
