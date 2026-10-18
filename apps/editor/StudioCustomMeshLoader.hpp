// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/StudioCustomMeshLoader.hpp
//
// Wavefront .obj parser and custom mesh bounding-box/normal/UV normalizer
// for the Studio Content Browser & Folder Tree mesh assignment pipeline.
// =============================================================================
#pragma once

#include <cd/asset/Primitives.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace cd::studio
{

/// Parse a Wavefront .obj file from disk into a `cd::asset::PrimitiveMesh` (supports v, vt, vn, f with fan triangulation).
[[nodiscard]] inline bool parse_wavefront_obj_mesh(const std::string& path_str, cd::asset::PrimitiveMesh& out_mesh)
{
    std::ifstream in(path_str);
    if (!in.is_open()) return false;

    std::vector<cd::math::Vec3f> positions;
    std::vector<cd::math::Vec2f> texcoords;
    std::vector<cd::math::Vec3f> normals;
    std::vector<cd::asset::PrimitiveVertex> raw_verts;

    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        std::string cmd;
        if (!(iss >> cmd)) continue;

        if (cmd == "v")
        {
            float x = 0.0F, y = 0.0F, z = 0.0F;
            if (iss >> x >> y >> z && std::isfinite(x) && std::isfinite(y) && std::isfinite(z))
                positions.push_back({ x, y, z });
        }
        else if (cmd == "vt")
        {
            float u = 0.0F, v = 0.0F;
            if (iss >> u >> v && std::isfinite(u) && std::isfinite(v))
                texcoords.push_back({ u, v });
        }
        else if (cmd == "vn")
        {
            float nx = 0.0F, ny = 1.0F, nz = 0.0F;
            if (iss >> nx >> ny >> nz && std::isfinite(nx) && std::isfinite(ny) && std::isfinite(nz))
                normals.push_back({ nx, ny, nz });
        }
        else if (cmd == "f")
        {
            struct ObjCorner { int vi = 0; int ti = 0; int ni = 0; };
            std::vector<ObjCorner> poly;
            std::string tok;
            while (iss >> tok)
            {
                ObjCorner c {};
                const auto s1 = tok.find('/');
                try
                {
                    if (s1 == std::string::npos)
                    {
                        c.vi = std::stoi(tok);
                    }
                    else
                    {
                        c.vi = std::stoi(tok.substr(0, s1));
                        const auto s2 = tok.find('/', s1 + 1U);
                        if (s2 == std::string::npos)
                        {
                            if (s1 + 1U < tok.size()) c.ti = std::stoi(tok.substr(s1 + 1U));
                        }
                        else
                        {
                            if (s2 > s1 + 1U) c.ti = std::stoi(tok.substr(s1 + 1U, s2 - s1 - 1U));
                            if (s2 + 1U < tok.size()) c.ni = std::stoi(tok.substr(s2 + 1U));
                        }
                    }
                }
                catch (...)
                {
                    continue;
                }
                poly.push_back(c);
            }
            if (poly.size() < 3U) continue;

            auto resolve_idx = [](int raw_idx, std::size_t count) noexcept -> int
            {
                if (raw_idx > 0 && std::cmp_less_equal(raw_idx, count)) return raw_idx - 1;
                if (raw_idx < 0 && std::cmp_less_equal(-raw_idx, count)) return static_cast<int>(count) + raw_idx;
                return -1;
            };

            for (std::size_t k = 1U; k + 1U < poly.size(); ++k)
            {
                const std::array<ObjCorner, 3> tri {{ poly[0], poly[k], poly[k + 1U] }};
                const int pi0 = resolve_idx(tri[0].vi, positions.size());
                const int pi1 = resolve_idx(tri[1].vi, positions.size());
                const int pi2 = resolve_idx(tri[2].vi, positions.size());
                if (pi0 < 0 || pi1 < 0 || pi2 < 0) continue;

                const cd::math::Vec3f p0 = positions[static_cast<std::size_t>(pi0)];
                const cd::math::Vec3f p1 = positions[static_cast<std::size_t>(pi1)];
                const cd::math::Vec3f p2 = positions[static_cast<std::size_t>(pi2)];
                cd::math::Vec3f face_n = cd::math::cross(p1 - p0, p2 - p0);
                const float fn_len = cd::math::length(face_n);
                face_n = (fn_len > 1e-6F) ? (face_n * (1.0F / fn_len)) : cd::math::Vec3f { 0.0F, 1.0F, 0.0F };

                const std::array<int, 3> pis {{ pi0, pi1, pi2 }};
                for (std::size_t ci = 0U; ci < 3U; ++ci)
                {
                    const cd::math::Vec3f pos = positions[static_cast<std::size_t>(pis[ci])];
                    const int ti = resolve_idx(tri[ci].ti, texcoords.size());
                    const int ni = resolve_idx(tri[ci].ni, normals.size());
                    cd::math::Vec3f nrm = (ni >= 0) ? normals[static_cast<std::size_t>(ni)] : face_n;
                    const float nl = cd::math::length(nrm);
                    nrm = (nl > 1e-6F) ? (nrm * (1.0F / nl)) : face_n;
                    const cd::math::Vec2f uv = (ti >= 0)
                        ? texcoords[static_cast<std::size_t>(ti)]
                        : cd::math::Vec2f { pos.x * 0.5F + 0.5F, pos.y * 0.5F + 0.5F };

                    cd::asset::PrimitiveVertex pv {};
                    pv.pos[0]    = pos.x;
                    pv.pos[1]    = pos.y;
                    pv.pos[2]    = pos.z;
                    pv.normal[0] = nrm.x;
                    pv.normal[1] = nrm.y;
                    pv.normal[2] = nrm.z;
                    pv.uv[0]     = uv.x;
                    pv.uv[1]     = uv.y;
                    pv.color[0]  = 1.0F;
                    pv.color[1]  = 1.0F;
                    pv.color[2]  = 1.0F;
                    raw_verts.push_back(pv);
                }
            }
        }
    }

    if (raw_verts.size() < 3U) return false;
    out_mesh = {};
    out_mesh.vertices = std::move(raw_verts);
    if (out_mesh.vertices.size() > 0xFFFFU)
    {
        out_mesh.indices_u32.resize(out_mesh.vertices.size());
        for (std::size_t i = 0; i < out_mesh.vertices.size(); ++i)
            out_mesh.indices_u32[i] = static_cast<std::uint32_t>(i);
    }
    else
    {
        out_mesh.indices.resize(out_mesh.vertices.size());
        for (std::size_t i = 0; i < out_mesh.vertices.size(); ++i)
            out_mesh.indices[i] = static_cast<std::uint16_t>(i);
    }
    return true;
}

