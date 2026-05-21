// =============================================================================
// CHROMODYNAMIC — samples/hello_gltf/main.cpp
//
// Loads a glTF / glb file via cd::asset_gltf and renders every mesh with a
// textured Lambertian shader. Exercises:
//   * cd::asset_gltf::load_gltf end-to-end (POSITION + NORMAL + TEXCOORD_0)
//   * GPU upload of each glTF image via staging buffer + copy_buffer_to_image
//   * Material with combined-image-sampler descriptor at set 0 / binding 0
//   * Per-primitive MaterialInstance bound to the matching material's texture
//   * Auto-framing orbit camera derived from the scene AABB
//
// Usage:
//   hello_gltf <path/to/file.gltf|.glb>
// With no path argument, the demo falls back to a built-in 2-triangle quad
// shaded by a 1×1 white default texture so the pipeline still proves end-
// to-end without an external asset.
// =============================================================================
#include <cd/asset_gltf/GltfLoader.hpp>
#include <cd/camera/Camera.hpp>
#include <cd/camera/OrbitController.hpp>
#include <cd/material/Material.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Transform.hpp>
#include <cd/math/Vector.hpp>
#include <cd/platform/Window.hpp>
#include <cd/render/Renderer.hpp>
#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi_vulkan/VulkanDevice.hpp>
#include <cd/shader/Compiler.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

// Vertex layout matches cd::asset_gltf::GltfVertex byte-for-byte so we can
// memcpy each primitive's vertex span straight into a GPU buffer.
struct Vertex
{
    float pos[3];
    float normal[3];
    float uv[2];
};

static_assert(sizeof(Vertex) == sizeof(cd::asset_gltf::GltfVertex), "vertex layout mismatch with GltfVertex");
static_assert(offsetof(Vertex, pos) == offsetof(cd::asset_gltf::GltfVertex, position), "pos offset");
static_assert(offsetof(Vertex, normal) == offsetof(cd::asset_gltf::GltfVertex, normal), "normal offset");
static_assert(offsetof(Vertex, uv) == offsetof(cd::asset_gltf::GltfVertex, texcoord0), "uv offset");

/// Push-constant block — MVP + per-material PBR factors + camera position.
/// Layout matches std140 alignment so the GLSL `push_constant` block below
/// reads it back verbatim. Total 112 bytes — well under the Vulkan minimum
/// guarantee of 128 bytes that every conformant implementation provides.
///   offset  field
///   0       mat4 mvp                (64 B)
///   64      vec4 base_color         (16 B)
///   80      vec4 mr_pad (metallic, roughness, ambient, _)
///   96      vec4 camera_pos (xyz + pad)
struct PushBlock
{
    cd::math::Mat4f mvp;
    std::array<float, 4> base_color;
    std::array<float, 4> mr_pad;
    std::array<float, 4> camera_pos;
};

static_assert(sizeof(PushBlock) == 112, "PushBlock size must match GLSL block layout");

constexpr const char* kVS = R"glsl(
#version 450
layout(push_constant) uniform PC {
  mat4 mvp;
  vec4 base_color;
  vec4 mr_pad;       // x=metallic y=roughness z=ambient w=_
  vec4 camera_pos;   // xyz=world camera position
} pc;
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 0) out vec3 v_world_pos;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec2 v_uv;
void main() {
  // Model matrix is identity for now — world_pos == object-space pos.
  // Once cd::scene lands we'll split MVP into model+vp here.
  v_world_pos = in_pos;
  v_normal = in_normal;
  v_uv = in_uv;
  vec4 clip = pc.mvp * vec4(in_pos, 1.0);
  clip.y = -clip.y;  // Vulkan NDC Y is down; our math is Y-up.
  gl_Position = clip;
}
)glsl";

constexpr const char* kFS = R"glsl(
#version 450
layout(set = 0, binding = 0) uniform sampler2D u_base_color;
layout(push_constant) uniform PC {
  mat4 mvp;
  vec4 base_color;
  vec4 mr_pad;       // x=metallic y=roughness z=ambient w=_
  vec4 camera_pos;
} pc;
layout(location = 0) in  vec3 v_world_pos;
layout(location = 1) in  vec3 v_normal;
layout(location = 2) in  vec2 v_uv;
layout(location = 0) out vec4 out_color;

// Minimal Cook-Torrance: GGX-NDF + Schlick-Fresnel + Smith geometry +
// Lambertian diffuse weighted by (1 - metallic). No image-based lighting
// yet — single directional light + flat ambient.
const float PI = 3.14159265358979;

