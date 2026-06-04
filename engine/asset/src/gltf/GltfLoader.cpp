// =============================================================================
// CHROMODYNAMIC — cd/asset/gltf/GltfLoader.cpp
// =============================================================================
#include <cd/asset/gltf/GltfLoader.hpp>
#include <cd/math/Transform.hpp>  // Transformf, to_mat4 — used for glTF TRS node decode.

// tinygltf brings its own copies of stb_image / stb_image_write / nlohmann_json.
// Define the impl macros in exactly one TU (here) and keep the rest of the
// engine free of those dependencies.
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
// We never write images — disable the writer to skip its big inclusion graph.
#define TINYGLTF_NO_STB_IMAGE_WRITE

#if defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable : 4100 4189 4244 4267 4456 4458 4505 4702 4996)
#elif defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wall"
    #pragma GCC diagnostic ignored "-Wextra"
    #pragma GCC diagnostic ignored "-Wpedantic"
    #pragma GCC diagnostic ignored "-Wshadow"
    #pragma GCC diagnostic ignored "-Wconversion"
#endif

#include <tiny_gltf.h>

#if defined(_MSC_VER)
    #pragma warning(pop)
#elif defined(__clang__)
    #pragma clang diagnostic pop
#elif defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <cstdio>

namespace cd::asset::gltf
{

namespace
{

// ---- Accessor helpers -------------------------------------------------------

/// Returns a const-byte pointer into the buffer that backs `accessor`, plus
/// the stride between consecutive elements. tinygltf already validates
/// buffer-view bounds at parse time, so we trust them here.
struct AccessorView
{
    const std::byte* data { nullptr };
    std::size_t stride { 0 };
    std::size_t count { 0 };
    int component_type { 0 };
    int type { 0 };
};

[[nodiscard]] AccessorView access(const tinygltf::Model& model, int accessor_index)
{
    AccessorView v {};
    if (accessor_index < 0 || accessor_index >= static_cast<int>(model.accessors.size()))
        return v;
    const auto& acc = model.accessors[static_cast<std::size_t>(accessor_index)];
    if (acc.bufferView < 0)
        return v;
    const auto& view = model.bufferViews[static_cast<std::size_t>(acc.bufferView)];
    const auto& buffer = model.buffers[static_cast<std::size_t>(view.buffer)];
    const auto elem_size =
        static_cast<std::size_t>(tinygltf::GetComponentSizeInBytes(static_cast<std::uint32_t>(acc.componentType))) *
        static_cast<std::size_t>(tinygltf::GetNumComponentsInType(static_cast<std::uint32_t>(acc.type)));
    v.stride = (view.byteStride == 0) ? elem_size : view.byteStride;
    v.data = reinterpret_cast<const std::byte*>(buffer.data.data()) + view.byteOffset + acc.byteOffset;
    v.count = acc.count;
    v.component_type = acc.componentType;
    v.type = acc.type;
    return v;
}

/// Read one Vec3f from a tightly- or sparsely-packed FLOAT3 accessor. The
/// caller is responsible for bounds: `i < view.count`.
[[nodiscard]] cd::math::Vec3f read_vec3(const AccessorView& view, std::size_t i)
{
    const auto* p = reinterpret_cast<const float*>(view.data + i * view.stride);
    return cd::math::Vec3f { p[0], p[1], p[2] };
}

[[nodiscard]] cd::math::Vec2f read_vec2(const AccessorView& view, std::size_t i)
{
    const auto* p = reinterpret_cast<const float*>(view.data + i * view.stride);
    return cd::math::Vec2f { p[0], p[1] };
}

/// Read one index, transparently widening uint8/uint16 to uint32. The glTF
/// spec allows any of the three for primitive indices.
[[nodiscard]] std::uint32_t read_index(const AccessorView& view, std::size_t i)
{
    const auto* p = view.data + i * view.stride;
    switch (view.component_type)
    {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            return static_cast<std::uint32_t>(*reinterpret_cast<const std::uint8_t*>(p));
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            return static_cast<std::uint32_t>(*reinterpret_cast<const std::uint16_t*>(p));
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            return *reinterpret_cast<const std::uint32_t*>(p);
        default:
            return 0U;
    }
}

// ---- Per-primitive decode ---------------------------------------------------

[[nodiscard]] GltfPrimitive decode_primitive(const tinygltf::Model& model, const tinygltf::Primitive& prim)
{
    GltfPrimitive out;
    out.material_index = prim.material;

    // POSITION is required by the glTF spec; if it is missing the entire
    // primitive collapses to a no-op rather than an exception.
    auto pos_it = prim.attributes.find("POSITION");
    if (pos_it == prim.attributes.end())
        return out;

    const AccessorView pos_view = access(model, pos_it->second);
    if (pos_view.data == nullptr || pos_view.count == 0)
        return out;

    AccessorView normal_view {};
    AccessorView uv_view {};
    AccessorView joints_view {};
    AccessorView weights_view {};
    if (auto it = prim.attributes.find("NORMAL"); it != prim.attributes.end())
        normal_view = access(model, it->second);
    if (auto it = prim.attributes.find("TEXCOORD_0"); it != prim.attributes.end())
        uv_view = access(model, it->second);
    if (auto it = prim.attributes.find("JOINTS_0"); it != prim.attributes.end())
        joints_view = access(model, it->second);
    if (auto it = prim.attributes.find("WEIGHTS_0"); it != prim.attributes.end())
        weights_view = access(model, it->second);

    out.vertices.resize(pos_view.count);
    const bool has_skin = (joints_view.data != nullptr) && (weights_view.data != nullptr);
    if (has_skin)
        out.skin_vertices.resize(pos_view.count);
    for (std::size_t i = 0; i < pos_view.count; ++i)
    {
        auto& v = out.vertices[i];
        v.position = read_vec3(pos_view, i);
        if (normal_view.data != nullptr && i < normal_view.count)
            v.normal = read_vec3(normal_view, i);
        if (uv_view.data != nullptr && i < uv_view.count)
            v.texcoord0 = read_vec2(uv_view, i);

        // JOINTS_0 is a vec4 of UNSIGNED_BYTE or UNSIGNED_SHORT.
        // WEIGHTS_0 is a vec4 of float (per glTF spec; we trust tinygltf
        // to normalize byte/short variants into float). Sparse-skin
        // primitives leave the slots at zero.
        if (has_skin && i < joints_view.count && i < weights_view.count)
        {
            auto& sv = out.skin_vertices[i];
            // Component type can be 5121 (u8) or 5123 (u16); both are
            // 4-element vectors. We promote to u16 in our cache.
            const auto* jb = joints_view.data + i * joints_view.stride;
            if (joints_view.component_type == 5121)  // u8
            {
                const auto* p = reinterpret_cast<const std::uint8_t*>(jb);
                sv.joints = { p[0], p[1], p[2], p[3] };
            }
            else  // u16 (5123) or default
            {
                const auto* p = reinterpret_cast<const std::uint16_t*>(jb);
                sv.joints = { p[0], p[1], p[2], p[3] };
            }
            const auto* wb = weights_view.data + i * weights_view.stride;
            const auto* wp = reinterpret_cast<const float*>(wb);
            sv.weights = { wp[0], wp[1], wp[2], wp[3] };
        }
    }

    // Indices: spec allows indices to be absent (non-indexed draw). We always
    // produce an index buffer so the renderer has one code path.
    if (prim.indices >= 0)
    {
        const AccessorView idx_view = access(model, prim.indices);
        out.indices.resize(idx_view.count);
        for (std::size_t i = 0; i < idx_view.count; ++i)
            out.indices[i] = read_index(idx_view, i);
    }
    else
    {
        out.indices.resize(pos_view.count);
        for (std::size_t i = 0; i < pos_view.count; ++i)
            out.indices[i] = static_cast<std::uint32_t>(i);
    }

    return out;
}

// ---- Material / texture decode ----------------------------------------------

[[nodiscard]] GltfMaterial decode_material(const tinygltf::Material& src)
{
    GltfMaterial m;
    m.name = src.name;
    const auto& pbr = src.pbrMetallicRoughness;
    if (pbr.baseColorFactor.size() == 4)
    {
        m.base_color_factor = { static_cast<float>(pbr.baseColorFactor[0]),
                                static_cast<float>(pbr.baseColorFactor[1]),
                                static_cast<float>(pbr.baseColorFactor[2]),
                                static_cast<float>(pbr.baseColorFactor[3]) };
    }
    m.metallic_factor = static_cast<float>(pbr.metallicFactor);
    m.roughness_factor = static_cast<float>(pbr.roughnessFactor);
    m.base_color_texture = pbr.baseColorTexture.index;
    // phase452: full texture-index plumbing for downstream per-prim
    // descriptor arrays. Renderers that don't bind these can ignore the
    // fields; loaders that do (cd::ui_renderer_rhi, future editor PBR)
    // get exactly what the glTF authored.
    m.metallic_roughness_texture = pbr.metallicRoughnessTexture.index;
    m.normal_texture    = src.normalTexture.index;
    m.normal_scale      = static_cast<float>(src.normalTexture.scale);
    m.occlusion_texture = src.occlusionTexture.index;
    m.occlusion_strength = static_cast<float>(src.occlusionTexture.strength);
    m.emissive_texture  = src.emissiveTexture.index;
    if (src.emissiveFactor.size() == 3)
    {
        m.emissive_factor = { static_cast<float>(src.emissiveFactor[0]),
                              static_cast<float>(src.emissiveFactor[1]),
                              static_cast<float>(src.emissiveFactor[2]) };
    }
    m.double_sided = src.doubleSided;
    m.alpha_cutoff = static_cast<float>(src.alphaCutoff);
    if (src.alphaMode == "MASK")
        m.alpha_mode = GltfAlphaMode::kMask;
    else if (src.alphaMode == "BLEND")
        m.alpha_mode = GltfAlphaMode::kBlend;  // treated as MASK with cutoff=0.5 for first cut
    else
        m.alpha_mode = GltfAlphaMode::kOpaque;
    return m;
}

[[nodiscard]] GltfTexture decode_texture(const tinygltf::Model& model, const tinygltf::Texture& tex)
{
    GltfTexture out;
    if (tex.source < 0 || tex.source >= static_cast<int>(model.images.size()))
        return out;
    const auto& img = model.images[static_cast<std::size_t>(tex.source)];
    if (img.image.empty() || img.width <= 0 || img.height <= 0)
        return out;

    out.width = static_cast<std::uint32_t>(img.width);
    out.height = static_cast<std::uint32_t>(img.height);

    // tinygltf decodes to 8-bit RGBA when it owns the decode step (stb_image
    // request_comp = 4). Honour that by padding RGB → RGBA if a caller-side
    // loader produced 3-channel data.
    const std::size_t pixel_count = static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height);
    out.rgba.resize(pixel_count * 4U);
    if (img.component == 4)
    {
        std::memcpy(out.rgba.data(), img.image.data(), out.rgba.size());
    }
    else if (img.component == 3)
    {
        for (std::size_t i = 0; i < pixel_count; ++i)
        {
            out.rgba[i * 4 + 0] = img.image[i * 3 + 0];
            out.rgba[i * 4 + 1] = img.image[i * 3 + 1];
            out.rgba[i * 4 + 2] = img.image[i * 3 + 2];
            out.rgba[i * 4 + 3] = 0xFF;
        }
    }
    else
    {
        // Grayscale / unsupported channel count — fill alpha white, broadcast
        // first channel into RGB so the pipeline gets something deterministic.
        const auto channels = static_cast<std::size_t>(img.component <= 0 ? 1 : img.component);
        for (std::size_t i = 0; i < pixel_count; ++i)
        {
            const std::uint8_t v = img.image[i * channels];
            out.rgba[i * 4 + 0] = v;
            out.rgba[i * 4 + 1] = v;
            out.rgba[i * 4 + 2] = v;
            out.rgba[i * 4 + 3] = 0xFF;
        }
    }
    return out;
}

// Post-parse: convert a tinygltf::Model into our public GltfScene shape.
// Shared between load_gltf (file path) and load_gltf_from_memory (byte buffer).
[[nodiscard]] GltfScene model_to_scene(const tinygltf::Model& model)
{
    GltfScene scene;

    // Materials first so primitives can reference them by index.
    scene.materials.reserve(model.materials.size());
    for (const auto& src : model.materials)
        scene.materials.push_back(decode_material(src));

    scene.textures.reserve(model.textures.size());
    for (const auto& tex : model.textures)
        scene.textures.push_back(decode_texture(model, tex));

    // ---- T1.14: alpha-mode override pass ------------------------------------
    // When the glTF JSON says alphaMode="OPAQUE" but the linked base-color
    // texture has alpha data that suggests a different intent, we infer the
    // correct mode. This handles the Sponza-curtain case where artists export
    // without setting alphaMode explicitly (the spec default is OPAQUE).
    //
    // Override policy:
    //   JSON OPAQUE + inferred kMask  → override to kMask + emit warning.
    //   JSON OPAQUE + inferred kBlend → keep JSON's OPAQUE choice; an artist
    //     who deliberately wants BLEND would have set it in the authoring tool.
    //   JSON MASK / BLEND             → never touched; respect the author.
    for (auto& mat : scene.materials)
    {
        if (mat.alpha_mode != GltfAlphaMode::kOpaque)
            continue;  // JSON had a deliberate non-OPAQUE setting — don't touch.

        if (mat.base_color_texture < 0 ||
            static_cast<std::size_t>(mat.base_color_texture) >= scene.textures.size())
            continue;  // No linked alpha texture → nothing to infer from.

        const auto& tex = scene.textures[static_cast<std::size_t>(mat.base_color_texture)];
        if (tex.rgba.empty())
            continue;  // Texture failed to decode → skip.

        const GltfAlphaMode inferred = infer_alpha_mode(std::span<const std::uint8_t>(tex.rgba));
        if (inferred == GltfAlphaMode::kMask)
        {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
            std::fprintf(
                stderr,
                "[cd::asset::gltf] T1.14 alpha-mode override: material '%s' "
                "JSON=OPAQUE → inferred MASK (alpha histogram >= 95%% fully "
                "opaque with cutout edges). Override applied.\n",
                mat.name.c_str()
            );
            mat.alpha_mode = GltfAlphaMode::kMask;
        }
        // inferred == kBlend: keep OPAQUE per the override policy above.
        // inferred == kOpaque: nothing to do.
    }

    // Meshes — flatten primitives (do NOT compute bbox here; that comes
    // from per-instance world-space accumulation below).
    scene.meshes.reserve(model.meshes.size());
    for (const auto& src : model.meshes)
    {
        GltfMesh mesh;
        mesh.name = src.name;
        mesh.primitives.reserve(src.primitives.size());
        for (const auto& prim : src.primitives)
        {
            mesh.primitives.push_back(decode_primitive(model, prim));
        }
        scene.meshes.push_back(std::move(mesh));
    }

    // ---- Skins (per glTF spec §3.7.3) -------------------------------------
    // Each skin has: joints[] (node indices), optional inverseBindMatrices
    // accessor, optional explicit skeleton root node. We decode the matrix
    // array eagerly so the runtime never touches tinygltf again.
    scene.skins.reserve(model.skins.size());
    for (const auto& src : model.skins)
    {
        GltfSkin sk;
        sk.name = src.name;
        sk.skeleton_root = src.skeleton;
        sk.joints.reserve(src.joints.size());
        for (int j : src.joints)
            sk.joints.push_back(j);

        if (src.inverseBindMatrices >= 0)
        {
            const AccessorView ibm_view = access(model, src.inverseBindMatrices);
            sk.inverse_bind_matrices.resize(ibm_view.count);
            for (std::size_t i = 0; i < ibm_view.count; ++i)
            {
                const auto* p = reinterpret_cast<const float*>(ibm_view.data + i * ibm_view.stride);
                cd::math::Mat4f m {};
                for (std::size_t c = 0; c < 4; ++c)
                    for (std::size_t r = 0; r < 4; ++r)
                        m[c][r] = p[c * 4 + r];
                sk.inverse_bind_matrices[i] = m;
            }
        }
        else
        {
            // Spec: when omitted, each joint's inverse-bind defaults to the
            // identity. Caller can recompute from the node hierarchy if needed.
            sk.inverse_bind_matrices.assign(sk.joints.size(), cd::math::Mat4f::identity());
        }
        scene.skins.push_back(std::move(sk));
    }

    // ---- Animations (SK1, phase 226) ---------------------------------------
    // glTF 2.0 §3.7.4: each animation has samplers (input=time, output=values)
    // + channels (target node + property path = TRS / morph). We decode every
    // sampler eagerly so the runtime never re-touches tinygltf for sampling.
    scene.animations.reserve(model.animations.size());
    for (const auto& src : model.animations)
    {
        GltfAnimation anim;
        anim.name = src.name;
        anim.samplers.reserve(src.samplers.size());
        for (const auto& s : src.samplers)
        {
            GltfAnimSampler smp;
            // Interpolation mode — default LINEAR per spec.
            if (s.interpolation == "STEP")
                smp.interpolation = GltfInterpolation::kStep;
            else if (s.interpolation == "CUBICSPLINE")
                smp.interpolation = GltfInterpolation::kCubicSpline;
            else
                smp.interpolation = GltfInterpolation::kLinear;

            // Input accessor — N keyframe times (FLOAT scalar).
            const AccessorView tv = access(model, s.input);
            smp.times.resize(tv.count);
            for (std::size_t i = 0; i < tv.count; ++i)
            {
                const auto* p = reinterpret_cast<const float*>(tv.data + i * tv.stride);
                smp.times[i] = *p;
            }
            // Output accessor — N * stride floats. tinygltf's GetNumComponentsInType
            // gives 3 (VEC3) or 4 (VEC4); for CUBICSPLINE the keyframe expands to
            // 3 packed entries — we keep the buffer flat and let the bridge layer
            // address it with stride knowledge.
            const AccessorView vv = access(model, s.output);
            const auto comp = static_cast<std::size_t>(
                tinygltf::GetNumComponentsInType(static_cast<std::uint32_t>(vv.type)));
            smp.values.resize(vv.count * comp);
            for (std::size_t i = 0; i < vv.count; ++i)
            {
                const auto* p = reinterpret_cast<const float*>(vv.data + i * vv.stride);
                for (std::size_t c = 0; c < comp; ++c)
                    smp.values[i * comp + c] = p[c];
            }
            if (!smp.times.empty())
                anim.duration = std::max(anim.duration, smp.times.back());
            anim.samplers.push_back(std::move(smp));
        }
        anim.channels.reserve(src.channels.size());
        for (const auto& c : src.channels)
        {
            GltfAnimChannel ch;
            ch.sampler_index = c.sampler;
            ch.target_node = c.target_node;
            if (c.target_path == "translation")
                ch.path = GltfTargetPath::kTranslation;
            else if (c.target_path == "rotation")
                ch.path = GltfTargetPath::kRotation;
            else if (c.target_path == "scale")
                ch.path = GltfTargetPath::kScale;
            else
                ch.path = GltfTargetPath::kMorphWeights;
            anim.channels.push_back(ch);
        }
        scene.animations.push_back(std::move(anim));
    }

    // ---- Node hierarchy ----------------------------------------------------
    // glTF nodes either carry an explicit 4x4 matrix or T/R/S components.
    // We resolve to a single Mat4f per node and stash mesh refs + child IDs.
    scene.nodes.reserve(model.nodes.size());
    for (const auto& src : model.nodes)
    {
        GltfNode n;
        n.name = src.name;
        n.mesh_index = src.mesh;  // -1 if no mesh attached.
        n.skin_index = src.skin;  // -1 if not a skinned mesh node.

        if (src.matrix.size() == 16)
        {
            // glTF stores matrices in column-major order — same as cd::math::Mat4f.
            cd::math::Mat4f m {};
            for (std::size_t c = 0; c < 4; ++c)
            {
                for (std::size_t r = 0; r < 4; ++r)
                {
                    m[c][r] = static_cast<float>(src.matrix[c * 4 + r]);
                }
            }
            n.local_matrix = m;
        }
        else
        {
            // Compose T·R·S — same convention as cd::math::Transform.
            cd::math::Transformf xf;
            if (src.translation.size() == 3)
            {
                xf.position = { static_cast<float>(src.translation[0]),
                                static_cast<float>(src.translation[1]),
                                static_cast<float>(src.translation[2]) };
            }
            if (src.rotation.size() == 4)
            {
                // glTF rotation is (x, y, z, w) — matches cd::math::Quat order.
                xf.rotation = { static_cast<float>(src.rotation[0]),
                                static_cast<float>(src.rotation[1]),
                                static_cast<float>(src.rotation[2]),
                                static_cast<float>(src.rotation[3]) };
            }
            if (src.scale.size() == 3)
            {
                xf.scale = { static_cast<float>(src.scale[0]),
                             static_cast<float>(src.scale[1]),
                             static_cast<float>(src.scale[2]) };
            }
            n.local_matrix = cd::math::to_mat4(xf);
        }

        for (int child : src.children)
            n.children.push_back(child);
        scene.nodes.push_back(std::move(n));
    }

    // Patch parent pointers (glTF only stores children → parents derived).
    for (std::size_t i = 0; i < scene.nodes.size(); ++i)
    {
        for (int child : scene.nodes[i].children)
        {
            if (child >= 0 && static_cast<std::size_t>(child) < scene.nodes.size())
                scene.nodes[static_cast<std::size_t>(child)].parent = static_cast<int>(i);
        }
    }

    // Roots = nodes that no other node lists as a child. If the file has an
    // explicit default scene, use its root list; otherwise fall back to the
    // parent-pointer derivation so files without a `scene` block still work.
    if (model.defaultScene >= 0 && static_cast<std::size_t>(model.defaultScene) < model.scenes.size())
    {
        for (int n : model.scenes[static_cast<std::size_t>(model.defaultScene)].nodes)
            scene.roots.push_back(n);
    }
    else
    {
        for (std::size_t i = 0; i < scene.nodes.size(); ++i)
        {
            if (scene.nodes[i].parent < 0)
                scene.roots.push_back(static_cast<int>(i));
        }
    }

    // ---- Bake flat instance list + world-space bbox -----------------------
    // Recursive walk from each root. The closure captures `scene` by ref so
    // we can append to .instances and update bb_min/bb_max as we go.
    cd::math::Vec3f bb_min { std::numeric_limits<float>::infinity(),
                             std::numeric_limits<float>::infinity(),
                             std::numeric_limits<float>::infinity() };
    cd::math::Vec3f bb_max { -std::numeric_limits<float>::infinity(),
                             -std::numeric_limits<float>::infinity(),
                             -std::numeric_limits<float>::infinity() };
    bool any_vertex = false;

    auto visit = [&](auto& self, int node_idx, const cd::math::Mat4f& parent_world) -> void
    {
        if (node_idx < 0 || static_cast<std::size_t>(node_idx) >= scene.nodes.size())
            return;
        const auto& node = scene.nodes[static_cast<std::size_t>(node_idx)];
        const cd::math::Mat4f world = parent_world * node.local_matrix;

        if (node.mesh_index >= 0 && static_cast<std::size_t>(node.mesh_index) < scene.meshes.size())
        {
            scene.instances.push_back(GltfInstance { node.mesh_index, node_idx, world });
            // Accumulate world-space AABB from each vertex of the referenced mesh.
            const auto& mesh = scene.meshes[static_cast<std::size_t>(node.mesh_index)];
            for (const auto& prim : mesh.primitives)
            {
                for (const auto& v : prim.vertices)
                {
                    const cd::math::Vec4f local { v.position[0], v.position[1], v.position[2], 1.0F };
                    const cd::math::Vec4f w = world * local;
                    bb_min[0] = std::min(bb_min[0], w[0]);
                    bb_min[1] = std::min(bb_min[1], w[1]);
                    bb_min[2] = std::min(bb_min[2], w[2]);
                    bb_max[0] = std::max(bb_max[0], w[0]);
                    bb_max[1] = std::max(bb_max[1], w[1]);
                    bb_max[2] = std::max(bb_max[2], w[2]);
                    any_vertex = true;
                }
            }
        }

        for (int child : node.children)
            self(self, child, world);
    };

    const auto identity = cd::math::Mat4f::identity();
    for (int root : scene.roots)
        visit(visit, root, identity);

    // Edge case: file has no node hierarchy at all (just bare meshes). The
    // legacy fallback wraps every mesh in an implicit identity instance so
    // the renderer still sees something.
    if (scene.instances.empty() && !scene.meshes.empty())
    {
        for (std::size_t i = 0; i < scene.meshes.size(); ++i)
        {
            scene.instances.push_back(GltfInstance { static_cast<int>(i), -1, identity });
            for (const auto& prim : scene.meshes[i].primitives)
            {
                for (const auto& v : prim.vertices)
                {
                    bb_min[0] = std::min(bb_min[0], v.position[0]);
                    bb_min[1] = std::min(bb_min[1], v.position[1]);
                    bb_min[2] = std::min(bb_min[2], v.position[2]);
                    bb_max[0] = std::max(bb_max[0], v.position[0]);
                    bb_max[1] = std::max(bb_max[1], v.position[1]);
                    bb_max[2] = std::max(bb_max[2], v.position[2]);
                    any_vertex = true;
                }
            }
        }
    }

    if (any_vertex)
    {
        scene.bbox_min = bb_min;
        scene.bbox_max = bb_max;
    }
    return scene;
}

}  // namespace

