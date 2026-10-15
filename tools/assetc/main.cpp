// =============================================================================
// CHROMODYNAMIC — tools/assetc/main.cpp
// FAZ 10 Wave 10.1 — Offline .cdpak Asset Cooker & Bundle Verifier (v2.0.1)
//
// Modes:
//   1. cd_assetc --smoke-self-test
//      Builds an in-memory .cdpak containing a .cdstage + shader + metadata,
//      opens it via cd::asset::pak::Pak, verifies TOC & byte payloads, and exits 0.
//   2. cd_assetc --pack <input_dir> <output.cdpak>
//      Recursively walks <input_dir>, packs all files into <output.cdpak>, and
//      verifies the written archive.
//   3. cd_assetc --verify <input.cdpak>
//      Opens <input.cdpak> and prints entry count and TOC verification status.
// =============================================================================
#include <cd/asset/pak/Pak.hpp>

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{

[[nodiscard]] bool read_file_bytes(const std::filesystem::path& path, std::vector<std::byte>& out)
{
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs)
    {
        return false;
    }
    const auto end_pos = ifs.tellg();
    if (end_pos < 0)
    {
        return false;
    }
    out.resize(static_cast<std::size_t>(end_pos));
    ifs.seekg(0, std::ios::beg);
    if (!out.empty())
    {
        ifs.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    }
    return ifs.good() || ifs.eof();
}

[[nodiscard]] bool write_file_bytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes)
{
    std::error_code ec;
    if (path.has_parent_path())
    {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs)
    {
        return false;
    }
    if (!bytes.empty())
    {
        ofs.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    return ofs.good();
}

[[nodiscard]] int run_smoke_self_test()
{
    cd::asset::pak::PakBuilder builder;
    builder.add("stages/main.cdstage", R"({"stage":"ShowcaseMain","elements":["SolarLight3D","AetherFog3D","Avatar"]})");
    builder.add("shaders/froxel.glsl", "#include <cd/gluon/froxel_cluster.glsl>\n");
    builder.add("config/project.cdproj", R"({"title":"CHROMODYNAMIC Packaged Game","version":"1.0.0"})");

    const std::vector<std::byte> pak_bytes = builder.build();
    if (pak_bytes.empty())
    {
        std::fprintf(stderr, "[cd_assetc] self-test failed: PakBuilder returned empty buffer\n");
        return 1;
    }

    cd::asset::pak::Pak reader;
    const auto open_res = reader.open(pak_bytes.data(), pak_bytes.size());
    if (!open_res.has_value())
    {
        std::fprintf(stderr, "[cd_assetc] self-test failed: Pak::open error\n");
        return 1;
    }
    if (reader.entry_count() != 3U)
    {
        std::fprintf(stderr, "[cd_assetc] self-test failed: expected 3 entries\n");
        return 1;
    }
    const auto stage_blob = reader.find("stages/main.cdstage");
    if (!stage_blob.has_value() || stage_blob->size == 0U)
    {
        std::fprintf(stderr, "[cd_assetc] self-test failed: missing stages/main.cdstage\n");
        return 1;
    }

    std::printf(
        "[cd_assetc] OK — packed %zu entries (%zu bytes), TOC verified.\n",
        reader.entry_count(),
        pak_bytes.size()
    );
    return 0;
}

[[nodiscard]] int pack_directory(const std::filesystem::path& input_dir, const std::filesystem::path& output_pak)
{
    std::error_code ec;
    if (!std::filesystem::exists(input_dir, ec))
    {
        std::fprintf(stderr, "[cd_assetc] input directory does not exist: %s\n", input_dir.string().c_str());
        return 1;
    }

    cd::asset::pak::PakBuilder builder;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(input_dir, ec))
    {
        if (!entry.is_regular_file(ec))
        {
            continue;
        }
        std::vector<std::byte> file_bytes;
        if (!read_file_bytes(entry.path(), file_bytes))
        {
            return 1;
        }
        const auto rel = std::filesystem::relative(entry.path(), input_dir, ec).generic_string();
        builder.add(rel, std::move(file_bytes));
    }

    const auto pak_bytes = builder.build();
    if (pak_bytes.empty() || !write_file_bytes(output_pak, pak_bytes))
    {
        std::fprintf(stderr, "[cd_assetc] failed to write pak: %s\n", output_pak.string().c_str());
        return 1;
    }

    std::printf(
        "[cd_assetc] Packed %zu files into %s (%zu bytes)\n",
        builder.entry_count(),
        output_pak.string().c_str(),
        pak_bytes.size()
    );
    return 0;
}

[[nodiscard]] int verify_pak_file(const std::filesystem::path& input_pak)
{
    std::vector<std::byte> pak_bytes;
    if (!read_file_bytes(input_pak, pak_bytes))
    {
        std::fprintf(stderr, "[cd_assetc] cannot read pak: %s\n", input_pak.string().c_str());
        return 1;
    }
    cd::asset::pak::Pak reader;
    auto res = reader.open(pak_bytes.data(), pak_bytes.size());
    if (!res.has_value())
    {
        std::fprintf(stderr, "[cd_assetc] corrupt pak: %s\n", input_pak.string().c_str());
        return 1;
    }
    std::printf("[cd_assetc] Verified %s (%zu entries, %zu bytes)\n",
                input_pak.string().c_str(), reader.entry_count(), pak_bytes.size());
    return 0;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc <= 1 || std::string_view(argv[1]) == "--smoke-self-test")
    {
        return run_smoke_self_test();
    }
    if (argc >= 4 && std::string_view(argv[1]) == "--pack")
    {
        return pack_directory(argv[2], argv[3]);
    }
    if (argc >= 3 && std::string_view(argv[1]) == "--verify")
    {
        return verify_pak_file(argv[2]);
    }

    std::filesystem::path input_dir {};
    std::filesystem::path output_pak {};
    bool verify_after_pack = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg { argv[i] };
        if (arg == "--input" && i + 1 < argc)
        {
            input_dir = argv[++i];
        }
        else if (arg == "--output" && i + 1 < argc)
        {
            output_pak = argv[++i];
        }
        else if (arg == "--verify")
        {
            verify_after_pack = true;
        }
    }
    if (!input_dir.empty() && !output_pak.empty())
    {
        const int rc = pack_directory(input_dir, output_pak);
        if (rc != 0)
        {
            return rc;
        }
        if (verify_after_pack)
        {
            return verify_pak_file(output_pak);
        }
        return 0;
    }

    return run_smoke_self_test();
}
