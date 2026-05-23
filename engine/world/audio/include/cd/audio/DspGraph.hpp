// =============================================================================
// CHROMODYNAMIC — cd/audio/DspGraph.hpp
// Phase 14.F / Wave 162 — header-only DSP graph primitives.
//
// `DspGraph` is a tiny ordered chain of DSP processors. Each node
// owns its own state and implements:
//   void process(std::span<float> samples, std::uint32_t sample_rate);
//
// The graph runs them in order in-place — samples in == samples out
// (mono), buffer reused. Wiring a DAG with merges / splits is *not*
// in scope at v0.39.0; the chain shape covers the gain+biquad
// pipeline that every 3D-positional source needs.
//
// Why this lives next to the WASAPI / ALSA / CoreAudio backends:
// the DSP graph runs *per-source* on the engine side, before the
// mixer hands the buffer to the backend. Keeping it inside
// cd::audio means a downstream consumer can pull the graph out of
// the engine without picking up the platform backend code.
//
// Node types shipped at v0.39.0:
//   - GainNode   : scalar gain in linear amplitude
//   - LowpassBiquad / HighpassBiquad : Robert Bristow-Johnson
//     "RBJ Cookbook" biquads, parameterised by cutoff Hz + Q.
//
// All math is single-precision float; designed for short buffers
// (≤ 4096 samples per process() call typical). No allocations in
// process() — the graph's vector storage holds the node list and
// stays put across calls.
//
// Marathon honesty: this is the *structure* a DSP graph wants.
// HRTF nodes, real distance attenuation, convolution reverb live
// in a later wave alongside human-ear validation. The unit tests
// here verify the math at the sample level (impulse + sine
// responses), not the perceptual quality.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cd::audio
{

/// Base interface for every DSP node. Each node mutates `samples`
/// in place; the chain runs them in order.
class IDspNode
{
public:
    IDspNode() = default;
    virtual ~IDspNode() = default;
    IDspNode(const IDspNode&) = delete;
    IDspNode& operator=(const IDspNode&) = delete;
    IDspNode(IDspNode&&) = delete;
    IDspNode& operator=(IDspNode&&) = delete;

    /// Apply this node's transform to `samples` in place. Caller
    /// passes the host sample rate so frequency-dependent nodes
    /// (biquads, delay lines) can compute their coefficients.
    virtual void process(std::span<float> samples, std::uint32_t sample_rate) = 0;

    /// Reset any time-dependent internal state. Called when the
    /// graph is moved to a different source or a sample-rate
    /// change invalidates the cached coefficients.
    virtual void reset() noexcept {}

    /// Short human-readable label for diagnostics.
    [[nodiscard]] virtual std::string_view label() const noexcept = 0;
};

// ---------------------------------------------------------------------------
// GainNode — scalar multiply. Linear amplitude (1.0 = unity gain).
// ---------------------------------------------------------------------------
class GainNode final : public IDspNode
{
public:
    explicit GainNode(float gain) noexcept : gain_ { gain } {}

    void set_gain(float g) noexcept { gain_ = g; }
    [[nodiscard]] float gain() const noexcept { return gain_; }

    void process(std::span<float> samples, std::uint32_t /*sample_rate*/) override
    {
        for (auto& s : samples) s *= gain_;
    }

    [[nodiscard]] std::string_view label() const noexcept override { return label_; }

private:
    float gain_ { 1.0F };
    std::string label_ { "gain" };
};

// ---------------------------------------------------------------------------
// BiquadNode — Direct Form II Transposed biquad. Two specializations:
// lowpass / highpass via the RBJ Cookbook coefficient formulas.
// ---------------------------------------------------------------------------
enum class BiquadKind : std::uint8_t
{
    kLowpass,
    kHighpass,
};

class BiquadNode final : public IDspNode
{
public:
    BiquadNode(BiquadKind kind, float cutoff_hz, float q = 0.7071F) noexcept
        : kind_ { kind }, cutoff_hz_ { cutoff_hz }, q_ { q }
    {
    }

    void set_cutoff(float hz) noexcept
    {
        cutoff_hz_ = hz;
        coeffs_valid_ = false;
    }
    void set_q(float q) noexcept
    {
        q_ = q;
        coeffs_valid_ = false;
    }

    void reset() noexcept override
    {
        z1_ = 0.0F;
        z2_ = 0.0F;
    }

    void process(std::span<float> samples, std::uint32_t sample_rate) override
    {
        if (!coeffs_valid_ || sample_rate != last_sr_)
        {
            compute_coeffs_(sample_rate);
            last_sr_ = sample_rate;
            coeffs_valid_ = true;
        }
        // Direct Form II Transposed.
        for (auto& x : samples)
        {
            const float y = b0_ * x + z1_;
            z1_ = b1_ * x - a1_ * y + z2_;
            z2_ = b2_ * x - a2_ * y;
            x = y;
        }
    }

    [[nodiscard]] std::string_view label() const noexcept override
    {
        return kind_ == BiquadKind::kLowpass ? lp_label_ : hp_label_;
    }

private:
    void compute_coeffs_(std::uint32_t sr) noexcept
    {
        // RBJ Cookbook (audio-eq-cookbook.txt).
        const float pi = std::numbers::pi_v<float>;
        const float omega = 2.0F * pi * cutoff_hz_ / static_cast<float>(sr);
        const float sin_w = std::sin(omega);
        const float cos_w = std::cos(omega);
        const float alpha = sin_w / (2.0F * std::max(0.0001F, q_));

        float b0u, b1u, b2u;
        if (kind_ == BiquadKind::kLowpass)
        {
            const float one_m = 1.0F - cos_w;
            b0u = one_m * 0.5F;
            b1u = one_m;
            b2u = one_m * 0.5F;
        }
        else
        {
            const float one_p = 1.0F + cos_w;
            b0u =  one_p * 0.5F;
            b1u = -one_p;
            b2u =  one_p * 0.5F;
        }
        const float a0u = 1.0F + alpha;
        const float a1u = -2.0F * cos_w;
        const float a2u = 1.0F - alpha;

        // Normalize by a0.
        b0_ = b0u / a0u;
        b1_ = b1u / a0u;
        b2_ = b2u / a0u;
        a1_ = a1u / a0u;
        a2_ = a2u / a0u;
    }

    BiquadKind kind_ { BiquadKind::kLowpass };
    float cutoff_hz_ { 1000.0F };
    float q_ { 0.7071F };
    bool coeffs_valid_ { false };
    std::uint32_t last_sr_ { 0 };

    // Coefficients (normalized by a0).
    float b0_ { 1.0F }, b1_ { 0.0F }, b2_ { 0.0F };
    float a1_ { 0.0F }, a2_ { 0.0F };
    // Filter state (z^-1, z^-2).
    float z1_ { 0.0F }, z2_ { 0.0F };

    std::string lp_label_ { "biquad_lowpass" };
    std::string hp_label_ { "biquad_highpass" };
};

// ---------------------------------------------------------------------------
// DspGraph — ordered chain of nodes. Owns each node via unique_ptr
// so callers can build a chain and let lifetime follow the graph.
// ---------------------------------------------------------------------------
class DspGraph
{
public:
    DspGraph() = default;

    /// Append a node to the end of the chain. The graph takes
    /// ownership of `node`. Returns a non-owning observer so the
    /// caller can tune parameters without re-allocating.
    template <class T>
    T* push(std::unique_ptr<T> node)
    {
        auto* raw = node.get();
        nodes_.push_back(std::move(node));
        return raw;
    }

    /// Process `samples` through every node in order. Each node
    /// mutates in place.
    void process(std::span<float> samples, std::uint32_t sample_rate)
    {
        for (auto& n : nodes_) n->process(samples, sample_rate);
    }

    void reset() noexcept
    {
        for (auto& n : nodes_) n->reset();
    }

    [[nodiscard]] std::size_t size() const noexcept { return nodes_.size(); }
    [[nodiscard]] bool empty() const noexcept { return nodes_.empty(); }

    void clear() noexcept { nodes_.clear(); }

private:
    std::vector<std::unique_ptr<IDspNode>> nodes_;
};

}  // namespace cd::audio
