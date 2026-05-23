// =============================================================================
// CHROMODYNAMIC — cd/shader/SpirvFile.cpp
//
// `load_spirv_file` — always-available helper that ships even when the
// glslang backend is disabled (CD_ENABLE_GLSLANG=OFF), so consumers can
// always load pre-compiled .spv blobs without dragging in a compiler.
// =============================================================================
#include <cd/shader/Compiler.hpp>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <ios>
#include <string>
#include <vector>

namespace cd::shader
{

cd::core::Result<std::vector<std::uint32_t>> load_spirv_file(std::string_view path)
{
    if (path.empty())
    {
        return std::unexpected(shader_errors::make(shader_errors::Code::kInvalidArgument, "empty path"));
    }
    // std::ifstream wants a NUL-terminated string; copy into a temporary.
    const std::string path_str { path };
    std::ifstream in(path_str, std::ios::binary | std::ios::ate);
    if (!in.is_open())
    {
        return std::unexpected(shader_errors::make(shader_errors::Code::kInvalidArgument, "spirv file not found"));
    }
    const std::streamsize size = in.tellg();
    if (size <= 0 || (size % 4) != 0)
    {
        return std::unexpected(
            shader_errors::make(
                shader_errors::Code::kInvalidArgument,
                "spirv file size must be a non-zero multiple of 4"
            )
        );
    }
    in.seekg(0, std::ios::beg);

    std::vector<std::uint32_t> words(static_cast<std::size_t>(size) / 4U);
    // Read as bytes then memcpy into the uint32_t buffer — char_traits<char>
    // is the only stream specialization required by the standard, so this is
    // the portable shape (avoids reinterpret_cast<char*> on the words array).
    std::vector<char> bytes(static_cast<std::size_t>(size));
    if (!in.read(bytes.data(), size))
    {
        return std::unexpected(shader_errors::make(shader_errors::Code::kInvalidArgument, "spirv read failed"));
    }
    std::memcpy(words.data(), bytes.data(), bytes.size());
    return words;
}

}  // namespace cd::shader
