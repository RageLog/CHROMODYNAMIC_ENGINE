// =============================================================================
// CHROMODYNAMIC — tools/cook_mesh
//
// Offline asset cooker. Takes a Wavefront .obj or a glTF .gltf/.glb file
// and emits a .cdmesh that the runtime can fread + memcpy straight into
// GPU vertex/index buffers.
//
// Why offline cook:
//   * Skip the per-launch parse cost (tinygltf ~50-200 ms / megabyte;
//     obj parser similar). .cdmesh load is bounded by disk read alone.
//   * Catch malformed assets at build time, not in front of the player.
//   * Pre-compute vertex de-dup, bbox, optional later: tangent space,
//     LOD chain, BVH acceleration structure.
//
// Usage:
//   cook_mesh --input <path.obj|.gltf|.glb> --output <path.cdmesh>
//   cook_mesh -i model.obj -o model.cdmesh
//
// Format detection: file extension. .obj → cd::asset_obj. .gltf/.glb →
// cd::asset_gltf (first instance only; multi-mesh files write the first
// instance — proper multi-mesh cooking is a v2 feature).
//
// Exit codes:
//   0 success
//   1 usage error
//   2 input load error
//   3 output write error
//   4 unsupported format
// =============================================================================
#include <cd/asset/cdmesh/CdMesh.hpp>
#include <cd/asset/gltf/GltfLoader.hpp>
#include <cd/asset/obj/ObjLoader.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{

struct Args
{
    std::string input;
    std::string output;
    bool verbose { false };
};

[[nodiscard]] bool parse_args(int argc, char** argv, Args& out)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view a { argv[i] };
        if ((a == "--input" || a == "-i") && i + 1 < argc)
        {
            out.input = argv[++i];
        }
        else if ((a == "--output" || a == "-o") && i + 1 < argc)
        {
            out.output = argv[++i];
        }
        else if (a == "--verbose" || a == "-v")
        {
            out.verbose = true;
        }
        else if (a == "--help" || a == "-h")
        {
            return false;
        }
        else
        {
            std::fprintf(stderr, "cook_mesh: unknown argument: %.*s\n", static_cast<int>(a.size()), a.data());
            return false;
        }
    }
    return !out.input.empty() && !out.output.empty();
}

void print_usage()
{
    std::printf("Usage: cook_mesh -i <input> -o <output> [-v]\n");
    std::printf("  -i / --input    Source mesh: .obj, .gltf, .glb\n");
    std::printf("  -o / --output   Destination .cdmesh\n");
    std::printf("  -v / --verbose  Print per-stage statistics\n");
}

[[nodiscard]] std::string_view to_lower_ext(std::string_view path)
{
    const auto dot = path.rfind('.');
    if (dot == std::string_view::npos)
        return {};
    return path.substr(dot);
}

[[nodiscard]] bool extension_is(std::string_view path, std::string_view candidate)
{
    auto ext = to_lower_ext(path);
    if (ext.size() != candidate.size())
        return false;
    for (std::size_t i = 0; i < ext.size(); ++i)
    {
        const char a = (ext[i] >= 'A' && ext[i] <= 'Z') ? static_cast<char>(ext[i] - 'A' + 'a') : ext[i];
        if (a != candidate[i])
            return false;
    }
    return true;
}

