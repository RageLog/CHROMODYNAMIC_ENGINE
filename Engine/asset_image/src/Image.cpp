// =============================================================================
// CHROMODYNAMIC — cd/asset_image/Image.cpp
// =============================================================================
#include <cd/asset_image/Image.hpp>

// stb_image is single-header; define the implementation macro in exactly
// this TU. The SYSTEM include in CMakeLists keeps -Werror from tripping
// on stb's relaxed warning posture.
#define STB_IMAGE_IMPLEMENTATION

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4100 4244 4267 4456 4505 4996)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wconversion"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#include <stb_image.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <cstring>
#include <filesystem>
#include <limits>
#include <string>

namespace cd::asset_image
{

namespace
{

/// stbi exposes a global flag for vertical flip. We toggle it per-call,
/// then restore — that keeps the function reentrant relative to other
/// callers that may have set their own preference.
struct StbiFlipScope
{
    int previous;
    explicit StbiFlipScope(bool flip) noexcept
        : previous { stbi__vertically_flip_on_load_global }
    {
        stbi_set_flip_vertically_on_load(flip ? 1 : 0);
    }
    ~StbiFlipScope()
    {
        stbi_set_flip_vertically_on_load(previous);
    }
    StbiFlipScope(const StbiFlipScope&) = delete;
    StbiFlipScope& operator=(const StbiFlipScope&) = delete;
    StbiFlipScope(StbiFlipScope&&) = delete;
    StbiFlipScope& operator=(StbiFlipScope&&) = delete;
};

}  // namespace

cd::core::Result<Image> load_image(std::string_view path, const LoadOptions& options)
{
    if (path.empty())
    {
        return std::unexpected(image_errors::make(image_errors::Code::kInvalidArgument, "empty path"));
    }
    const std::string p { path };
    if (!std::filesystem::exists(p))
    {
        return std::unexpected(image_errors::make(image_errors::Code::kFileNotFound, p));
    }

    StbiFlipScope flip_scope { options.flip_vertical };

    int w = 0;
    int h = 0;
    int src_comp = 0;
    // Request 4 channels so callers always see RGBA, regardless of source.
    std::uint8_t* data = stbi_load(p.c_str(), &w, &h, &src_comp, /*desired_channels=*/4);
    if (data == nullptr)
    {
        const char* reason = stbi_failure_reason();
        return std::unexpected(
            image_errors::make(image_errors::Code::kDecodeFailed, reason != nullptr ? reason : "stbi_load failed")
        );
    }

    Image out;
    out.width = static_cast<std::uint32_t>(w);
    out.height = static_cast<std::uint32_t>(h);
    out.has_alpha = (src_comp == 4 || src_comp == 2);  // RGBA / GA carry alpha
    const std::size_t byte_count = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4U;
    out.rgba.resize(byte_count);
    std::memcpy(out.rgba.data(), data, byte_count);
    stbi_image_free(data);
    return out;
}

cd::core::Result<Image> load_image_from_memory(std::span<const std::uint8_t> bytes, const LoadOptions& options)
{
    if (bytes.empty())
    {
        return std::unexpected(image_errors::make(image_errors::Code::kInvalidArgument, "empty buffer"));
    }
    StbiFlipScope flip_scope { options.flip_vertical };

    int w = 0;
    int h = 0;
    int src_comp = 0;
    // stbi takes int length — guard against >2 GB inputs (the cast would be UB).
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        return std::unexpected(image_errors::make(image_errors::Code::kInvalidArgument, "input > 2 GB"));
    }
    std::uint8_t* data = stbi_load_from_memory(
        bytes.data(),
        static_cast<int>(bytes.size()),
        &w,
        &h,
        &src_comp,
        /*desired_channels=*/4
    );
    if (data == nullptr)
    {
        const char* reason = stbi_failure_reason();
        return std::unexpected(
            image_errors::make(image_errors::Code::kDecodeFailed, reason != nullptr ? reason : "stbi_load_from_memory")
        );
    }

    Image out;
    out.width = static_cast<std::uint32_t>(w);
    out.height = static_cast<std::uint32_t>(h);
    out.has_alpha = (src_comp == 4 || src_comp == 2);
    const std::size_t byte_count = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4U;
    out.rgba.resize(byte_count);
    std::memcpy(out.rgba.data(), data, byte_count);
    stbi_image_free(data);
    return out;
}