float D_GGX(float NoH, float a) {
  float a2 = a * a;
  float d  = (NoH * NoH) * (a2 - 1.0) + 1.0;
  return a2 / (PI * d * d + 1e-7);
}

float G_SchlickGGX(float NoV, float k) {
  return NoV / (NoV * (1.0 - k) + k + 1e-7);
}

float G_Smith(float NoV, float NoL, float roughness) {
  // Schlick-GGX with the (r+1)^2/8 remapping commonly used in real-time PBR.
  float r = roughness + 1.0;
  float k = (r * r) / 8.0;
  return G_SchlickGGX(NoV, k) * G_SchlickGGX(NoL, k);
}

vec3 F_Schlick(float HoV, vec3 F0) {
  return F0 + (vec3(1.0) - F0) * pow(clamp(1.0 - HoV, 0.0, 1.0), 5.0);
}

void main() {
  vec4 tex = texture(u_base_color, v_uv);
  vec3 albedo = tex.rgb * pc.base_color.rgb;
  float metallic = clamp(pc.mr_pad.x, 0.0, 1.0);
  float roughness = clamp(pc.mr_pad.y, 0.04, 1.0);  // floor to avoid singular NDF.
  float ambient   = pc.mr_pad.z;

  vec3 N = normalize(v_normal);
  vec3 V = normalize(pc.camera_pos.xyz - v_world_pos);
  vec3 L = normalize(vec3(0.6, 0.8, 0.3));
  vec3 H = normalize(L + V);

  float NoL = max(dot(N, L), 0.0);
  float NoV = max(dot(N, V), 0.0);
  float NoH = max(dot(N, H), 0.0);
  float HoV = max(dot(H, V), 0.0);

  // F0: 0.04 for dielectrics, lerp toward albedo for metals.
  vec3 F0 = mix(vec3(0.04), albedo, metallic);

  float D = D_GGX(NoH, roughness * roughness);
  float G = G_Smith(NoV, NoL, roughness);
  vec3  F = F_Schlick(HoV, F0);
  vec3 specular = (D * G * F) / max(4.0 * NoV * NoL, 1e-4);

  vec3 kS = F;
  vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
  vec3 direct = (kD * albedo / PI + specular) * NoL;

  vec3 amb = albedo * ambient;
  out_color = vec4(direct + amb, tex.a * pc.base_color.a);
}
)glsl";

[[nodiscard]] cd::rhi::BufferHandle
make_upload_buffer(cd::rhi::IDevice& dev, std::span<const std::byte> bytes, cd::rhi::BufferUsage usage)
{
    cd::rhi::BufferDesc bd {};
    bd.size = bytes.size();
    bd.usage = usage;
    bd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    auto r = dev.create_buffer(bd);
    if (!r.has_value())
        return {};
    if (auto u = dev.upload_buffer(*r, 0, bytes); !u.has_value())
    {
        dev.destroy_buffer(*r);
        return {};
    }
    return *r;
}

/// One-shot command-buffer helper: barrier (UNDEFINED → TRANSFER_DST), copy
/// staging → image, barrier (TRANSFER_DST → SHADER_RESOURCE). Caller owns
/// both the staging buffer and the destination image.
[[nodiscard]] bool upload_texture_2d(
    cd::rhi::IDevice& dev,
    cd::rhi::TextureHandle image,
    cd::rhi::BufferHandle staging,
    std::uint32_t w,
    std::uint32_t h
)
{
    auto cmd = dev.create_command_buffer(cd::rhi::QueueType::kGraphics);
    if (cmd == nullptr)
        return false;
    cmd->begin();

    std::array<cd::rhi::TextureBarrier, 1> to_dst {
        cd::rhi::TextureBarrier {
                                 .texture = image,
                                 .from = cd::rhi::ResourceState::kUndefined,
                                 .to = cd::rhi::ResourceState::kTransferDst,
                                 .range = { .base_mip = 0, .mip_count = 1, .base_layer = 0, .layer_count = 1 },
                                 }
    };
    cmd->barrier({}, to_dst);

    std::array<cd::rhi::BufferImageCopyRegion, 1> regions {
        cd::rhi::BufferImageCopyRegion {
                                        .buffer_offset = 0,
                                        .mip_level = 0,
                                        .base_layer = 0,
                                        .layer_count = 1,
                                        .image_offset = { 0, 0, 0 },
                                        .image_extent = { w, h, 1 },
                                        }
    };
    cmd->copy_buffer_to_image(staging, image, regions);

    std::array<cd::rhi::TextureBarrier, 1> to_read {
        cd::rhi::TextureBarrier {
                                 .texture = image,
                                 .from = cd::rhi::ResourceState::kTransferDst,
                                 .to = cd::rhi::ResourceState::kShaderResource,
                                 .range = { .base_mip = 0, .mip_count = 1, .base_layer = 0, .layer_count = 1 },
                                 }
    };
    cmd->barrier({}, to_read);

    cmd->end();
    dev.submit(*cmd);
    dev.wait_idle();
    return true;
}

