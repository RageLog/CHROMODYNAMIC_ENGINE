// =============================================================================
// CHROMODYNAMIC — engine/asset/validator/src/Validator.cpp
// Phase 611 — cd::asset::validator implementation (Sprint-1)
// =============================================================================

#include <cd/asset/validator/Validator.hpp>

#include <algorithm>
#include <cstring>

namespace cd::asset::validator
{

// ---- helpers ----------------------------------------------------------------

namespace
{

/// Append a kError issue to `issues`.
void push_error(std::vector<ValidationIssue>& issues,
                std::string_view              path,
                std::string                   message)
{
    issues.push_back({ Severity::kError, std::string{ path }, std::move(message) });
}

/// Append a kWarning issue to `issues`.
void push_warning(std::vector<ValidationIssue>& issues,
                  std::string_view              path,
                  std::string                   message)
{
    issues.push_back({ Severity::kWarning, std::string{ path }, std::move(message) });
}

/// Return true if `blob` starts with `magic` bytes.
bool starts_with_magic(std::span<const std::uint8_t> blob,
                       std::span<const std::uint8_t> magic) noexcept
{
    if (blob.size() < magic.size())
    {
        return false;
    }
    return std::equal(magic.begin(), magic.end(), blob.begin());
}

/// Convenience: magic from a string literal (excluding NUL terminator).
template <std::size_t N>
bool starts_with_magic(std::span<const std::uint8_t> blob,
                       const char (&literal)[N]) noexcept
{
    // N includes the NUL terminator; use N-1 bytes.
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(literal);
    return starts_with_magic(blob,
                             std::span<const std::uint8_t>{ bytes, N - 1U });
}

/// Read a little-endian uint32 at `offset` (caller must verify bounds).
[[nodiscard]] std::uint32_t
read_u32_le(std::span<const std::uint8_t> blob, std::size_t offset) noexcept
{
    std::uint32_t v{};
    std::memcpy(&v, blob.data() + offset, sizeof(v));
    return v;
}

}  // namespace

// ---- Validator::configure ---------------------------------------------------

void Validator::configure(const ValidatorPolicy& policy)
{
    policy_ = policy;
}

// ---- Validator::policy ------------------------------------------------------

const ValidatorPolicy& Validator::policy() const noexcept
{
    return policy_;
}

// ---- Validator::validate_texture_blob ---------------------------------------

std::vector<ValidationIssue>
Validator::validate_texture_blob(std::span<const std::uint8_t> blob,
                                 std::string_view              path) const
{
    std::vector<ValidationIssue> issues;

    // ---- T1: non-empty -------------------------------------------------------
    if (blob.empty())
    {
        push_error(issues, path, "texture blob is empty");
        return issues;
    }

    // ---- T2: magic header check ---------------------------------------------
    // PNG: \x89 P N G \r \n \x1a \n  (8 bytes)
    constexpr std::array<std::uint8_t, 4> k_png_magic{ 0x89U, 'P', 'N', 'G' };
    // JPEG: FF D8
    constexpr std::array<std::uint8_t, 2> k_jpg_magic{ 0xFFU, 0xD8U };
    // KTX2: 12-byte identifier (first 4 bytes are \xABKTX)
    constexpr std::array<std::uint8_t, 4> k_ktx2_magic{ 0xABU, 'K', 'T', 'X' };
    // DDS: "DDS "
    constexpr std::array<std::uint8_t, 4> k_dds_magic{ 'D', 'D', 'S', ' ' };
    // HDR (Radiance RGBE): "#?RADIANCE"
    // We check 2 bytes "#?" as a lightweight identifier.
    constexpr std::array<std::uint8_t, 2> k_hdr_magic{ '#', '?' };

    const bool is_png  = starts_with_magic(blob, std::span<const std::uint8_t>{ k_png_magic });
    const bool is_jpg  = starts_with_magic(blob, std::span<const std::uint8_t>{ k_jpg_magic });
    const bool is_ktx2 = starts_with_magic(blob, std::span<const std::uint8_t>{ k_ktx2_magic });
    const bool is_dds  = starts_with_magic(blob, std::span<const std::uint8_t>{ k_dds_magic });
    const bool is_hdr  = starts_with_magic(blob, std::span<const std::uint8_t>{ k_hdr_magic });

    const bool known_format = is_png || is_jpg || is_ktx2 || is_dds || is_hdr;
    if (!known_format)
    {
        push_error(issues, path,
                   "unrecognised texture format: magic header does not match "
                   "PNG, JPEG, KTX2, DDS, or HDR");
    }

    // ---- T3: conservative oversized check -----------------------------------
    // blob.size() / 4 bytes-per-pixel is a lower-bound pixel estimate.
    // If even that lower bound exceeds the policy limit, warn.
    const std::uint64_t conservative_pixels =
        static_cast<std::uint64_t>(blob.size()) / 4ULL;
    if (conservative_pixels > policy_.max_texture_pixels)
    {
        push_warning(issues, path,
                     "texture blob may exceed max_texture_pixels policy limit "
                     "(conservative estimate based on blob byte size)");
    }

    return issues;
}

// ---- Validator::validate_gltf_blob ------------------------------------------

std::vector<ValidationIssue>
Validator::validate_gltf_blob(std::span<const std::uint8_t> blob,
                              std::string_view              path) const
{
    std::vector<ValidationIssue> issues;

    // ---- T1: non-empty -------------------------------------------------------
    if (blob.empty())
    {
        push_error(issues, path, "glTF blob is empty");
        return issues;
    }

    // ---- T2: detect GLB vs JSON .gltf ---------------------------------------
    // GLB magic: 0x67 0x6C 0x54 0x46 ("glTF")
    constexpr std::array<std::uint8_t, 4> k_glb_magic{ 0x67U, 0x6CU, 0x54U, 0x46U };
    const bool is_glb = starts_with_magic(blob, std::span<const std::uint8_t>{ k_glb_magic });

    if (is_glb)
    {
        // GLB: minimum header is 12 bytes (magic + version + length).
        if (blob.size() < 12UZ)
        {
            push_error(issues, path, "GLB blob too small: header incomplete (< 12 bytes)");
            return issues;
        }

        // Version field at offset 4 must be 2 for glTF 2.0.
        const std::uint32_t glb_version = read_u32_le(blob, 4UZ);
        if (glb_version != 2U)
        {
            push_warning(issues, path,
                         "GLB version field is not 2 — may be unsupported glTF version");
        }
    }
    else
    {
        // JSON .gltf: skip leading whitespace then expect '{'.
        const auto first_non_ws = std::find_if(
            blob.begin(), blob.end(),
            [](const std::uint8_t b) noexcept
            {
                return b != static_cast<std::uint8_t>(' ')  &&
                       b != static_cast<std::uint8_t>('\t') &&
                       b != static_cast<std::uint8_t>('\r') &&
                       b != static_cast<std::uint8_t>('\n');
            });

        if (first_non_ws == blob.end() ||
            *first_non_ws != static_cast<std::uint8_t>('{'))
        {
            push_error(issues, path,
                       "glTF JSON blob does not begin with '{' — not valid JSON glTF "
                       "and does not carry GLB magic header");
        }
    }

    return issues;
}

// ---- Validator::validate_audio_blob -----------------------------------------

std::vector<ValidationIssue>
Validator::validate_audio_blob(std::span<const std::uint8_t> blob,
                               std::string_view              path) const
{
    std::vector<ValidationIssue> issues;

    // ---- T1: non-empty -------------------------------------------------------
    if (blob.empty())
    {
        push_error(issues, path, "audio blob is empty");
        return issues;
    }

    // ---- T2: WAV detection --------------------------------------------------
    // RIFF/WAVE: "RIFF" at offset 0, "WAVE" at offset 8 (minimum 12 bytes).
    constexpr std::array<std::uint8_t, 4> k_riff{ 'R', 'I', 'F', 'F' };
    constexpr std::array<std::uint8_t, 4> k_wave{ 'W', 'A', 'V', 'E' };
    constexpr std::array<std::uint8_t, 4> k_ogg { 'O', 'g', 'g', 'S' };

    const bool has_riff = starts_with_magic(blob, std::span<const std::uint8_t>{ k_riff });
    const bool has_wave = (blob.size() >= 12UZ) &&
                          starts_with_magic(blob.subspan(8UZ, 4UZ),
                                            std::span<const std::uint8_t>{ k_wave });
    const bool is_wav   = has_riff && has_wave;

    // ---- T3: OGG detection --------------------------------------------------
    const bool is_ogg = starts_with_magic(blob, std::span<const std::uint8_t>{ k_ogg });

    if (!is_wav && !is_ogg)
    {
        if (has_riff && !has_wave)
        {
            push_error(issues, path,
                       "blob starts with RIFF but missing WAVE tag at offset 8 — "
                       "not a valid WAV file");
        }
        else
        {
            push_error(issues, path,
                       "unrecognised audio format: magic header does not match "
                       "RIFF/WAVE (WAV) or OggS (OGG)");
        }
    }

    return issues;
}

}  // namespace cd::asset::validator
