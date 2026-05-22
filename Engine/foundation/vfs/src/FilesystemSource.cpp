// =============================================================================
// CHROMODYNAMIC — cd/vfs/FilesystemSource.cpp
// =============================================================================
#include <cd/vfs/FilesystemSource.hpp>

#include <cstdint>
#include <fstream>
#include <ios>
#include <utility>

namespace cd::vfs
{

FilesystemSource::FilesystemSource(std::filesystem::path root, std::string name)
    : root_ { std::move(root) }
    , name_ { std::move(name) }
{
}

std::filesystem::path FilesystemSource::resolve(std::string_view path) const
{
    return root_ / std::filesystem::path { path };
}

bool FilesystemSource::exists(std::string_view path) const
{
    std::error_code ec;
    return std::filesystem::is_regular_file(resolve(path), ec);
}

cd::core::Result<std::vector<std::byte>> FilesystemSource::read(std::string_view path) const
{
    const auto abs = resolve(path);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(abs, ec))
    {
        return std::unexpected(vfs_errors::make(vfs_errors::Code::kNotFound, "fs: path not found"));
    }
    std::ifstream in { abs, std::ios::binary };
    if (!in)
    {
        return std::unexpected(vfs_errors::make(vfs_errors::Code::kIoFailed, "fs: open failed"));
    }
    in.seekg(0, std::ios::end);
    const auto size = static_cast<std::streamsize>(in.tellg());
    in.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(size > 0 ? static_cast<std::size_t>(size) : 0);
    if (size > 0)
    {
        in.read(reinterpret_cast<char*>(bytes.data()), size);
        if (!in)
        {
            return std::unexpected(vfs_errors::make(vfs_errors::Code::kIoFailed, "fs: read failed"));
        }
    }
    return bytes;
}

std::vector<std::string> FilesystemSource::list(std::string_view prefix) const
{
    std::vector<std::string> out;
    std::error_code ec;
    if (!std::filesystem::is_directory(root_, ec))
        return out;
    for (auto it = std::filesystem::recursive_directory_iterator { root_, ec };
         it != std::filesystem::recursive_directory_iterator {};
         it.increment(ec))
    {
        if (ec)
            break;
        if (!it->is_regular_file(ec))
            continue;
        std::error_code rel_ec;
        auto rel = std::filesystem::relative(it->path(), root_, rel_ec);
        if (rel_ec)
            continue;
        auto s = rel.generic_string();
        if (prefix.empty() || (s.size() >= prefix.size() && std::string_view { s.data(), prefix.size() } == prefix))
        {
            out.push_back(std::move(s));
        }
    }
    return out;
}

}  // namespace cd::vfs