/// GPU-side companion to one glTF texture. Owns the image, view, and the
/// (transient) staging buffer until the upload completes.
struct GpuTexture
{
    cd::rhi::TextureHandle image {};
    cd::rhi::TextureViewHandle view {};

    void destroy(cd::rhi::IDevice& dev)
    {
        if (view.is_valid())
            dev.destroy_texture_view(view);
        if (image.is_valid())
            dev.destroy_texture(image);
        image = {};
        view = {};
    }
};

[[nodiscard]] bool create_and_upload_texture(
    cd::rhi::IDevice& dev,
    std::span<const std::uint8_t> rgba,
    std::uint32_t w,
    std::uint32_t h,
    GpuTexture& out
)
{
    out.destroy(dev);

    cd::rhi::TextureDesc td {};
    td.type = cd::rhi::TextureType::k2D;
    td.format = cd::rhi::Format::kRGBA8Unorm;
    td.extent = { w, h, 1 };
    td.mip_levels = 1;
    td.array_layers = 1;
    td.usage = cd::rhi::TextureUsage::kSampled | cd::rhi::TextureUsage::kTransferDst;
    td.memory = cd::rhi::MemoryUsage::kGpuOnly;
    auto tex = dev.create_texture(td);
    if (!tex.has_value())
        return false;
    out.image = *tex;

    cd::rhi::TextureViewDesc tvd {};
    tvd.texture = out.image;
    tvd.type = cd::rhi::TextureType::k2D;
    tvd.format = cd::rhi::Format::kRGBA8Unorm;
    tvd.base_mip = 0;
    tvd.mip_count = 1;
    tvd.base_layer = 0;
    tvd.layer_count = 1;
    auto view = dev.create_texture_view(tvd);
    if (!view.has_value())
    {
        out.destroy(dev);
        return false;
    }
    out.view = *view;

    const std::span<const std::byte> px_bytes { reinterpret_cast<const std::byte*>(rgba.data()), rgba.size() };
    const auto staging = make_upload_buffer(dev, px_bytes, cd::rhi::BufferUsage::kTransferSrc);
    if (!staging.is_valid())
    {
        out.destroy(dev);
        return false;
    }
    const bool ok = upload_texture_2d(dev, out.image, staging, w, h);
    dev.destroy_buffer(staging);
    if (!ok)
    {
        out.destroy(dev);
        return false;
    }
    return true;
}

/// Per-frame depth resource — owned by the sample, not the renderer.
struct DepthTarget
{
    cd::rhi::TextureHandle image {};
    cd::rhi::TextureViewHandle view {};

    void destroy(cd::rhi::IDevice& dev)
    {
        if (view.is_valid())
            dev.destroy_texture_view(view);
        if (image.is_valid())
            dev.destroy_texture(image);
        image = {};
        view = {};
    }
};

[[nodiscard]] bool
create_depth_target(cd::rhi::IDevice& dev, cd::rhi::Extent2D size, cd::rhi::Format format, DepthTarget& out)
{
    out.destroy(dev);
    cd::rhi::TextureDesc td {};
    td.type = cd::rhi::TextureType::k2D;
    td.format = format;
    td.extent = { size.width, size.height, 1 };
    td.mip_levels = 1;
    td.array_layers = 1;
    td.usage = cd::rhi::TextureUsage::kDepthStencilAttachment;
    td.memory = cd::rhi::MemoryUsage::kGpuOnly;
    auto img = dev.create_texture(td);
    if (!img.has_value())
        return false;

    cd::rhi::TextureViewDesc vd {};
    vd.texture = *img;
    vd.type = cd::rhi::TextureType::k2D;
    vd.format = format;
    vd.base_mip = 0;
    vd.mip_count = 1;
    vd.base_layer = 0;
    vd.layer_count = 1;
    auto view = dev.create_texture_view(vd);
    if (!view.has_value())
    {
        dev.destroy_texture(*img);
        return false;
    }
    out.image = *img;
    out.view = *view;
    return true;
}