// ---- Public helper: alpha-mode heuristic ------------------------------------

GltfAlphaMode infer_alpha_mode(std::span<const std::uint8_t> rgba_pixels) noexcept
{
    // Each pixel is 4 bytes: R G B A. We only inspect the A channel (byte 3).
    // Empty span = no evidence of translucency → kOpaque.
    if (rgba_pixels.empty())
        return GltfAlphaMode::kOpaque;

    // Require a complete set of RGBA4 pixels.
    const std::size_t pixel_count = rgba_pixels.size() / 4U;
    if (pixel_count == 0U)
        return GltfAlphaMode::kOpaque;

    std::size_t fully_opaque = 0U;
    for (std::size_t i = 0U; i < pixel_count; ++i)
    {
        if (rgba_pixels[i * 4U + 3U] == 0xFFU)
            ++fully_opaque;
    }

    // 100 % fully opaque → kOpaque.
    if (fully_opaque == pixel_count)
        return GltfAlphaMode::kOpaque;

    // Degenerate: all pixels are fully transparent (alpha==0 across the board).
    // This is an invalid / placeholder texture — we have no evidence of
    // semi-transparency intent, so kOpaque is the safe fallback.
    if (fully_opaque == 0U)
        return GltfAlphaMode::kOpaque;

    // >= 95 % at 255 AND at least one < 255 → kMask (cutout edges).
    // Use integer arithmetic: fully_opaque * 20 >= pixel_count * 19
    // avoids floating-point and is exact for any pixel count.
    if (fully_opaque * 20U >= pixel_count * 19U)
        return GltfAlphaMode::kMask;

    // < 95 % fully opaque (but at least one opaque) → mixed / semi-transparent.
    return GltfAlphaMode::kBlend;
}

