// =============================================================================
// CHROMODYNAMIC — cd/nrc/Nrc.hpp
// Faz 3 M — Neural Radiance Cache (Müller 2021).
//
// Online-trained MLP that caches incoming radiance at each ray's
// last-bounce point. Replaces the indirect-bounce continuation of a
// path tracer with a single MLP query for substantial variance
// reduction at constant ray cost.
//
// STATUS: research-skeleton-v1 (sealed in
// docs/ADR/ADR-20260616-band6-render-misc-scope.md §4). This is a CPU
// reference / research skeleton, NOT a production radiance cache. A usable
// NRC needs an on-GPU trainable MLP (16-wide fully-fused tensor-core layers,
// Adam, frequency encoding) — a multi-month subsystem.
//
// This header ships the **public API** and a **CPU reference MLP**
// (tiny, single hidden layer, SGD, single-threaded) so consumer code can
// compile + smoke-test the contract today. The CPU MLP IS the verified
// contract: its forward pass (W_in·x+b_in → ReLU → W_out·h+b_out) and SGD
// backward pass (half-squared-error gradient, ReLU passthrough) are pinned
// exactly by tests/test_nrc.cpp against an independent reference forward and a
// single-sample overfit-to-~0-loss convergence proof.
//
// SEALED BACKENDS — the production accelerators (Tiny CUDA NN, OneAPI MLP,
// custom SPIR-V compute) are *documented stubs only*: there is intentionally
// NO type, function, or `CD_NRC_BACKEND` translation unit in this library for
// them. A usable GPU NRC (16-wide fully-fused tensor-core layers, Adam,
// frequency encoding, per-frame online training co-scheduled with the path
// tracer) is a multi-month subsystem requiring a CUDA/SPIR-V toolchain and
// render-loop integration that this header cannot host. They are gated in
// docs/ADR/ADR-20260616-band6-render-misc-scope.md §4 (promote-on-need); do
// not stub partial GPU NN code here — it would be untestable on CI hardware.
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
#include <cstdint>
#include <random>
#include <span>
#include <string_view>
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

/// Tiny CPU reference MLP — single hidden layer, no Adam, no
/// frequency encoding. Exists so the consumer code can compile +
/// run tests today; the production backend swaps in via the
/// `CD_NRC_BACKEND` CMake option (none / tinycudann / oneapi).
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
        for (std::uint32_t i = 0; i < cfg_.hidden_width; ++i)
        {
            float s = b_in_[i];
            for (std::uint32_t k = 0; k < kInputDim; ++k)
                s += w_in_[i * kInputDim + k] * features[k];
            h[i] = std::max(0.0F, s);  // ReLU
        }
        cd::math::Vec3f out { b_out_[0], b_out_[1], b_out_[2] };
        for (std::uint32_t c = 0; c < kOutputDim; ++c)
            for (std::uint32_t i = 0; i < cfg_.hidden_width; ++i)
                (&out.x)[c] += w_out_[c * cfg_.hidden_width + i] * h[i];
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

private:
    Config             cfg_;
    std::vector<float> w_in_;
    std::vector<float> b_in_;
    std::vector<float> w_out_;
    std::vector<float> b_out_;
};

}  // namespace cd::nrc