/// One GPU-resident primitive ready to draw + the descriptor set that
/// references its material's texture. MaterialInstance owns the descriptor
/// set; we keep it alive as long as the Drawable.
struct Drawable
{
    cd::rhi::BufferHandle vb {};
    cd::rhi::BufferHandle ib {};
    std::uint32_t index_count { 0 };
    int mesh_index { -1 };  // Source glTF mesh, lets us join Drawables with GltfInstance.
    std::array<float, 4> base_color { 1.0F, 1.0F, 1.0F, 1.0F };
    float metallic { 0.0F };   // glTF default: 0 = pure dielectric.
    float roughness { 1.0F };  // glTF default: 1 = fully rough.
    cd::material::MaterialInstance instance {};

    void destroy(cd::rhi::IDevice& dev)
    {
        // MaterialInstance is RAII; releasing it before the buffers is fine
        // because the descriptor set is independent of the vertex/index data.
        instance = {};
        if (ib.is_valid())
            dev.destroy_buffer(ib);
        if (vb.is_valid())
            dev.destroy_buffer(vb);
        ib = {};
        vb = {};
    }
};

/// Fallback scene when no glTF path is supplied: a single quad with a 1×1
/// white texture, just enough to confirm the textured pipeline cold-starts.
[[nodiscard]] cd::asset_gltf::GltfScene make_fallback_scene()
{
    cd::asset_gltf::GltfScene scene;
    cd::asset_gltf::GltfMesh mesh;
    mesh.name = "fallback_quad";
    cd::asset_gltf::GltfPrimitive prim;
    prim.vertices = {
        cd::asset_gltf::GltfVertex { .position = { -0.5F, -0.5F, 0.0F },
                                    .normal = { 0.0F, 0.0F, 1.0F },
                                    .texcoord0 = { 0.0F, 0.0F } },
        cd::asset_gltf::GltfVertex { .position = { 0.5F, -0.5F, 0.0F },
                                    .normal = { 0.0F, 0.0F, 1.0F },
                                    .texcoord0 = { 1.0F, 0.0F } },
        cd::asset_gltf::GltfVertex { .position = { 0.5F, 0.5F, 0.0F },
                                    .normal = { 0.0F, 0.0F, 1.0F },
                                    .texcoord0 = { 1.0F, 1.0F } },
        cd::asset_gltf::GltfVertex { .position = { -0.5F, 0.5F, 0.0F },
                                    .normal = { 0.0F, 0.0F, 1.0F },
                                    .texcoord0 = { 0.0F, 1.0F } },
    };
    prim.indices = { 0, 1, 2, 0, 2, 3 };
    prim.material_index = -1;
    mesh.primitives.push_back(std::move(prim));
    scene.meshes.push_back(std::move(mesh));
    // Bake the single mesh into a single identity-matrix instance so the
    // render loop (which iterates scene.instances) sees something to draw.
    // Without this the fallback path renders a black window.
    scene.instances.push_back(cd::asset_gltf::GltfInstance { /*mesh_index=*/0,
                                                             /*node_index=*/-1,
                                                             cd::math::Mat4f::identity() });
    scene.bbox_min = { -0.5F, -0.5F, 0.0F };
    scene.bbox_max = { 0.5F, 0.5F, 0.0F };
    return scene;
}

}  // namespace