/// Validate and normalize a custom disk mesh so out-of-range/flat/un-UV'd meshes render cleanly in the studio.
[[nodiscard]] inline bool normalize_and_validate_custom_mesh(cd::asset::PrimitiveMesh& mesh) noexcept
{
    if (mesh.vertices.empty() || (mesh.indices.empty() && mesh.indices_u32.empty()))
        return false;

    cd::math::Vec3f bmin {  1e30F,  1e30F,  1e30F };
    cd::math::Vec3f bmax { -1e30F, -1e30F, -1e30F };
    bool any_uv = false;
    for (const auto& v : mesh.vertices)
    {
        if (!std::isfinite(v.pos[0]) || !std::isfinite(v.pos[1]) || !std::isfinite(v.pos[2]))
            return false;
        bmin.x = std::min(bmin.x, v.pos[0]);
        bmin.y = std::min(bmin.y, v.pos[1]);
        bmin.z = std::min(bmin.z, v.pos[2]);
        bmax.x = std::max(bmax.x, v.pos[0]);
        bmax.y = std::max(bmax.y, v.pos[1]);
        bmax.z = std::max(bmax.z, v.pos[2]);
        if (std::abs(v.uv[0]) > 1e-5F || std::abs(v.uv[1]) > 1e-5F)
            any_uv = true;
    }

    const cd::math::Vec3f ext = bmax - bmin;
    const float max_ext = std::max({ ext.x, ext.y, ext.z });
    if (max_ext < 1e-5F || max_ext > 1e6F) return false;

    const cd::math::Vec3f center = (bmin + bmax) * 0.5F;
    const float norm_scale = (max_ext > 2.5F || max_ext < 0.35F) ? (1.15F / max_ext) : 1.0F;
    const bool recenter    = (cd::math::length(center) > 0.35F) || (norm_scale != 1.0F);
    const bool is_flat_z   = (ext.z < 1e-4F && ext.x > 1e-3F && ext.y > 1e-3F);

    for (auto& v : mesh.vertices)
    {
        if (!any_uv)
        {
            v.uv[0] = (ext.x > 1e-4F) ? ((v.pos[0] - bmin.x) / ext.x) : 0.5F;
            v.uv[1] = (ext.y > 1e-4F) ? (1.0F - (v.pos[1] - bmin.y) / ext.y) : 0.5F;
        }
        if (recenter)
        {
            v.pos[0] = (v.pos[0] - center.x) * norm_scale;
            v.pos[1] = (v.pos[1] - center.y) * norm_scale;
            v.pos[2] = (v.pos[2] - center.z) * norm_scale;
        }
        if (is_flat_z && std::abs(v.normal[2]) < 1e-3F)
        {
            v.normal[0] = 0.0F;
            v.normal[1] = 0.0F;
            v.normal[2] = 1.0F;
        }
        else
        {
            const float nl = std::sqrt(v.normal[0] * v.normal[0] + v.normal[1] * v.normal[1] + v.normal[2] * v.normal[2]);
            if (nl > 1e-5F && std::isfinite(nl))
            {
                v.normal[0] /= nl;
                v.normal[1] /= nl;
                v.normal[2] /= nl;
            }
            else
            {
                v.normal[0] = 0.0F;
                v.normal[1] = 1.0F;
                v.normal[2] = 0.0F;
            }
        }
    }
    return true;
}

} // namespace cd::studio
