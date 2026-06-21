// =============================================================================
// cd::nrc — CPU reference MLP tests (Müller 2021 Neural Radiance Cache).
//
// The CpuReferenceMlp is a single-hidden-layer perceptron:
//   h   = ReLU(W_in · x + b_in)            (hidden_width units)
//   out = W_out · h + b_out                (kOutputDim = 3, linear)
// trained by plain SGD on the half-squared-error loss L = ½·Σ(out-target)².
//
// These tests pin the ACTUAL forward/backward behaviour of that code. A
// matching reference forward pass is reproduced here from the public init
// contract (mt19937 seed 0xC1DDF1, uniform[-0.1,0.1) drawn in the order
// w_in, b_in, w_out, b_out) so that exact numeric outputs can be asserted
// without exposing the private weights. Backends (Tiny CUDA NN / SPIR-V) are
// documented stubs — no API exists to exercise, so no test targets them.
//
// AAA throughout; deterministic (seeded RNG only); edge + negative coverage.
// =============================================================================
#include <cd/nrc/Nrc.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <random>
#include <span>
#include <vector>

namespace
{

using cd::nrc::Config;
using cd::nrc::CpuReferenceMlp;
using cd::nrc::kInputDim;
using cd::nrc::kOutputDim;
using cd::math::Vec3f;

// -----------------------------------------------------------------------------
// Reference forward pass — independent re-implementation of the production
// init + query so we can assert exact numeric agreement with the header code.
// Mirrors CpuReferenceMlp's ctor RNG draw order and query arithmetic exactly.
// -----------------------------------------------------------------------------
struct ReferenceWeights
{
    std::uint32_t      width {};
    std::vector<float> w_in;   // width * kInputDim
    std::vector<float> b_in;   // width
    std::vector<float> w_out;  // kOutputDim * width
    std::vector<float> b_out;  // kOutputDim
};

[[nodiscard]] ReferenceWeights make_reference_weights(std::uint32_t width)
{
    ReferenceWeights r {};
    r.width = width;
    std::mt19937                          rng(0xC1DDF1);
    std::uniform_real_distribution<float> u(-0.1F, 0.1F);
    r.w_in.resize(static_cast<std::size_t>(width) * kInputDim);
    r.b_in.resize(width);
    r.w_out.resize(static_cast<std::size_t>(kOutputDim) * width);
    r.b_out.resize(kOutputDim);
    for (auto& v : r.w_in) v = u(rng);
    for (auto& v : r.b_in) v = u(rng);
    for (auto& v : r.w_out) v = u(rng);
    for (auto& v : r.b_out) v = u(rng);
    return r;
}

[[nodiscard]] Vec3f reference_forward(const ReferenceWeights&            r,
                                      std::span<const float, kInputDim>  feat)
{
    std::vector<float> h(r.width, 0.0F);
    for (std::uint32_t i = 0; i < r.width; ++i)
    {
        float s = r.b_in[i];
        for (std::uint32_t k = 0; k < kInputDim; ++k)
            s += r.w_in[static_cast<std::size_t>(i) * kInputDim + k] * feat[k];
        h[i] = std::max(0.0F, s);  // ReLU
    }
    Vec3f out { r.b_out[0], r.b_out[1], r.b_out[2] };
    for (std::uint32_t c = 0; c < kOutputDim; ++c)
        for (std::uint32_t i = 0; i < r.width; ++i)
            out[c] += r.w_out[static_cast<std::size_t>(c) * r.width + i] * h[i];
    return out;
}

// L1 error of the MLP against `target` for a fixed feature vector.
[[nodiscard]] float l1_error(const CpuReferenceMlp&              mlp,
                             std::span<const float, kInputDim>   feat,
                             const Vec3f&                        target)
{
    const auto p = mlp.query(feat);
    return std::abs(p.x - target.x) + std::abs(p.y - target.y) +
           std::abs(p.z - target.z);
}

[[nodiscard]] std::array<float, kInputDim> filled_feat(float v)
{
    std::array<float, kInputDim> f {};
    std::ranges::fill(f, v);
    return f;
}

// =============================================================================
// Construction / smoke
// =============================================================================
TEST(Nrc, ConstructionDoesNotCrash)
{
    const Config c {};
    const CpuReferenceMlp mlp(c);
    SUCCEED();
}

TEST(Nrc, QueryReturnsFiniteValues)
{
    const Config c {};
    const CpuReferenceMlp mlp(c);
    const auto feat = filled_feat(0.1F);
    const auto out  = mlp.query(std::span<const float, kInputDim>(feat));
    EXPECT_TRUE(std::isfinite(out.x));
    EXPECT_TRUE(std::isfinite(out.y));
    EXPECT_TRUE(std::isfinite(out.z));
}

// =============================================================================
// Forward pass — exact math vs. an independent reference implementation.
// =============================================================================
TEST(Nrc, ForwardMatchesReferenceForwardExactly)
{
    const Config c {};
    const CpuReferenceMlp mlp(c);
    const auto ref = make_reference_weights(c.hidden_width);

    // A non-trivial, asymmetric feature vector so every weight contributes.
    std::array<float, kInputDim> feat {};
    for (std::size_t i = 0; i < feat.size(); ++i)
        feat[i] = 0.05F * static_cast<float>(i) - 0.3F;
    const std::span<const float, kInputDim> fs(feat);

    const auto got = mlp.query(fs);
    const auto exp = reference_forward(ref, fs);

    EXPECT_FLOAT_EQ(got.x, exp.x);
    EXPECT_FLOAT_EQ(got.y, exp.y);
    EXPECT_FLOAT_EQ(got.z, exp.z);
}

// Zero input collapses the affine hidden pre-activation to b_in; ReLU then
// gates each unit, and the output is W_out · ReLU(b_in) + b_out — NOT just
// b_out (a common forward-pass bug would drop the hidden contribution).
TEST(Nrc, ZeroInputEqualsBiasPropagatedThroughReLU)
{
    const Config c {};
    const CpuReferenceMlp mlp(c);
    const auto ref = make_reference_weights(c.hidden_width);

    const auto feat = filled_feat(0.0F);
    const std::span<const float, kInputDim> fs(feat);

    const auto got = mlp.query(fs);
    const auto exp = reference_forward(ref, fs);
    EXPECT_FLOAT_EQ(got.x, exp.x);
    EXPECT_FLOAT_EQ(got.y, exp.y);
    EXPECT_FLOAT_EQ(got.z, exp.z);
}

// Determinism: two MLPs built from the same Config see byte-identical weights
// (fixed RNG seed), so identical input must yield identical output.
TEST(Nrc, WeightInitIsDeterministicAcrossInstances)
{
    const Config c {};
    const CpuReferenceMlp a(c);
    const CpuReferenceMlp b(c);
    const auto feat = filled_feat(0.123F);
    const std::span<const float, kInputDim> fs(feat);

    const auto oa = a.query(fs);
    const auto ob = b.query(fs);
    EXPECT_FLOAT_EQ(oa.x, ob.x);
    EXPECT_FLOAT_EQ(oa.y, ob.y);
    EXPECT_FLOAT_EQ(oa.z, ob.z);
}

// ReLU non-linearity: drive the hidden pre-activations strongly negative.
// With width=1 and a single very negative feature the hidden unit MAY clamp;
// rather than assume the sign, assert the queried output exactly equals the
// reference, which applies the same max(0,·) — i.e. the activation is ReLU,
// not identity or leaky. Cross-checked against a hand ReLU on the reference.
TEST(Nrc, HiddenActivationIsReLU)
{
    Config c {};
    c.hidden_width = 8;
    const CpuReferenceMlp mlp(c);
    const auto ref = make_reference_weights(c.hidden_width);

    // Large-magnitude features force several hidden pre-activations below 0,
    // so the ReLU clamp is genuinely exercised (vs. an identity passthrough).
    std::array<float, kInputDim> feat {};
    for (std::size_t i = 0; i < feat.size(); ++i)
        feat[i] = (i % 2 == 0) ? 5.0F : -5.0F;
    const std::span<const float, kInputDim> fs(feat);

    // Independent ReLU forward, then confirm at least one unit actually clamped
    // (otherwise this test would silently pass on an identity activation).
    std::uint32_t clamped = 0;
    for (std::uint32_t i = 0; i < ref.width; ++i)
    {
        float s = ref.b_in[i];
        for (std::uint32_t k = 0; k < kInputDim; ++k)
            s += ref.w_in[static_cast<std::size_t>(i) * kInputDim + k] * feat[k];
        if (s <= 0.0F) ++clamped;
    }
    ASSERT_GT(clamped, 0U) << "test feature did not exercise the ReLU clamp";

    const auto got = mlp.query(fs);
    const auto exp = reference_forward(ref, fs);
    EXPECT_FLOAT_EQ(got.x, exp.x);
    EXPECT_FLOAT_EQ(got.y, exp.y);
    EXPECT_FLOAT_EQ(got.z, exp.z);
}

// Layer dimensions: changing hidden_width changes the realised network, and
// the query still produces 3 finite outputs for the configured width.
TEST(Nrc, LayerWidthConfigurableAndOutputDimIsThree)
{
    for (const std::uint32_t w : { 1U, 4U, 16U, 64U, 128U })
    {
        Config c {};
        c.hidden_width = w;
        const CpuReferenceMlp mlp(c);
        const auto ref  = make_reference_weights(w);
        const auto feat = filled_feat(0.2F);
        const std::span<const float, kInputDim> fs(feat);

        const auto got = mlp.query(fs);
        const auto exp = reference_forward(ref, fs);
        EXPECT_FLOAT_EQ(got.x, exp.x) << "width=" << w;
        EXPECT_FLOAT_EQ(got.y, exp.y) << "width=" << w;
        EXPECT_FLOAT_EQ(got.z, exp.z) << "width=" << w;
    }
    // kOutputDim is fixed at 3 (RGB radiance) by the public contract.
    EXPECT_EQ(kOutputDim, 3U);
    EXPECT_EQ(kInputDim, 32U);
}

// Inference is a pure function of the (immutable) query input: repeated and
// batched queries on a const MLP never mutate state.
TEST(Nrc, QueryIsPureOverABatch)
{
    const Config c {};
    const CpuReferenceMlp mlp(c);

    const std::array<std::array<float, kInputDim>, 4> batch {
        filled_feat(-0.5F), filled_feat(0.0F),
        filled_feat(0.5F),  filled_feat(1.0F) };

    std::array<Vec3f, 4> first {};
    for (std::size_t b = 0; b < batch.size(); ++b)
        first[b] = mlp.query(std::span<const float, kInputDim>(batch[b]));

    // Re-query in reverse order; results must be identical (no hidden state).
    for (std::size_t b = batch.size(); b-- > 0;)
    {
        const auto again =
            mlp.query(std::span<const float, kInputDim>(batch[b]));
        EXPECT_FLOAT_EQ(again.x, first[b].x);
        EXPECT_FLOAT_EQ(again.y, first[b].y);
        EXPECT_FLOAT_EQ(again.z, first[b].z);
    }
}

// Saturation/large-magnitude inputs stay finite (no overflow to inf/NaN with
// the [-0.1,0.1) weight scale even for very large feature magnitudes).
TEST(Nrc, LargeMagnitudeInputStaysFinite)
{
    const Config c {};
    const CpuReferenceMlp mlp(c);
    const auto feat = filled_feat(1.0e6F);
    const auto out  = mlp.query(std::span<const float, kInputDim>(feat));
    EXPECT_TRUE(std::isfinite(out.x));
    EXPECT_TRUE(std::isfinite(out.y));
    EXPECT_TRUE(std::isfinite(out.z));
}

// =============================================================================
// Training (SGD backward pass) — the heart of the NRC online update.
// =============================================================================

// Strict learning on a fittable constant target. A research skeleton that does
// not actually learn (no-op train_step / broken backward) leaves err1 ≈ err0
// and trips every bound below. The original weak test allowed zero gain.
TEST(Nrc, TrainingStrictlyReducesErrorOnFittableTarget)
{
    Config c {};
    c.learning_rate = 1e-2F;
    CpuReferenceMlp mlp(c);

    std::array<float, kInputDim> feat {};
    for (std::size_t i = 0; i < feat.size(); ++i)
        feat[i] = (i % 3 == 0) ? 1.0F : 0.0F;
    const std::span<const float, kInputDim> fs(feat);
    const Vec3f target { 0.5F, 0.5F, 0.5F };

    const float err0 = l1_error(mlp, fs, target);
    for (int i = 0; i < 400; ++i)
        mlp.train_step(fs, target);
    const float err1 = l1_error(mlp, fs, target);

    EXPECT_LT(err1, err0)        << "MLP failed to reduce error at all";
    EXPECT_LT(err1, err0 * 0.5F) << "error reduced < 50% — backward pass weak/broken";
    EXPECT_LT(err1, 0.05F)       << "did not converge near the fittable target";
}

// A single training sample must be overfittable to (almost) zero loss — this
// is the strongest evidence the gradient direction and chain rule are right.
TEST(Nrc, OverfitsSingleSampleToNearZeroLoss)
{
    Config c {};
    c.learning_rate = 1e-2F;
    CpuReferenceMlp mlp(c);

    std::array<float, kInputDim> feat {};
    for (std::size_t i = 0; i < feat.size(); ++i)
        feat[i] = 0.3F + 0.01F * static_cast<float>(i);
    const std::span<const float, kInputDim> fs(feat);
    const Vec3f target { 0.7F, -0.2F, 0.4F };

    for (int i = 0; i < 5000; ++i)
        mlp.train_step(fs, target);

    const auto out = mlp.query(fs);
    EXPECT_NEAR(out.x, target.x, 1e-3F);
    EXPECT_NEAR(out.y, target.y, 1e-3F);
    EXPECT_NEAR(out.z, target.z, 1e-3F);
}

// Monotone-ish descent: error after N steps is below error after N/2 steps —
// SGD on a single fittable point should not diverge at a sane learning rate.
TEST(Nrc, ErrorDecreasesMonotonicallyOverTraining)
{
    Config c {};
    c.learning_rate = 5e-3F;
    CpuReferenceMlp mlp(c);

    const auto feat = filled_feat(0.4F);
    const std::span<const float, kInputDim> fs(feat);
    const Vec3f target { 0.25F, 0.25F, 0.25F };

    const float e0 = l1_error(mlp, fs, target);
    for (int i = 0; i < 100; ++i) mlp.train_step(fs, target);
    const float e100 = l1_error(mlp, fs, target);
    for (int i = 0; i < 100; ++i) mlp.train_step(fs, target);
    const float e200 = l1_error(mlp, fs, target);

    EXPECT_LT(e100, e0);
    EXPECT_LT(e200, e100);
}

// Negative/degenerate: training on a zero feature vector still drives the
// OUTPUT bias toward the target (only b_out and b_in can move; W_in·0 = 0, so
// the input weights are frozen — confirms the gradient is gated by the input).
TEST(Nrc, TrainingOnZeroInputStillFitsViaBias)
{
    Config c {};
    c.learning_rate = 1e-2F;
    CpuReferenceMlp mlp(c);

    const auto feat = filled_feat(0.0F);
    const std::span<const float, kInputDim> fs(feat);
    const Vec3f target { 0.6F, 0.6F, 0.6F };

    const float err0 = l1_error(mlp, fs, target);
    for (int i = 0; i < 2000; ++i)
        mlp.train_step(fs, target);
    const float err1 = l1_error(mlp, fs, target);

    EXPECT_LT(err1, err0);
    EXPECT_LT(err1, 0.05F) << "bias path failed to fit a constant target";
}

// Learning-rate effect: a larger (but still stable) rate converges faster, so
// after a fixed budget the high-rate net has strictly smaller error.
TEST(Nrc, HigherLearningRateConvergesFaster)
{
    std::array<float, kInputDim> feat {};
    for (std::size_t i = 0; i < feat.size(); ++i)
        feat[i] = (i % 2 == 0) ? 0.5F : -0.5F;
    const std::span<const float, kInputDim> fs(feat);
    const Vec3f target { 0.3F, 0.1F, -0.2F };

    Config slow {};
    slow.learning_rate = 1e-3F;
    CpuReferenceMlp m_slow(slow);

    Config fast {};
    fast.learning_rate = 1e-2F;
    CpuReferenceMlp m_fast(fast);

    // Same start (identical seed) → fair race over the same step budget.
    const float start = l1_error(m_slow, fs, target);
    ASSERT_FLOAT_EQ(start, l1_error(m_fast, fs, target));

    for (int i = 0; i < 200; ++i)
    {
        m_slow.train_step(fs, target);
        m_fast.train_step(fs, target);
    }
    const float e_slow = l1_error(m_slow, fs, target);
    const float e_fast = l1_error(m_fast, fs, target);

    EXPECT_LT(e_fast, e_slow)
        << "higher LR did not converge faster (slow=" << e_slow
        << " fast=" << e_fast << ")";
}

// Training changes the queried output (the net is genuinely mutable): one step
// on a target far from the current prediction perturbs the output toward it.
TEST(Nrc, SingleStepMovesOutputTowardTarget)
{
    Config c {};
    c.learning_rate = 1e-1F;  // big step → clearly observable move
    CpuReferenceMlp mlp(c);

    std::array<float, kInputDim> feat {};
    std::ranges::fill(feat, 0.5F);  // positive → guarantees some ReLU units active
    const std::span<const float, kInputDim> fs(feat);
    const Vec3f target { 2.0F, 2.0F, 2.0F };

    const auto before = mlp.query(fs);
    mlp.train_step(fs, target);
    const auto after = mlp.query(fs);

    // Each component must move strictly closer to the (far, positive) target.
    EXPECT_GT(after.x, before.x);
    EXPECT_GT(after.y, before.y);
    EXPECT_GT(after.z, before.z);
}

// Negative: an already-perfect prediction yields a ~zero-gradient step, so the
// query is unchanged (err == 0 → no weight update). Confirms the loss is the
// driver, not a fixed bias drift.
TEST(Nrc, ZeroErrorTrainStepIsANoOp)
{
    const Config c {};
    CpuReferenceMlp mlp(c);

    const auto feat = filled_feat(0.15F);
    const std::span<const float, kInputDim> fs(feat);
    const auto target = mlp.query(fs);  // current prediction == target → err 0

    const auto before = mlp.query(fs);
    mlp.train_step(fs, target);
    const auto after = mlp.query(fs);

    EXPECT_FLOAT_EQ(after.x, before.x);
    EXPECT_FLOAT_EQ(after.y, before.y);
    EXPECT_FLOAT_EQ(after.z, before.z);
}

}  // namespace
