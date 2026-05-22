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
