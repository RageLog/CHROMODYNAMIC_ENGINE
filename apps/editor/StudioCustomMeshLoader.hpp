// =============================================================================
// CHROMODYNAMIC STUDIO -- apps/editor/StudioCustomMeshLoader.hpp
//
// Canonical Wavefront .obj mesh adapter delegating parsing to `cd::asset::obj`
// (`cd::asset_obj`) and `cd::app_common::read_text_file_utf8` (zero duplicate
// `.obj` parser, zero `try/catch(...)`), plus studio bounding-box/UV normalizer.
// =============================================================================
#pragma once

#include <cd/app_common/FileTextIo.hpp>
#include <cd/asset/Primitives.hpp>
#include <cd/asset/obj/ObjLoader.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace cd::studio
{

/// Parse a Wavefront .obj file from disk into a `cd::asset::PrimitiveMesh` via `cd::asset::obj::parse_obj`.
[[nodiscard]] inline bool parse_wavefront_obj_mesh(const std::string& path_str, cd::asset::PrimitiveMesh& out_mesh)
{
    const auto text_res = cd::app_common::read_text_file_utf8(path_str);
    if (!text_res.has_value())
    {
        return false;
    }

    const auto obj_res = cd::asset::obj::parse_obj(*text_res);
    if (!obj_res.has_value() || obj_res->indices.size() < 3U || obj_res->vertices.empty())
    {
        return false;
    }

    const auto& obj = *obj_res;
    bool has_explicit_uv = false;
    for (const auto& ov : obj.vertices)
    {
        if (std::abs(ov.texcoord0.x) > 1e-5F || std::abs(ov.texcoord0.y) > 1e-5F)
        {
            has_explicit_uv = true;
            break;
        }
    }

    std::vector<cd::asset::PrimitiveVertex> raw_verts;
    raw_verts.reserve(obj.indices.size());
    for (const std::uint32_t vidx : obj.indices)
    {
        if (vidx >= obj.vertices.size())
        {
            return false;
        }
        const auto& ov = obj.vertices[vidx];
        cd::asset::PrimitiveVertex pv {};
        pv.pos[0]    = ov.position.x;
        pv.pos[1]    = ov.position.y;
        pv.pos[2]    = ov.position.z;
        pv.normal[0] = ov.normal.x;
        pv.normal[1] = ov.normal.y;
        pv.normal[2] = ov.normal.z;
        pv.uv[0]     = has_explicit_uv ? ov.texcoord0.x : (ov.position.x * 0.5F + 0.5F);
        pv.uv[1]     = has_explicit_uv ? ov.texcoord0.y : (ov.position.y * 0.5F + 0.5F);
        pv.color[0]  = 1.0F;
        pv.color[1]  = 1.0F;
        pv.color[2]  = 1.0F;
        raw_verts.push_back(pv);
    }

    out_mesh = {};
    out_mesh.vertices = std::move(raw_verts);
    if (out_mesh.vertices.size() > 0xFFFFU)
    {
        out_mesh.indices_u32.resize(out_mesh.vertices.size());
        for (std::size_t i = 0; i < out_mesh.vertices.size(); ++i)
        {
            out_mesh.indices_u32[i] = static_cast<std::uint32_t>(i);
        }
    }
    else
    {
        out_mesh.indices.resize(out_mesh.vertices.size());
        for (std::size_t i = 0; i < out_mesh.vertices.size(); ++i)
        {
            out_mesh.indices[i] = static_cast<std::uint16_t>(i);
        }
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
