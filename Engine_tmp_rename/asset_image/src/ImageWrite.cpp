// =============================================================================
// CHROMODYNAMIC — cd/asset_image/ImageWrite.cpp
//
// PNG encoder for RGBA8 buffers via stb_image_write. The single-header
// implementation macro must be defined in exactly one TU — separate
// from Image.cpp so the decoder and encoder don't fight over their
// respective IMPLEMENTATION macros and the file scope stays tractable.
//
// Wave 134 / Track A Part 1 — backs cd::asset_image::write_png_rgba
// declared in Image.hpp. The Phase 11 golden-screenshot pipeline calls
// this to write captured swapchain frames to disk.
// =============================================================================
#include <cd/asset_image/Image.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION

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
    #pragma clang diagnostic ignored "-Wmissing-declarations"
    #pragma clang diagnostic ignored "-Wdouble-promotion"
    #pragma clang diagnostic ignored "-Wformat=2"
    #pragma clang diagnostic ignored "-Wformat-nonliteral"
#elif defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wold-style-cast"
    #pragma GCC diagnostic ignored "-Wsign-conversion"
    #pragma GCC diagnostic ignored "-Wconversion"
    #pragma GCC diagnostic ignored "-Wunused-parameter"
    #pragma GCC diagnostic ignored "-Wunused-function"
    #pragma GCC diagnostic ignored "-Wmissing-declarations"
#endif

#include <stb_image_write.h>

#if defined(_MSC_VER)
    #pragma warning(pop)
#elif defined(__clang__)
    #pragma clang diagnostic pop
#elif defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

#include <filesystem>
#include <string>

namespace cd::asset_image
{

cd::core::Result<void>
write_png_rgba(std::string_view path, const std::uint8_t* rgba, std::uint32_t width, std::uint32_t height)
{
    if (rgba == nullptr)
        return std::unexpected(image_errors::make(image_errors::Code::kInvalidArgument, "rgba pointer is null"));
    if (width == 0 || height == 0)
        return std::unexpected(image_errors::make(image_errors::Code::kInvalidArgument, "zero-sized image"));

    // Ensure the parent directory exists — stb_image_write does not
    // create intermediates, and a missing directory just yields a
    // generic "0" return that's hard to diagnose.
    const std::filesystem::path p { std::string { path } };
    if (p.has_parent_path())
    {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        // We don't fail if create_directories errors out (might be a
        // pre-existing directory we can't stat); stbi_write_png will
        // surface a real "can't open" via its return code below.
    }

    // stb takes a stride in bytes of one row. RGBA8 tightly packed.
    constexpr int kChannels = 4;
    const int stride_bytes = static_cast<int>(width) * kChannels;
    const int rc = stbi_write_png(
        p.string().c_str(),
        static_cast<int>(width),
        static_cast<int>(height),
        kChannels,
        rgba,
        stride_bytes
    );
    if (rc == 0)
        return std::unexpected(image_errors::make(
            image_errors::Code::kEncodeFailed,
            "stbi_write_png returned 0 (typically: file open failed or dimensions invalid)"
        ));
    return {};
}

}  // namespace cd::asset_image