int main(int argc, char** argv)
{
    // ---- Load scene (file or fallback) ------------------------------------
    cd::asset_gltf::GltfScene scene;
    if (argc >= 2)
    {
        auto loaded = cd::asset_gltf::load_gltf(argv[1]);
        if (!loaded.has_value())
        {
            std::fprintf(
                stderr,
                "gltf: %.*s\n",
                static_cast<int>(loaded.error().message.size()),
                loaded.error().message.data()
            );
            return 1;
        }
        scene = std::move(*loaded);
        std::printf(
            "hello_gltf: loaded %s\n"
            "            meshes=%zu materials=%zu textures=%zu\n",
            argv[1],
            scene.meshes.size(),
            scene.materials.size(),
            scene.textures.size()
        );
    }
    else
    {
        scene = make_fallback_scene();
        std::printf("hello_gltf: no path argument — using built-in fallback quad.\n");
        std::printf("            usage: hello_gltf <path/to/file.gltf|.glb>\n");
    }
    std::fflush(stdout);

    // ---- Window + device + renderer ---------------------------------------
    cd::platform::WindowDesc wd {};
    wd.title = "CHROMODYNAMIC — hello_gltf";
    wd.width = 1280;
    wd.height = 720;
    auto window_r = cd::platform::create_window(wd);
    if (!window_r.has_value())
    {
        std::fprintf(
            stderr,
            "window: %.*s\n",
            static_cast<int>(window_r.error().message.size()),
            window_r.error().message.data()
        );
        return 2;
    }
    auto& window = **window_r;

    cd::rhi_vulkan::VulkanCreateInfo vci {};
    auto device_r = cd::rhi_vulkan::create_vulkan_device(vci);
    if (!device_r.has_value())
    {
        std::fprintf(
            stderr,
            "device: %.*s\n",
            static_cast<int>(device_r.error().message.size()),
            device_r.error().message.data()
        );
        return 3;
    }
    auto& device = **device_r;

    cd::render::RendererDesc rd {};
    rd.device = &device;
    rd.swapchain.window_handle = window.native_window_handle();
    rd.swapchain.display_handle = window.native_display_handle();
    rd.swapchain.extent = { window.width(), window.height() };
    rd.swapchain.format = cd::rhi::Format::kBGRA8Unorm;
    rd.frames_in_flight = 2;
    auto renderer_r = cd::render::Renderer::create(rd);
    if (!renderer_r.has_value())
    {
        std::fprintf(
            stderr,
            "renderer: %.*s\n",
            static_cast<int>(renderer_r.error().message.size()),
            renderer_r.error().message.data()
        );
        return 4;
    }
    auto& renderer = *renderer_r;

    // ---- Depth target -----------------------------------------------------
    constexpr auto kDepthFormat = cd::rhi::Format::kD32Float;
    DepthTarget depth {};
    if (!create_depth_target(device, { window.width(), window.height() }, kDepthFormat, depth))
    {
        std::fprintf(stderr, "depth create failed\n");
        return 5;
    }
    bool depth_initialized_on_gpu = false;

    // ---- 1×1 white default texture ----------------------------------------
    // Primitives whose material has no base-color texture (or no material at
    // all) bind this so the shader's `texture()` call always returns a
    // multiplicative identity. The alternative — branching in the shader on
    // a "has-texture" flag — would split the pipeline.
    constexpr std::array<std::uint8_t, 4> kWhitePixel { 0xFF, 0xFF, 0xFF, 0xFF };
    GpuTexture white_tex {};
    if (!create_and_upload_texture(device, kWhitePixel, 1, 1, white_tex))
    {
        std::fprintf(stderr, "default white texture upload failed\n");
        return 6;
    }

    // ---- Upload every glTF texture ----------------------------------------
    std::vector<GpuTexture> gpu_textures;
    gpu_textures.resize(scene.textures.size());
    for (std::size_t i = 0; i < scene.textures.size(); ++i)
    {
        const auto& src = scene.textures[i];
        if (src.rgba.empty() || src.width == 0 || src.height == 0)
            continue;  // leave entry default-constructed → falls back to white below.
        if (!create_and_upload_texture(device, src.rgba, src.width, src.height, gpu_textures[i]))
        {
            std::fprintf(stderr, "warning: texture %zu upload failed — using white fallback\n", i);
        }
    }
    std::printf("hello_gltf: uploaded %zu texture(s).\n", scene.textures.size());

    // ---- Shared sampler ---------------------------------------------------
    cd::rhi::SamplerDesc sdesc {};
    sdesc.mag_filter = cd::rhi::SamplerFilter::kLinear;
    sdesc.min_filter = cd::rhi::SamplerFilter::kLinear;
    sdesc.mipmap_mode = cd::rhi::SamplerMipmapMode::kLinear;
    sdesc.address_u = cd::rhi::SamplerAddressMode::kRepeat;
    sdesc.address_v = cd::rhi::SamplerAddressMode::kRepeat;
    sdesc.address_w = cd::rhi::SamplerAddressMode::kRepeat;
    auto samp_r = device.create_sampler(sdesc);
    if (!samp_r.has_value())
    {
        std::fprintf(
            stderr,
            "sampler: %.*s\n",
            static_cast<int>(samp_r.error().message.size()),
            samp_r.error().message.data()
        );
        return 7;
    }
    const auto sampler = *samp_r;

    // ---- Material with descriptor binding ---------------------------------
    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr)
    {
        std::fprintf(stderr, "no glslang\n");
        return 8;
    }

    constexpr std::array<cd::rhi::VertexBinding, 1> kBindings {
        cd::rhi::VertexBinding { 0, sizeof(Vertex), false }
    };
    constexpr std::array<cd::rhi::VertexAttribute, 3> kAttrs {
        cd::rhi::VertexAttribute { 0, 0, cd::rhi::Format::kRGB32Float, offsetof(Vertex, pos)    },
        cd::rhi::VertexAttribute { 1, 0, cd::rhi::Format::kRGB32Float, offsetof(Vertex, normal) },
        cd::rhi::VertexAttribute { 2, 0, cd::rhi::Format::kRG32Float,  offsetof(Vertex, uv)     },
    };
    constexpr std::array<cd::rhi::Format, 1> kColorFormats { cd::rhi::Format::kBGRA8Unorm };
    constexpr std::array<cd::rhi::DescriptorSetLayoutBinding, 1> kDescBindings {
        cd::rhi::DescriptorSetLayoutBinding { .binding = 0,
                                             .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                             .count = 1,
                                             .stages = cd::rhi::ShaderStage::kFragment }
    };
    constexpr std::array<cd::rhi::PushConstantRange, 1> kPush {
        cd::rhi::PushConstantRange { .stages = cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                                    .offset = 0,
                                    .size = static_cast<std::uint32_t>(sizeof(PushBlock)) }
    };

    cd::material::MaterialDesc md {};
    md.vertex_glsl = kVS;
    md.fragment_glsl = kFS;
    md.vertex_bindings = kBindings;
    md.vertex_attributes = kAttrs;
    md.descriptor_bindings = kDescBindings;
    md.color_attachment_formats = kColorFormats;
    md.depth_attachment_format = kDepthFormat;
    md.push_constants = kPush;
    md.topology = cd::rhi::PrimitiveTopology::kTriangleList;
    md.raster.cull = cd::rhi::CullMode::kNone;
    md.depth_stencil.depth_test = true;
    md.depth_stencil.depth_write = true;
    md.depth_stencil.depth_compare = cd::rhi::CompareOp::kLess;
    md.name = "gltf_textured";
    auto material_r = cd::material::Material::create(device, compiler.get(), md);
    if (!material_r.has_value())
    {
        std::fprintf(
            stderr,
            "material: %.*s\n",
            static_cast<int>(material_r.error().message.size()),
            material_r.error().message.data()
        );
        return 9;
    }
    auto& material = *material_r;

    // ---- Upload every primitive + allocate its descriptor set -------------
    // One Drawable per (mesh, primitive). We tag with `mesh_index` so the
    // render loop can join each scene instance's world matrix to the right
    // GPU buffers.
    std::vector<Drawable> drawables;
    drawables.reserve(16);
    for (std::size_t m = 0; m < scene.meshes.size(); ++m)
    {
        const auto& mesh = scene.meshes[m];
        for (const auto& prim : mesh.primitives)
        {
            if (prim.vertices.empty() || prim.indices.empty())
                continue;

            Drawable d {};
            d.mesh_index = static_cast<int>(m);
            const std::span<const std::byte> vb_bytes { reinterpret_cast<const std::byte*>(prim.vertices.data()),
                                                        prim.vertices.size() * sizeof(cd::asset_gltf::GltfVertex) };
            const std::span<const std::byte> ib_bytes { reinterpret_cast<const std::byte*>(prim.indices.data()),
                                                        prim.indices.size() * sizeof(std::uint32_t) };
            d.vb = make_upload_buffer(device, vb_bytes, cd::rhi::BufferUsage::kVertex);
            d.ib = make_upload_buffer(device, ib_bytes, cd::rhi::BufferUsage::kIndex);
            d.index_count = static_cast<std::uint32_t>(prim.indices.size());

            // Resolve material → base_color factor and (optional) texture.
            cd::rhi::TextureViewHandle bound_view = white_tex.view;
            if (prim.material_index >= 0 && static_cast<std::size_t>(prim.material_index) < scene.materials.size())
            {
                const auto& mat = scene.materials[static_cast<std::size_t>(prim.material_index)];
                d.base_color = mat.base_color_factor;
                d.metallic = mat.metallic_factor;
                d.roughness = mat.roughness_factor;
                if (mat.base_color_texture >= 0 &&
                    static_cast<std::size_t>(mat.base_color_texture) < gpu_textures.size() &&
                    gpu_textures[static_cast<std::size_t>(mat.base_color_texture)].view.is_valid())
                {
                    bound_view = gpu_textures[static_cast<std::size_t>(mat.base_color_texture)].view;
                }
            }

            if (!d.vb.is_valid() || !d.ib.is_valid())
            {
                std::fprintf(stderr, "buffer upload failed for primitive\n");
                d.destroy(device);
                continue;
            }

            auto inst_r = cd::material::MaterialInstance::create(device, material);
            if (!inst_r.has_value())
            {
                std::fprintf(
                    stderr,
                    "instance: %.*s\n",
                    static_cast<int>(inst_r.error().message.size()),
                    inst_r.error().message.data()
                );
                d.destroy(device);
                continue;
            }
            d.instance = std::move(*inst_r);

            std::array<cd::rhi::DescriptorWrite, 1> writes {
                cd::rhi::DescriptorWrite { .binding = 0,
                                          .array_element = 0,
                                          .type = cd::rhi::DescriptorType::kCombinedImageSampler,
                                          .buffer = {},
                                          .buffer_offset = 0,
                                          .buffer_range = 0,
                                          .view = bound_view,
                                          .sampler = sampler }
            };
            if (auto wr = d.instance.update(writes); !wr.has_value())
            {
                std::fprintf(
                    stderr,
                    "desc update: %.*s\n",
                    static_cast<int>(wr.error().message.size()),
                    wr.error().message.data()
                );
                d.destroy(device);
                continue;
            }
            drawables.push_back(std::move(d));
        }
    }
    if (drawables.empty())
    {
        std::fprintf(stderr, "no drawable primitives — bailing out\n");
        return 10;
    }
    std::printf("hello_gltf: uploaded %zu primitive(s).\n", drawables.size());
    std::fflush(stdout);

    // ---- Camera + orbit controller (auto-framed to scene AABB) ------------
    // Local var named `cam` (not `camera`) so it does NOT shadow the
    // `cd::camera` namespace when we reference helpers like
    // `cd::camera::view_projection(cam, aspect)` below.
    cd::camera::Camera cam = cd::camera::auto_frame_aabb(scene.bbox_min, scene.bbox_max);
    cd::camera::OrbitController orbit {};
    orbit.sync_from_camera(cam);  // pick up the auto-frame radius/angles
    orbit.auto_spin_rate = 0.6F;  // matches the previous hand-rolled angle = elapsed*0.6F

    std::printf("hello_gltf: ready. ESC or close to exit.\n");
    std::fflush(stdout);

    // ---- Main loop --------------------------------------------------------
    std::vector<cd::platform::OSEvent> events;
    events.reserve(64);
    bool needs_rebuild = false;
    auto rebuild = [&]
    {
        if (window.width() == 0 || window.height() == 0)
            return false;
        if (!renderer.recreate_swapchain({ window.width(), window.height() }).has_value())
            return false;
        if (!create_depth_target(device, { window.width(), window.height() }, kDepthFormat, depth))
            return false;
        depth_initialized_on_gpu = false;
        needs_rebuild = false;
        return true;
    };

    auto t_prev = std::chrono::steady_clock::now();
    while (true)
    {
        events.clear();
        if (!window.pump_events(events))
            break;
        for (const auto& e : events)
        {
            if (e.kind == cd::platform::OSEventKind::kKeyDown && e.key == cd::platform::KeyCode::kEscape)
                window.request_close();
            else if (e.kind == cd::platform::OSEventKind::kResize)
                needs_rebuild = true;
        }
        if (needs_rebuild && !rebuild())
            continue;

        auto frame_r = renderer.begin_frame();
        if (!frame_r.has_value())
        {
            if (frame_r.error().code ==
                static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            {
                needs_rebuild = true;
                continue;
            }
            std::fprintf(
                stderr,
                "begin_frame: %.*s\n",
                static_cast<int>(frame_r.error().message.size()),
                frame_r.error().message.data()
            );
            return 11;
        }
        auto& frame = *frame_r;
        auto& cmd = *frame.command_buffer;

        if (!depth_initialized_on_gpu)
        {
            std::array<cd::rhi::TextureBarrier, 1> dbar {
                cd::rhi::TextureBarrier {
                                         .texture = depth.image,
                                         .from = cd::rhi::ResourceState::kUndefined,
                                         .to = cd::rhi::ResourceState::kDepthWrite,
                                         .range = { .base_mip = 0, .mip_count = 1, .base_layer = 0, .layer_count = 1 },
                                         }
            };
            cmd.barrier({}, dbar);
            depth_initialized_on_gpu = true;
        }

        std::array<cd::rhi::ColorAttachmentInfo, 1> color_attach {
            cd::rhi::ColorAttachmentInfo {
                                          .view = frame.swapchain_image_view,
                                          .load_op = cd::rhi::LoadOp::kClear,
                                          .store_op = cd::rhi::StoreOp::kStore,
                                          .clear_color = { .f32 = { 0.06F, 0.07F, 0.10F, 1.0F } },
                                          }
        };
        cd::rhi::DepthStencilAttachmentInfo depth_attach {};
        depth_attach.view = depth.view;
        depth_attach.depth_load = cd::rhi::LoadOp::kClear;
        depth_attach.depth_store = cd::rhi::StoreOp::kStore;
        depth_attach.clear.depth = 1.0F;
        cd::rhi::RenderPassBeginInfo rp {};
        rp.render_area = cd::rhi::Rect2D {
            { 0, 0 },
            frame.extent
        };
        rp.color_attachments = color_attach;
        rp.depth_stencil = &depth_attach;
        cmd.begin_render_pass(rp);
        cmd.set_viewport(
            cd::rhi::Viewport { 0.0F,
                                0.0F,
                                static_cast<float>(frame.extent.width),
                                static_cast<float>(frame.extent.height),
                                0.0F,
                                1.0F }
        );
        cmd.set_scissor(
            cd::rhi::Rect2D {
                { 0, 0 },
                frame.extent
        }
        );

        // Drive the orbit controller from real elapsed time so the spin
        // speed stays the same regardless of frame rate / pauses.
        const auto t_now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(t_now - t_prev).count();
        t_prev = t_now;
        orbit.update_auto(cam, dt);

        const float aspect = static_cast<float>(frame.extent.width) / static_cast<float>(frame.extent.height);
        const cd::math::Mat4f view_proj = cd::camera::view_projection(cam, aspect);
        const cd::math::Vec3f eye = cd::camera::world_position(cam);

        material.apply(cmd);
        // Iterate the baked instance list — each entry pairs a mesh_index
        // with the node's world matrix. We draw EVERY drawable whose
        // mesh_index matches, so a mesh referenced by N nodes renders N
        // times with N different transforms (classic glTF instancing).
        for (const auto& inst : scene.instances)
        {
            for (const auto& d : drawables)
            {
                if (d.mesh_index != inst.mesh_index)
                    continue;
                PushBlock pb {};
                pb.mvp = view_proj * inst.world_matrix;
                pb.base_color = d.base_color;
                // (metallic, roughness, ambient, _) packed into one vec4 to fit
                // std140 alignment without padding gymnastics. Ambient is a flat
                // term so dark sides of unlit objects still read.
                pb.mr_pad = { d.metallic, d.roughness, 0.08F, 0.0F };
                pb.camera_pos = { eye[0], eye[1], eye[2], 1.0F };
                cmd.push_constants(
                    material.pipeline_layout(),
                    cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                    /*offset=*/0,
                    static_cast<std::uint32_t>(sizeof(pb)),
                    &pb
                );
                d.instance.bind(cmd, /*set_index=*/0);
                cmd.bind_vertex_buffer(0, d.vb, 0);
                cmd.bind_index_buffer(d.ib, 0, cd::rhi::IndexType::kUInt32);
                cmd.draw_indexed(d.index_count, /*instance_count=*/1, 0, 0, 0);
            }
        }
        cmd.end_render_pass();

        auto end_r = renderer.end_frame();
        if (!end_r.has_value())
        {
            if (end_r.error().code == static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            {
                needs_rebuild = true;
                continue;
            }
            std::fprintf(
                stderr,
                "end_frame: %.*s\n",
                static_cast<int>(end_r.error().message.size()),
                end_r.error().message.data()
            );
            return 12;
        }
    }

    renderer.wait_idle();
    for (auto& d : drawables)
        d.destroy(device);
    for (auto& t : gpu_textures)
        t.destroy(device);
    white_tex.destroy(device);
    device.destroy_sampler(sampler);
    depth.destroy(device);
    std::printf("hello_gltf: clean exit.\n");
    return 0;
}
