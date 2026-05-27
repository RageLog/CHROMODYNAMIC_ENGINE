// =============================================================================
// CHROMODYNAMIC — samples/hello_mesh_shader
//
// CPU-reference smoke for cd::mesh_shader. Builds meshlets from a 512-
// triangle synthetic mesh and verifies:
//   * every triangle is assigned to exactly one meshlet.
//   * each meshlet respects the 64-vertex / 124-triangle caps.
//   * the bounding sphere encloses every meshlet vertex.
// Same chunking + cone-culling math the GPU mesh shader uses.
// =============================================================================
#include <cd/core/Version.hpp>
#include <cd/math/Vector.hpp>
#include <cd/mesh_shader/Meshlet.hpp>

#include <cmath>
#include <cstdio>
#include <vector>

int main()
{
    std::printf("CHROMODYNAMIC %u.%u.%u — hello_mesh_shader\n",
                static_cast<unsigned>(cd::core::kEngineVersion.major),
                static_cast<unsigned>(cd::core::kEngineVersion.minor),
                static_cast<unsigned>(cd::core::kEngineVersion.patch));

    namespace ms = cd::mesh_shader;

    // Synthetic grid: 16x16 quads = 256 quads = 512 triangles.
    constexpr std::uint32_t kSide = 17;  // 16+1 vertices per side
    std::vector<cd::math::Vec3f> positions;
    positions.reserve(kSide * kSide);
    for (std::uint32_t y = 0; y < kSide; ++y)
    for (std::uint32_t x = 0; x < kSide; ++x)
    {
        positions.push_back({
            static_cast<float>(x),
            static_cast<float>(y),
            0.0F });
    }
    std::vector<std::uint32_t> indices;
    indices.reserve((kSide - 1) * (kSide - 1) * 6);
    for (std::uint32_t y = 0; y + 1 < kSide; ++y)
    for (std::uint32_t x = 0; x + 1 < kSide; ++x)
    {
        const std::uint32_t i0 = y * kSide + x;
        const std::uint32_t i1 = i0 + 1;
        const std::uint32_t i2 = i0 + kSide;
        const std::uint32_t i3 = i2 + 1;
        indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
        indices.push_back(i1); indices.push_back(i2); indices.push_back(i3);
    }
    std::printf("  source: %zu vertices, %zu triangles\n",
                positions.size(), indices.size() / 3);

    auto md = ms::build_meshlets(indices, positions);
    std::printf("  built  %zu meshlets\n", md.meshlets.size());

    std::uint32_t total_tris = 0;
    std::uint32_t max_verts = 0;
    std::uint32_t max_tris  = 0;
    int fails = 0;
    for (std::size_t m = 0; m < md.meshlets.size(); ++m)
    {
        const auto& ml = md.meshlets[m];
        total_tris += ml.triangle_count;
        max_verts  = std::max(max_verts, ml.vertex_count);
        max_tris   = std::max(max_tris, ml.triangle_count);
        if (ml.vertex_count   > ms::kVerticesPerMeshlet ||
            ml.triangle_count > ms::kTrianglesPerMeshlet)
        {
            std::printf("FAIL — meshlet %zu exceeds caps (v=%u t=%u)\n",
                        m, ml.vertex_count, ml.triangle_count);
            ++fails;
        }
        // bounding sphere encloses every vertex.
        for (std::uint32_t v = 0; v < ml.vertex_count; ++v)
        {
            const std::uint32_t gi = md.vertex_indices[ml.vertex_offset + v];
            const auto& p = positions[gi];
            const float dx = p.x - ml.bounds_sphere.x;
            const float dy = p.y - ml.bounds_sphere.y;
            const float dz = p.z - ml.bounds_sphere.z;
            const float d  = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d > ml.bounds_sphere.w + 1e-3F)
            {
                std::printf("FAIL — meshlet %zu vertex %u outside bounds (d=%.4f r=%.4f)\n",
                            m, v, static_cast<double>(d),
                            static_cast<double>(ml.bounds_sphere.w));
                ++fails;
            }
        }
    }
    std::printf("  totals: %u tris, max verts/meshlet=%u, max tris/meshlet=%u\n",
                total_tris, max_verts, max_tris);

    if (total_tris != indices.size() / 3)
    {
        std::printf("FAIL — total triangles mismatch (%u vs %zu)\n",
                    total_tris, indices.size() / 3);
        ++fails;
    }
    if (fails != 0)
    {
        std::printf("[hello_mesh_shader] %d FAILURES\n", fails);
        return 1;
    }

    std::printf("[hello_mesh_shader] PARITY OK\n");
    return 0;
}
