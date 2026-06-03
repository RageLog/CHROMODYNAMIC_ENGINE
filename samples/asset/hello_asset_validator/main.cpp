// =============================================================================
// CHROMODYNAMIC — samples/asset/hello_asset_validator
// Phase 637 — console proof that cd::asset::validator is consumable.
//
// Demonstrates cd::asset::validator::Validator:
//   1. Configure a ValidatorPolicy (tight 4 MiPixel texture limit).
//   2. validate_texture_blob on an EMPTY blob         -> kError expected.
//   3. validate_texture_blob on a TOO-LARGE blob      -> kWarning expected.
//   4. validate_texture_blob on a valid synthetic PNG -> no issues expected.
//
// Prints every ValidationIssue (severity + path + message).
// Always exits 0.
// =============================================================================
#include <cd/asset/validator/Validator.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string_view>
#include <vector>

namespace
{

// ---- helpers -----------------------------------------------------------------

std::string_view severity_str(cd::asset::validator::Severity s) noexcept
{
    using cd::asset::validator::Severity;
    switch (s)
    {
        case Severity::kInfo:    return "INFO";
        case Severity::kWarning: return "WARNING";
        case Severity::kError:   return "ERROR";
    }
    return "UNKNOWN";
}

void print_issues(const std::vector<cd::asset::validator::ValidationIssue>& issues,
                  std::string_view                                           label)
{
    std::printf("  [%s]\n", std::string{ label }.c_str());
    if (issues.empty())
    {
        std::printf("    (no issues)\n");
        return;
    }
    for (const auto& issue : issues)
    {
        std::printf("    %s  path=\"%s\"  msg=\"%s\"\n",
                    severity_str(issue.severity).data(),
                    issue.asset_path.c_str(),
                    issue.message.c_str());
    }
}

// ---- synthetic blobs ---------------------------------------------------------

// (a) Empty blob — nothing at all.
// (b) Too-large: a blob whose byte count / 4 > max_texture_pixels (4 Mi).
//     4 Mi pixels * 4 bytes/pixel = 16 MiB. We build 16 MiB + 4 bytes starting
//     with a valid PNG signature so only the oversized warning fires, not the
//     format error.
// (c) Valid synthetic PNG: just the 8-byte PNG signature — small and valid.

std::vector<std::uint8_t> make_too_large_png_blob()
{
    // Policy: max_texture_pixels = 4 * 1024 * 1024 (set below).
    // Threshold byte size for warning: max_texture_pixels * 4 bytes/pixel = 16 MiB.
    // We exceed by 4 bytes.
    constexpr std::size_t k_threshold_bytes = 4ULL * 1024ULL * 1024ULL * 4ULL + 4ULL;
    std::vector<std::uint8_t> blob(k_threshold_bytes, 0x00U);

    // Insert PNG magic so format detection passes.
    blob[0] = 0x89U;
    blob[1] = 'P';
    blob[2] = 'N';
    blob[3] = 'G';
    blob[4] = '\r';
    blob[5] = '\n';
    blob[6] = 0x1AU;
    blob[7] = '\n';

    return blob;
}

std::vector<std::uint8_t> make_valid_png_blob()
{
    // Minimal valid PNG blob: 8-byte PNG signature.
    // The validator only checks magic + size limit; 8 bytes is well within limits.
    return {
        0x89U, 'P', 'N', 'G', '\r', '\n', 0x1AU, '\n'
    };
}

}  // namespace

// ---- main --------------------------------------------------------------------

int main()
{
    std::printf("=== hello_asset_validator — cd::asset::validator demo ===\n\n");

    // Configure a Validator with a tight 4 MiPixel texture limit.
    cd::asset::validator::Validator validator;
    validator.configure(cd::asset::validator::ValidatorPolicy{
        .max_texture_pixels = 4ULL * 1024ULL * 1024ULL,  // 4 MiPixels (tight)
        .max_mesh_vertices  = 1024ULL * 1024ULL           // default 1 MiVerts
    });

    std::printf("Policy: max_texture_pixels=%llu  max_mesh_vertices=%llu\n\n",
                static_cast<unsigned long long>(validator.policy().max_texture_pixels),
                static_cast<unsigned long long>(validator.policy().max_mesh_vertices));

    // ---- Case (a): empty blob ------------------------------------------------
    {
        std::printf("Case (a): empty blob\n");
        const std::vector<std::uint8_t> empty_blob;
        const auto issues = validator.validate_texture_blob(
            std::span<const std::uint8_t>{ empty_blob },
            "synthetic/empty.png"
        );
        print_issues(issues, "empty blob -> expect kError");
        std::printf("\n");
    }

    // ---- Case (b): too-large blob --------------------------------------------
    {
        std::printf("Case (b): too-large blob (16 MiB + 4 bytes, valid PNG magic)\n");
        const auto large_blob = make_too_large_png_blob();
        const auto issues = validator.validate_texture_blob(
            std::span<const std::uint8_t>{ large_blob },
            "synthetic/huge_4k_atlas.png"
        );
        print_issues(issues, "too-large blob -> expect kWarning");
        std::printf("\n");
    }

    // ---- Case (c): valid synthetic PNG ---------------------------------------
    {
        std::printf("Case (c): valid synthetic PNG (8-byte signature)\n");
        const auto png_blob = make_valid_png_blob();
        const auto issues = validator.validate_texture_blob(
            std::span<const std::uint8_t>{ png_blob },
            "synthetic/valid_albedo.png"
        );
        print_issues(issues, "valid PNG -> expect no issues");
        std::printf("\n");
    }

    std::printf("=== done ===\n");
    return 0;
}