cd::core::Result<std::vector<Image>> generate_mips(const Image& src, std::uint32_t levels)
{
    if (src.width == 0 || src.height == 0)
    {
        return std::unexpected(image_errors::make(image_errors::Code::kInvalidArgument, "zero source dimensions"));
    }
    if (src.rgba.size() < static_cast<std::size_t>(src.width) * src.height * 4U)
    {
        return std::unexpected(image_errors::make(image_errors::Code::kInvalidArgument, "src.rgba too small"));
    }

    std::vector<Image> chain;
    chain.push_back(src);  // mip 0 = source

    // Compute total possible mip count when levels == 0: log2 of the
    // larger dim plus 1, so a 256-wide image gets 9 mips (256, 128, ...,
    // 1). When levels > 0 it caps the chain at that number.
    std::uint32_t cap = levels;
    if (cap == 0)
    {
        std::uint32_t dim = std::max(src.width, src.height);
        cap = 1;
        while (dim > 1)
        {
            dim >>= 1U;
            ++cap;
        }
    }

    for (std::uint32_t lvl = 1; lvl < cap; ++lvl)
    {
        const auto& prev = chain.back();
        const std::uint32_t w = std::max(prev.width >> 1U, 1U);
        const std::uint32_t h = std::max(prev.height >> 1U, 1U);
        Image next;
        next.width = w;
        next.height = h;
        next.has_alpha = prev.has_alpha;
        next.rgba.resize(static_cast<std::size_t>(w) * h * 4U);

        for (std::uint32_t y = 0; y < h; ++y)
        {
            const std::uint32_t sy0 = y * 2U;
            const std::uint32_t sy1 = std::min(sy0 + 1U, prev.height - 1U);
            for (std::uint32_t x = 0; x < w; ++x)
            {
                const std::uint32_t sx0 = x * 2U;
                const std::uint32_t sx1 = std::min(sx0 + 1U, prev.width - 1U);

                auto px = [&](std::uint32_t sx, std::uint32_t sy) noexcept -> const std::uint8_t*
                {
                    return prev.rgba.data() + (static_cast<std::size_t>(sy) * prev.width + sx) * 4U;
                };
                const auto* p00 = px(sx0, sy0);
                const auto* p10 = px(sx1, sy0);
                const auto* p01 = px(sx0, sy1);
                const auto* p11 = px(sx1, sy1);

                auto* dst = next.rgba.data() + (static_cast<std::size_t>(y) * w + x) * 4U;
                for (std::size_t c = 0; c < 4; ++c)
                {
                    // +2 rounding bias avoids 0.5-truncation darkening.
                    const std::uint32_t sum = static_cast<std::uint32_t>(p00[c]) + p10[c] + p01[c] + p11[c] + 2U;
                    dst[c] = static_cast<std::uint8_t>(sum / 4U);
                }
            }
        }

        chain.push_back(std::move(next));
        if (w == 1 && h == 1)
            break;  // reached 1x1 — no more mips possible
    }

    return chain;
}

cd::core::Result<ImageHdr> load_image_hdr(std::string_view path, const LoadOptions& options)
{
    if (path.empty())
    {
        return std::unexpected(image_errors::make(image_errors::Code::kInvalidArgument, "empty path"));
    }
    const std::string p { path };
    if (!std::filesystem::exists(p))
    {
        return std::unexpected(image_errors::make(image_errors::Code::kFileNotFound, p));
    }

    StbiFlipScope flip_scope { options.flip_vertical };

    int w = 0;
    int h = 0;
    int src_comp = 0;
    float* data = stbi_loadf(p.c_str(), &w, &h, &src_comp, /*desired_channels=*/4);
    if (data == nullptr)
    {
        const char* reason = stbi_failure_reason();
        return std::unexpected(
            image_errors::make(image_errors::Code::kDecodeFailed, reason != nullptr ? reason : "stbi_loadf")
        );
    }

    ImageHdr out;
    out.width = static_cast<std::uint32_t>(w);
    out.height = static_cast<std::uint32_t>(h);
    const std::size_t float_count = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4U;
    out.rgba.resize(float_count);
    std::memcpy(out.rgba.data(), data, float_count * sizeof(float));
    stbi_image_free(data);
    return out;
}

}  // namespace cd::asset_image