/// Common cook path: a flat (vertex blob, index blob, bbox) triple →
/// cdmesh on disk. Indices are written as u32 regardless of source.
[[nodiscard]] int write_cooked(
    std::string_view output,
    std::span<const std::uint8_t> vertices,
    std::span<const std::uint8_t> indices,
    std::uint32_t vertex_count,
    std::uint32_t index_count,
    std::uint32_t vertex_stride,
    std::uint32_t index_stride,
    const cd::math::Vec3f& bb_min,
    const cd::math::Vec3f& bb_max,
    bool verbose
)
{
    cd::asset::cdmesh::SaveDesc d {};
    d.vertices = vertices;
    d.indices = indices;
    d.vertex_count = vertex_count;
    d.index_count = index_count;
    d.vertex_stride = vertex_stride;
    d.index_stride = index_stride;
    d.bbox_min = bb_min;
    d.bbox_max = bb_max;
    auto r = cd::asset::cdmesh::save(output, d);
    if (!r.has_value())
    {
        std::fprintf(
            stderr,
            "cook_mesh: write failed: %.*s\n",
            static_cast<int>(r.error().message.size()),
            r.error().message.data()
        );
        return 3;
    }
    if (verbose)
    {
        std::printf(
            "cook_mesh: wrote %s  (verts=%u stride=%u  idx=%u stride=%u)\n",
            std::string { output }.c_str(),
            vertex_count,
            vertex_stride,
            index_count,
            index_stride
        );
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv)
{
    Args args;
    if (!parse_args(argc, argv, args))
    {
        print_usage();
        return 1;
    }

    if (args.verbose)
        std::printf("cook_mesh: %s -> %s\n", args.input.c_str(), args.output.c_str());

    // ---- OBJ path ----
    if (extension_is(args.input, ".obj"))
    {
        auto loaded = cd::asset::obj::load_obj(args.input);
        if (!loaded.has_value())
        {
            std::fprintf(
                stderr,
                "cook_mesh: obj load failed: %.*s\n",
                static_cast<int>(loaded.error().message.size()),
                loaded.error().message.data()
            );
            return 2;
        }
        const auto& mesh = *loaded;
        return write_cooked(
            args.output,
            { reinterpret_cast<const std::uint8_t*>(mesh.vertices.data()),
              mesh.vertices.size() * sizeof(cd::asset::obj::ObjVertex) },
            { reinterpret_cast<const std::uint8_t*>(mesh.indices.data()),
              mesh.indices.size() * sizeof(std::uint32_t) },
            static_cast<std::uint32_t>(mesh.vertices.size()),
            static_cast<std::uint32_t>(mesh.indices.size()),
            sizeof(cd::asset::obj::ObjVertex),
            4,
            mesh.bbox_min,
            mesh.bbox_max,
            args.verbose
        );
    }

    // ---- glTF path (first instance only) ----
    if (extension_is(args.input, ".gltf") || extension_is(args.input, ".glb"))
    {
        auto loaded = cd::asset::gltf::load_gltf(args.input);
        if (!loaded.has_value())
        {
            std::fprintf(
                stderr,
                "cook_mesh: gltf load failed: %.*s\n",
                static_cast<int>(loaded.error().message.size()),
                loaded.error().message.data()
            );
            return 2;
        }
        const auto& scene = *loaded;
        if (scene.meshes.empty() || scene.meshes[0].primitives.empty())
        {
            std::fprintf(stderr, "cook_mesh: gltf has no mesh primitives\n");
            return 2;
        }
        const auto& prim = scene.meshes[0].primitives[0];
        if (args.verbose && scene.instances.size() > 1)
        {
            std::printf("cook_mesh: WARNING — file has %zu instances; cooking ONLY the first primitive.\n",
                        scene.instances.size());
        }
        return write_cooked(
            args.output,
            { reinterpret_cast<const std::uint8_t*>(prim.vertices.data()),
              prim.vertices.size() * sizeof(cd::asset::gltf::GltfVertex) },
            { reinterpret_cast<const std::uint8_t*>(prim.indices.data()),
              prim.indices.size() * sizeof(std::uint32_t) },
            static_cast<std::uint32_t>(prim.vertices.size()),
            static_cast<std::uint32_t>(prim.indices.size()),
            sizeof(cd::asset::gltf::GltfVertex),
            4,
            scene.bbox_min,
            scene.bbox_max,
            args.verbose
        );
    }

    std::fprintf(stderr, "cook_mesh: unsupported extension '%s'. Use .obj, .gltf, or .glb.\n", args.input.c_str());
    return 4;
}
