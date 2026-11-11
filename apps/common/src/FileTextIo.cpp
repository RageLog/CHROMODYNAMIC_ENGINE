// =============================================================================
// CHROMODYNAMIC — apps/common/src/FileTextIo.cpp
// =============================================================================
#include <cd/app_common/FileTextIo.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace cd::app_common
{

std::string_view strip_utf8_bom_view(std::string_view text) noexcept
{
    if (text.size() >= 3U &&
        static_cast<unsigned char>(text[0]) == 0xEFU &&
        static_cast<unsigned char>(text[1]) == 0xBBU &&
        static_cast<unsigned char>(text[2]) == 0xBFU)
    {
        return text.substr(3U);
    }
    return text;
}

cd::core::Result<std::string> read_text_file_utf8(std::string_view path)
{
    if (path.empty())
    {
        return std::unexpected(io_errors::make(io_errors::Code::kInvalidPath, "empty path"));
    }

    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    if (!in.is_open())
    {
        return std::unexpected(io_errors::make(io_errors::Code::kOpenFailed, "failed to open text file"));
    }

    std::ostringstream ss;
    ss << in.rdbuf();
    std::string raw = ss.str();
    const std::string_view stripped = strip_utf8_bom_view(raw);
    if (stripped.size() != raw.size())
    {
        raw.erase(0U, 3U);
    }
    return raw;
}

cd::core::Result<void> write_text_file_utf8(std::string_view path, std::string_view content)
{
    if (path.empty())
    {
        return std::unexpected(io_errors::make(io_errors::Code::kInvalidPath, "empty path"));
    }

    const std::filesystem::path fs_path(path);
    if (fs_path.has_parent_path())
    {
        std::error_code ec {};
        std::filesystem::create_directories(fs_path.parent_path(), ec);
    }

    std::ofstream out(fs_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        return std::unexpected(io_errors::make(io_errors::Code::kOpenFailed, "failed to open output text file"));
    }
    if (!content.empty())
    {
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    }
    if (!out.good())
    {
        return std::unexpected(io_errors::make(io_errors::Code::kWriteFailed, "text write failed"));
    }
    return {};
}

cd::core::Result<std::vector<std::uint8_t>> read_binary_file(std::string_view path)
{
    if (path.empty())
    {
        return std::unexpected(io_errors::make(io_errors::Code::kInvalidPath, "empty path"));
    }

    std::ifstream in(std::filesystem::path(path), std::ios::binary | std::ios::ate);
    if (!in.is_open())
    {
        return std::unexpected(io_errors::make(io_errors::Code::kOpenFailed, "failed to open binary file"));
    }

    const std::streampos end_pos = in.tellg();
    if (end_pos < 0)
    {
        return std::unexpected(io_errors::make(io_errors::Code::kOpenFailed, "failed to stat binary file"));
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end_pos));
    in.seekg(0, std::ios::beg);
    if (!bytes.empty())
    {
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    return bytes;
}

cd::core::Result<void> write_binary_file(std::string_view path, std::span<const std::byte> bytes)
{
    if (path.empty())
    {
        return std::unexpected(io_errors::make(io_errors::Code::kInvalidPath, "empty path"));
    }

    const std::filesystem::path fs_path(path);
    if (fs_path.has_parent_path())
    {
        std::error_code ec {};
        std::filesystem::create_directories(fs_path.parent_path(), ec);
    }

    std::ofstream out(fs_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        return std::unexpected(io_errors::make(io_errors::Code::kOpenFailed, "failed to open output binary file"));
    }
    if (!bytes.empty())
    {
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    if (!out.good())
    {
        return std::unexpected(io_errors::make(io_errors::Code::kWriteFailed, "binary write failed"));
    }
    return {};
}

}  // namespace cd::app_common
