// =============================================================================
// CHROMODYNAMIC — samples/hello_nrc
//
// CPU-reference smoke for cd::nrc (Neural Radiance Cache, Müller 2021).
// Instantiates the CpuReferenceMlp shipped by the lib, runs a handful of
// inference queries, performs a tiny training loop to fit a constant
// radiance, then verifies the MLP converges (loss drops). This validates
// the *shape* of the API — production swaps in TinyCudaNN behind the
// same query/train_step interface.
// =============================================================================
#include <cd/core/Version.hpp>
#include <cd/nrc/Nrc.hpp>

#include <array>
#include <cmath>
#include <cstdio>

int main()
{
    std::printf("CHROMODYNAMIC %u.%u.%u — hello_nrc\n",
                static_cast<unsigned>(cd::core::kEngineVersion.major),
                static_cast<unsigned>(cd::core::kEngineVersion.minor),
                static_cast<unsigned>(cd::core::kEngineVersion.patch));

    cd::nrc::Config cfg {};
    cfg.hidden_layers = 1;        // CPU ref is single-hidden
    cfg.hidden_width  = 64;
    cfg.learning_rate = 5e-3F;
    cd::nrc::CpuReferenceMlp mlp { cfg };

    // Fixed input features (32 floats) — frequency-encoded position +
    // direction + roughness. Same vector for every train+query so the
    // network is fitting a constant function.
    std::array<float, cd::nrc::kInputDim> feat {};
    for (std::size_t i = 0; i < feat.size(); ++i)
        feat[i] = 0.1F * static_cast<float>(i % 4) - 0.15F;

    const cd::math::Vec3f target { 0.4F, 0.6F, 0.8F };

    // Initial loss.
    auto loss_fn = [&]() {
        const auto pred = mlp.query(feat);
        const float dx = pred.x - target.x;
        const float dy = pred.y - target.y;
        const float dz = pred.z - target.z;
        return dx * dx + dy * dy + dz * dz;
    };
    const float loss_before = loss_fn();
    std::printf("  initial loss = %.4f\n", static_cast<double>(loss_before));

    // 200 SGD steps.
    for (int i = 0; i < 200; ++i) mlp.train_step(feat, target);
    const float loss_after = loss_fn();
    std::printf("  loss after 200 steps = %.4f  (%.0f%% reduction)\n",
                static_cast<double>(loss_after),
                static_cast<double>(100.0F * (1.0F - loss_after / std::max(loss_before, 1e-6F))));

    if (loss_after >= loss_before * 0.5F)
    {
        std::printf("FAIL — MLP did not converge\n");
        return 1;
    }
    const auto final_pred = mlp.query(feat);
    std::printf("  pred=(%.3f, %.3f, %.3f)  target=(%.3f, %.3f, %.3f)\n",
                static_cast<double>(final_pred.x),
                static_cast<double>(final_pred.y),
                static_cast<double>(final_pred.z),
                static_cast<double>(target.x),
                static_cast<double>(target.y),
                static_cast<double>(target.z));

    std::printf("[hello_nrc] PARITY OK\n");
    return 0;
}
