#include <cd/nrc/Nrc.hpp>

#include <gtest/gtest.h>

#include <array>

namespace
{

using cd::nrc::Config;
using cd::nrc::CpuReferenceMlp;
using cd::nrc::kInputDim;

TEST(Nrc, ConstructionDoesNotCrash)
{
    Config c {};
    CpuReferenceMlp mlp(c);
    SUCCEED();
}

TEST(Nrc, QueryReturnsFiniteValues)
{
    Config c {};
    CpuReferenceMlp mlp(c);
    std::array<float, kInputDim> feat {};
    for (auto& v : feat) v = 0.1F;
    const auto out = mlp.query(std::span<const float, kInputDim>(feat));
    EXPECT_TRUE(std::isfinite(out.x));
    EXPECT_TRUE(std::isfinite(out.y));
    EXPECT_TRUE(std::isfinite(out.z));
}

// L1 error of the MLP against `target` for a fixed feature vector.
[[nodiscard]] float l1_error(const CpuReferenceMlp& mlp,
                             std::span<const float, kInputDim> feat,
                             const cd::math::Vec3f& target)
{
    const auto p = mlp.query(feat);
    return std::abs(p.x - target.x) + std::abs(p.y - target.y) +
           std::abs(p.z - target.z);
}

// The original test allowed zero gain (err1 < err0 + eps). A research
// skeleton that does not actually learn would pass that. This version
// asserts the single-hidden-layer SGD MLP STRICTLY reduces L1 error on a
// fittable constant target — at least a 50% reduction — and ends below an
// absolute floor. Fail-on-revert: a broken backward pass (or a no-op
// train_step) leaves err1 ≈ err0 and trips both bounds.
TEST(Nrc, TrainingStrictlyReducesErrorOnFittableTarget)
{
    Config c {};
    c.learning_rate = 1e-2F;
    CpuReferenceMlp mlp(c);

    std::array<float, kInputDim> feat {};
    for (std::size_t i = 0; i < feat.size(); ++i)
        feat[i] = (i % 3 == 0) ? 1.0F : 0.0F;
    const std::span<const float, kInputDim> feat_span(feat);
    const cd::math::Vec3f target { 0.5F, 0.5F, 0.5F };

    const float err0 = l1_error(mlp, feat_span, target);
    for (int i = 0; i < 400; ++i)
        mlp.train_step(feat_span, target);
    const float err1 = l1_error(mlp, feat_span, target);

    // Strict learning: error must shrink, and substantially.
    EXPECT_LT(err1, err0)        << "MLP failed to reduce error at all";
    EXPECT_LT(err1, err0 * 0.5F) << "MLP reduced error < 50% — backward pass weak/broken";
    EXPECT_LT(err1, 0.05F)       << "MLP did not converge near the fittable target";
}

}  // namespace
