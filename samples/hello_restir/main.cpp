// =============================================================================
// CHROMODYNAMIC — samples/hello_restir
//
// CPU-reference smoke for cd::restir_di + cd::restir_gi (Bitterli 2020
// + Ouyang 2021 reservoir sampling). Builds a synthetic 32-candidate
// stream, runs WRS into a Reservoir, verifies:
//   * the survivor's target_pdf bias correlates with the input PDF
//     distribution (high-PDF candidates win more often).
//   * temporal reuse via clamp_history caps M correctly.
//   * the GI reservoir mirrors the DI one with a bounce sample slot.
// =============================================================================
#include <cd/core/Version.hpp>
#include <cd/math/Random.hpp>
#include <cd/restir_di/Reservoir.hpp>
#include <cd/restir_gi/GiReservoir.hpp>

#include <array>
#include <cstdio>

int main()
{
    std::printf("CHROMODYNAMIC %u.%u.%u — hello_restir\n",
                static_cast<unsigned>(cd::core::kEngineVersion.major),
                static_cast<unsigned>(cd::core::kEngineVersion.minor),
                static_cast<unsigned>(cd::core::kEngineVersion.patch));

    // -------- DI reservoir test --------
    cd::restir_di::Reservoir r {};
    cd::math::Random rng { 0xBADF00Du };

    // 32 candidates: half have low PDF (0.1), half high (0.9). With WRS
    // the survivor should be the high-PDF group most of the time.
    constexpr std::uint32_t kCandidates = 32;
    std::uint32_t hi_wins = 0;
    constexpr std::uint32_t kTrials = 200;
    for (std::uint32_t trial = 0; trial < kTrials; ++trial)
    {
        cd::restir_di::Reservoir t {};
        for (std::uint32_t i = 0; i < kCandidates; ++i)
        {
            cd::restir_di::Sample s {};
            const bool hi = (i & 1u) != 0u;
            s.light_index = i;
            s.target_pdf  = hi ? 0.9F : 0.1F;
            s.radiance    = { s.target_pdf, s.target_pdf, s.target_pdf };
            cd::restir_di::update(t, s, s.target_pdf, rng.next_float());
        }
        if ((t.selected.light_index & 1u) != 0u) ++hi_wins;
    }
    const float hi_ratio = static_cast<float>(hi_wins) / static_cast<float>(kTrials);
    std::printf("  DI: %u trials, high-PDF wins %.1f%% (expect > 70%%)\n",
                kTrials, static_cast<double>(hi_ratio * 100.0F));
    if (hi_ratio < 0.65F)
    {
        std::printf("FAIL — WRS not biased correctly\n");
        return 1;
    }

    // Temporal clamp: M should be capped at 20.
    r.weight_sum = 100.0F;
    r.M = 200;
    cd::restir_di::clamp_history(r, 20);
    if (r.M != 20 || std::abs(r.weight_sum - 10.0F) > 1e-4F)
    {
        std::printf("FAIL — clamp_history math (M=%u w=%.2f)\n",
                    r.M, static_cast<double>(r.weight_sum));
        return 2;
    }
    std::printf("  DI: clamp_history(200, 20) → M=20, weight scaled correctly OK\n");

    // -------- GI reservoir test (mirror shape) --------
    cd::restir_gi::Reservoir gi {};
    cd::restir_gi::Sample gs {};
    gs.point    = { 2.0F, 3.0F, -1.0F };
    gs.normal   = { 0.0F, 1.0F, 0.0F };
    gs.incoming = { 0.3F, 0.5F, 0.7F };
    gs.valid    = 1;
    cd::restir_gi::update(gi, gs, 0.5F, 0.5F);
    const float fw = gi.final_weight(0.5F);
    if (gi.M != 1 || std::abs(fw - 1.0F) > 1e-4F)
    {
        std::printf("FAIL — GI reservoir not updating (M=%u, final_w=%.4f)\n",
                    gi.M, static_cast<double>(fw));
        return 3;
    }
    std::printf("  GI: single-sample reservoir update + final_weight OK\n");

    std::printf("[hello_restir] PARITY OK\n");
    return 0;
}
