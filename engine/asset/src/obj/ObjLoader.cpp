// =============================================================================
// CHROMODYNAMIC — cd/asset/obj/ObjLoader.cpp
// =============================================================================
#include <cd/asset/obj/ObjLoader.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cd::asset::obj
{

namespace
{

/// Parse a float from a `string_view` starting at `pos`. Advances `pos`
/// past the consumed characters and any trailing whitespace. Returns
/// false on parse failure (caller treats as malformed file).
[[nodiscard]] bool parse_float(std::string_view text, std::size_t& pos, float& out) noexcept
{
    // Skip leading whitespace.
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t'))
        ++pos;
    if (pos >= text.size())
        return false;
    const char* begin = text.data() + pos;
    const char* end = text.data() + text.size();
    auto [ptr, ec] = std::from_chars(begin, end, out);
    if (ec != std::errc {})
        return false;
    pos = static_cast<std::size_t>(ptr - text.data());
    return true;
}

/// Parse a 1-based OBJ index. Returns false on malformed input. Negative
/// (relative) indices are explicitly rejected via the optional out param.
[[nodiscard]] bool parse_index(std::string_view tok, int& out) noexcept
{
    if (tok.empty())
    {
        out = 0;
        return true;
    }
    int v = 0;
    auto [ptr, ec] = std::from_chars(tok.data(), tok.data() + tok.size(), v);
    if (ec != std::errc {})
        return false;
    out = v;
    return true;
}

/// Tokenise a face vertex spec ("v", "v/vt", "v//vn", "v/vt/vn") into
/// three integer slots. Missing fields are returned as 0.
struct FaceTriple
{
    int v { 0 };
    int vt { 0 };
    int vn { 0 };
};

/// Equality at namespace scope so ADL from std::equal_to in
/// std::unordered_map<FaceTriple, ...> can find it. The use is INDIRECT
/// (through std::equal_to specialisation) — the compiler cannot see it
/// from this TU, hence the explicit `[[maybe_unused]]` to silence the
/// -Wunused-function it would otherwise emit.
[[maybe_unused]] [[nodiscard]] inline bool operator==(const FaceTriple& a, const FaceTriple& b) noexcept
{
    return a.v == b.v && a.vt == b.vt && a.vn == b.vn;
}

struct FaceTripleHash
{
    [[nodiscard]] std::size_t operator()(const FaceTriple& t) const noexcept
    {
        // splitmix64-flavoured mix; cheap, no collisions for typical mesh sizes.
        std::uint64_t h = static_cast<std::uint64_t>(t.v);
        h = (h * 0x9E3779B97F4A7C15ULL) ^ static_cast<std::uint64_t>(t.vt);
        h = (h * 0x9E3779B97F4A7C15ULL) ^ static_cast<std::uint64_t>(t.vn);
        // We only target 64-bit hosts. size_t == uint64_t on every supported
        // platform, so an explicit cast would trip GCC's -Wuseless-cast;
        // rely on the implicit conversion instead.
        return h ^ (h >> 32);
    }
};

[[nodiscard]] bool parse_face_triple(std::string_view tok, FaceTriple& out, bool& negative_index)
{
    out = FaceTriple {};
    negative_index = false;
    std::size_t i = 0;
    int slots[3] = { 0, 0, 0 };
    int slot = 0;
    std::size_t field_start = 0;
    while (i <= tok.size() && slot < 3)
    {
        if (i == tok.size() || tok[i] == '/')
        {
            const auto field = tok.substr(field_start, i - field_start);
            int v = 0;
            if (!parse_index(field, v))
                return false;
            if (v < 0)
                negative_index = true;
            slots[slot] = v;
            ++slot;
            field_start = i + 1;
        }
        ++i;
    }
    out.v = slots[0];
    out.vt = slots[1];
    out.vn = slots[2];
    return true;
}

[[nodiscard]] cd::core::Result<std::string> read_file_to_string(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
    {
        return std::unexpected(obj_errors::make(obj_errors::Code::kFileNotFound, path));
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

}  // namespace

cd::core::Result<ObjMesh> parse_obj(std::string_view text)
{
    if (text.empty())
    {
        return std::unexpected(obj_errors::make(obj_errors::Code::kInvalidArgument, "empty input"));
    }

    std::vector<cd::math::Vec3f> positions;
    std::vector<cd::math::Vec3f> normals;
    std::vector<cd::math::Vec2f> uvs;
    positions.reserve(1024);
    normals.reserve(1024);
    uvs.reserve(1024);

    ObjMesh out;
    out.vertices.reserve(1024);
    out.indices.reserve(2048);

    // De-dup map for (v, vt, vn) tuples → unique vertex index.
    std::unordered_map<FaceTriple, std::uint32_t, FaceTripleHash> dedup;
    dedup.reserve(1024);

    // Single-pass line scan.
    std::size_t line_start = 0;
    while (line_start < text.size())
    {
        std::size_t line_end = text.find('\n', line_start);
        if (line_end == std::string_view::npos)
            line_end = text.size();
        std::string_view line = text.substr(line_start, line_end - line_start);
        // Strip trailing \r for CRLF files.
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        line_start = line_end + 1;

        if (line.empty() || line[0] == '#')
            continue;

        // First token = directive.
        std::size_t pos = 0;
        while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
            ++pos;
        if (pos >= line.size())
            continue;
        const std::size_t key_start = pos;
        while (pos < line.size() && line[pos] != ' ' && line[pos] != '\t')
            ++pos;
        const std::string_view directive = line.substr(key_start, pos - key_start);

        if (directive == "v")
        {
            float x = 0.0F;
            float y = 0.0F;
            float z = 0.0F;
            if (!parse_float(line, pos, x) || !parse_float(line, pos, y) || !parse_float(line, pos, z))
                return std::unexpected(obj_errors::make(obj_errors::Code::kParseFailed, "bad 'v' line"));
            positions.push_back({ x, y, z });
        }
        else if (directive == "vn")
        {
            float x = 0.0F;
            float y = 0.0F;
            float z = 0.0F;
            if (!parse_float(line, pos, x) || !parse_float(line, pos, y) || !parse_float(line, pos, z))
                return std::unexpected(obj_errors::make(obj_errors::Code::kParseFailed, "bad 'vn' line"));
            normals.push_back({ x, y, z });
        }
        else if (directive == "vt")
        {
            float u = 0.0F;
            float v = 0.0F;
            if (!parse_float(line, pos, u) || !parse_float(line, pos, v))
                return std::unexpected(obj_errors::make(obj_errors::Code::kParseFailed, "bad 'vt' line"));
            uvs.push_back({ u, v });
        }
        else if (directive == "f")
        {
            // Collect up to N face vertices (we support tris + quads;
            // ngons would need polygon triangulation — out of scope for v1).
            std::vector<FaceTriple> face;
            face.reserve(4);
            while (pos < line.size())
            {
                while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
                    ++pos;
                if (pos >= line.size())
                    break;
                const std::size_t tok_start = pos;
                while (pos < line.size() && line[pos] != ' ' && line[pos] != '\t')
                    ++pos;
                const auto tok = line.substr(tok_start, pos - tok_start);
                FaceTriple t {};
                bool negative = false;
                if (!parse_face_triple(tok, t, negative))
                    return std::unexpected(obj_errors::make(obj_errors::Code::kParseFailed, "bad face triple"));
                if (negative)
                    return std::unexpected(
                        obj_errors::make(obj_errors::Code::kUnsupported, "negative face indices not supported")
                    );
                face.push_back(t);
            }

            if (face.size() < 3)
                return std::unexpected(obj_errors::make(obj_errors::Code::kParseFailed, "face < 3 verts"));

            // Resolve each triple to an output-vertex index, building the
            // de-dup table on the fly.
            auto resolve = [&](const FaceTriple& t) -> cd::core::Result<std::uint32_t>
            {
                auto it = dedup.find(t);
                if (it != dedup.end())
                    return it->second;

                if (t.v <= 0 || static_cast<std::size_t>(t.v) > positions.size())
                    return std::unexpected(obj_errors::make(obj_errors::Code::kParseFailed, "v index out of range"));

                ObjVertex ov;
                ov.position = positions[static_cast<std::size_t>(t.v - 1)];

                if (t.vt > 0 && static_cast<std::size_t>(t.vt) <= uvs.size())
                    ov.texcoord0 = uvs[static_cast<std::size_t>(t.vt - 1)];
                if (t.vn > 0 && static_cast<std::size_t>(t.vn) <= normals.size())
                    ov.normal = normals[static_cast<std::size_t>(t.vn - 1)];
                // If no normal yet, leave the (0,1,0) default — we'll
                // override with face normals after the loop when the
                // input lacked any `vn` directive at all.

                const auto idx = static_cast<std::uint32_t>(out.vertices.size());
                out.vertices.push_back(ov);
                dedup.emplace(t, idx);
                return idx;
            };

            // Fan-triangulate (0, i, i+1) for tris (1 triangle) and quads
            // (2 triangles). The interior diagonal is (0 → 2), matching
            // common DCC export conventions.
            std::vector<std::uint32_t> face_indices;
            face_indices.reserve(face.size());
            for (const auto& t : face)
            {
                auto r = resolve(t);
                if (!r.has_value())
                    return std::unexpected(r.error());
                face_indices.push_back(*r);
            }
            for (std::size_t k = 1; k + 1 < face_indices.size(); ++k)
            {
                out.indices.push_back(face_indices[0]);
                out.indices.push_back(face_indices[k]);
                out.indices.push_back(face_indices[k + 1]);
            }
        }
        // Ignored directives: g, o, s, mtllib, usemtl. We treat them as
        // comments — silent skip keeps real-world files loading cleanly.
    }

    if (out.vertices.empty())
    {
        return std::unexpected(obj_errors::make(obj_errors::Code::kParseFailed, "no vertices"));
    }

    // Synthesize flat normals if the file carried no `vn` directive at all.
    // Per-vertex de-dup is now finalised so we can safely overwrite normals
    // without changing the dedup map.
    if (normals.empty())
    {
        // Reset normals to zero so we can sum per-face contributions.
        for (auto& v : out.vertices)
            v.normal = { 0.0F, 0.0F, 0.0F };

        for (std::size_t i = 0; i + 2 < out.indices.size(); i += 3)
        {
            const auto i0 = out.indices[i + 0];
            const auto i1 = out.indices[i + 1];
            const auto i2 = out.indices[i + 2];
            const auto& a = out.vertices[i0].position;
            const auto& b = out.vertices[i1].position;
            const auto& c = out.vertices[i2].position;
            const cd::math::Vec3f e0 { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
            const cd::math::Vec3f e1 { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
            const cd::math::Vec3f fn { e0[1] * e1[2] - e0[2] * e1[1],
                                       e0[2] * e1[0] - e0[0] * e1[2],
                                       e0[0] * e1[1] - e0[1] * e1[0] };
            // Accumulate area-weighted face normal into each vertex.
            for (auto idx : { i0, i1, i2 })
            {
                out.vertices[idx].normal[0] += fn[0];
                out.vertices[idx].normal[1] += fn[1];
                out.vertices[idx].normal[2] += fn[2];
            }
        }
        for (auto& v : out.vertices)
        {
            const float len =
                std::sqrt(v.normal[0] * v.normal[0] + v.normal[1] * v.normal[1] + v.normal[2] * v.normal[2]);
            if (len > 1e-6F)
            {
                v.normal[0] /= len;
                v.normal[1] /= len;
                v.normal[2] /= len;
            }
            else
            {
                v.normal = { 0.0F, 1.0F, 0.0F };
            }
        }
    }

    // Bounding box.
    cd::math::Vec3f bb_min { std::numeric_limits<float>::infinity(),
                             std::numeric_limits<float>::infinity(),
                             std::numeric_limits<float>::infinity() };
    cd::math::Vec3f bb_max { -std::numeric_limits<float>::infinity(),
                             -std::numeric_limits<float>::infinity(),
                             -std::numeric_limits<float>::infinity() };
    for (const auto& v : out.vertices)
    {
        bb_min[0] = std::min(bb_min[0], v.position[0]);
        bb_min[1] = std::min(bb_min[1], v.position[1]);
        bb_min[2] = std::min(bb_min[2], v.position[2]);
        bb_max[0] = std::max(bb_max[0], v.position[0]);
        bb_max[1] = std::max(bb_max[1], v.position[1]);
        bb_max[2] = std::max(bb_max[2], v.position[2]);
    }
    out.bbox_min = bb_min;
    out.bbox_max = bb_max;
    return out;
}

cd::core::Result<ObjMesh> load_obj(std::string_view path)
{
    if (path.empty())
    {
        return std::unexpected(obj_errors::make(obj_errors::Code::kInvalidArgument, "empty path"));
    }
    const std::string p { path };
    if (!std::filesystem::exists(p))
    {
        return std::unexpected(obj_errors::make(obj_errors::Code::kFileNotFound, p));
    }
    auto text = read_file_to_string(p);
    if (!text.has_value())
    {
        return std::unexpected(text.error());
    }
    return parse_obj(*text);
}

}  // namespace cd::asset::obj