cd::core::Result<GltfScene> load_gltf(std::string_view path)
{
    if (path.empty())
    {
        return std::unexpected(gltf_errors::make(gltf_errors::Code::kInvalidArgument, "load_gltf: empty path"));
    }
    const std::string p { path };
    if (!std::filesystem::exists(p))
    {
        return std::unexpected(gltf_errors::make(gltf_errors::Code::kFileNotFound, p));
    }

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string err;
    std::string warn;

    const bool is_binary = (p.size() >= 4) && (p.substr(p.size() - 4) == ".glb" || p.substr(p.size() - 4) == ".GLB");
    const bool ok = is_binary ? loader.LoadBinaryFromFile(&model, &err, &warn, p)
                              : loader.LoadASCIIFromFile(&model, &err, &warn, p);
    if (!ok)
    {
        std::string msg = "tinygltf: ";
        msg += err.empty() ? warn : err;
        return std::unexpected(gltf_errors::make(gltf_errors::Code::kParseFailed, msg));
    }

    return model_to_scene(model);
}

cd::core::Result<GltfScene>
load_gltf_from_memory(const std::uint8_t* bytes, std::size_t size, std::string_view base_dir)
{
    if (bytes == nullptr)
    {
        return std::unexpected(gltf_errors::make(gltf_errors::Code::kInvalidArgument, "null buffer"));
    }
    if (size < 4)
    {
        return std::unexpected(gltf_errors::make(gltf_errors::Code::kParseFailed, "buffer too small"));
    }

    // glTF (text) starts with '{'; glb (binary) starts with magic "glTF".
    const bool is_binary = bytes[0] == 'g' && bytes[1] == 'l' && bytes[2] == 'T' && bytes[3] == 'F';

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string err;
    std::string warn;
    const std::string base { base_dir };

    bool ok = false;
    if (is_binary)
    {
        ok = loader.LoadBinaryFromMemory(&model, &err, &warn, bytes, static_cast<unsigned int>(size), base);
    }
    else
    {
        ok = loader.LoadASCIIFromString(
            &model,
            &err,
            &warn,
            reinterpret_cast<const char*>(bytes),
            static_cast<unsigned int>(size),
            base
        );
    }
    if (!ok)
    {
        std::string msg = "tinygltf: ";
        msg += err.empty() ? warn : err;
        return std::unexpected(gltf_errors::make(gltf_errors::Code::kParseFailed, msg));
    }

    return model_to_scene(model);
}

}  // namespace cd::asset::gltf
