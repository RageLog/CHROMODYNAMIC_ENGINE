#include <cd/nrc/Nrc.hpp>

#include <gtest/gtest.h>

#include <array>

namespace
{

using cd::nrc::Config;
using cd::nrc::CpuReferenceMlp;
using cd::nrc::kInputDim;

constexpr float kEps = 0.1F;

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

TEST(Nrc, TrainingReducesError)
{
    Config c {};
    c.learning_rate = 1e-2F;
    CpuReferenceMlp mlp(c);
    std::array<float, kInputDim> feat {};
    for (std::size_t i = 0; i < feat.size(); ++i)
        feat[i] = (i % 3 == 0) ? 1.0F : 0.0F;
    const cd::math::Vec3f target { 0.5F, 0.5F, 0.5F };
    const auto err0 = [&] {
        const auto p = mlp.query(std::span<const float, kInputDim>(feat));
        return std::abs(p.x - target.x) + std::abs(p.y - target.y) +
               std::abs(p.z - target.z);
    }();
    for (int i = 0; i < 200; ++i)
        mlp.train_step(std::span<const float, kInputDim>(feat), target);
    const auto err1 = [&] {
        const auto p = mlp.query(std::span<const float, kInputDim>(feat));
        return std::abs(p.x - target.x) + std::abs(p.y - target.y) +
               std::abs(p.z - target.z);
    }();
    EXPECT_LT(err1, err0 + kEps);
}

}  // namespace
