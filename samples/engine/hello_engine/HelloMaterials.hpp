// =============================================================================
// HelloMaterials.hpp
// -----------------------------------------------------------------------------
// hello_engine-local all-scene-pipelines aggregate. Builds the 7 materials
// (sky / composite / bloom prefilter / bloom downsample / bloom upsample /
// prim / velocity / shadow) the sample uses for the HDR-MRT-composite frame
// graph, plus the shadow depth-only pipeline. Lifted out of main() in
// Marathon Run 13 phase N15b.
//
// Why one aggregate?  All seven materials share the same MRT attachment
// layout (kColorFmts) for the HDR pass + the same vertex layout (kPrimBindings
// / kPrimAttrs) for the prim / velocity / shadow rasterization passes.
// Threading them as 7 separate locals took ~340 lines of boot boilerplate in
// main(); now the call site is a single spawn_materials() returning a
// MaterialBundle with cd::material::Material moved into each field.
//
// Rendering behaviour: unchanged.  Same MaterialDesc inputs, same raster +
// depth-stencil + blend settings, same per-failure exit codes (7 / 32 / 40 /
// 41 / 42 / 9 / 52 / 10 preserved via spawn_materials int rc).
// =============================================================================
#pragma once

#include "PrimShader.hpp"
#include "HelloLighting.hpp"

#include <cd/asset/Primitives.hpp>
#include <cd/material/AnalyticalSkyMaterial.hpp>
#include <cd/material/Material.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/post/bloom/Bloom.hpp>
#include <cd/post/composite/Composite.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/shader/Compiler.hpp>
#include <cd/velocity/Velocity.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <string_view>
#include <utility>

namespace cd_sample {

struct MaterialBundle
{
    cd::material::Material sky               {};
    cd::material::Material composite         {};
    cd::material::Material bloom_prefilter   {};
    cd::material::Material bloom_downsample  {};
    cd::material::Material bloom_upsample    {};
    cd::material::Material prim              {};
    cd::material::Material velocity          {};
    cd::material::Material shadow            {};
};

struct MaterialSpawnError
{
    int         exit_code { 0 };
    const char* name      { "" };
};

// -- X5 / M1 hot-reload integration -----------------------------------
// On-disk source paths for the prim and shadow materials' vertex +
// fragment shaders. Resolved relative to the executable directory;
// the CMake POST_BUILD rule copies shaders/ next to the exe so these
// paths work from any cwd (bin/Debug/, bin/Release/, etc.).
// If a path fails to open, Material::create logs a warning and falls
// back to the embedded GLSL strings (see ADR-20260529-X5 fallback).
inline constexpr std::string_view kPrimVertGlslPath =
    "shaders/prim.vert.glsl";
inline constexpr std::string_view kPrimFragGlslPath =
    "shaders/prim.frag.glsl";

// D-F6: on-disk paths for the shadow depth-only material.
inline constexpr std::string_view kShadowVertGlslPath =
    "shaders/shadow.vert.glsl";
inline constexpr std::string_view kShadowFragGlslPath =
    "shaders/shadow.frag.glsl";

/// Build the full prim MaterialDesc and call Material::create. Shared
/// between initial spawn (spawn_materials) and hot-reload (HelloShaderWatch).
/// On success overwrites *prim_material and returns true; on failure
/// leaves *prim_material untouched and returns false so the previous
/// pipeline keeps rendering.
[[nodiscard]] inline bool
prim_recreate(cd::rhi::IDevice&         device,
              cd::shader::ICompiler*    compiler,
              cd::material::Material*   prim_material)
{
    constexpr std::array<cd::rhi::Format, 4> kColorFmts {
        cd::rhi::Format::kRGBA16Float,
        cd::rhi::Format::kRGBA16Float,
        cd::rhi::Format::kRGBA8Unorm,
        cd::rhi::Format::kRG8Unorm
    };
    constexpr std::array<cd::rhi::VertexBinding, 1> kPrimBindings {
        cd::rhi::VertexBinding { 0, sizeof(cd::asset::PrimitiveVertex), false }
    };
    constexpr std::array<cd::rhi::VertexAttribute, 4> kPrimAttrs {
        cd::rhi::VertexAttribute { 0, 0, cd::rhi::Format::kRGB32Float, offsetof(cd::asset::PrimitiveVertex, pos)    },
        cd::rhi::VertexAttribute { 1, 0, cd::rhi::Format::kRGB32Float, offsetof(cd::asset::PrimitiveVertex, normal) },
        cd::rhi::VertexAttribute { 2, 0, cd::rhi::Format::kRG32Float,  offsetof(cd::asset::PrimitiveVertex, uv)     },
        cd::rhi::VertexAttribute { 3, 0, cd::rhi::Format::kRGB32Float, offsetof(cd::asset::PrimitiveVertex, color)  }
    };
    constexpr std::array<cd::rhi::PushConstantRange, 1> kPush {
        cd::rhi::PushConstantRange {
            .stages = cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
            .offset = 0,
            .size   = static_cast<std::uint32_t>(sizeof(cd::hello_engine::PrimPush)) }
    };
    // phase842b-W8-BE-rt-bindless-texture-sampling: three new bindings
    // for the ray-side texture-sampling path:
    //   11 — Sponza vertex buffer (PrimitiveVertex array) as storage
    //        buffer. Read by the chrome reflection branch to recover
    //        per-vertex UV at a ray hit.
    //   12 — Sponza index buffer (uint32) as storage buffer. Looks up
    //        the 3 vertex indices of the hit triangle.
    //   13 — bindless sampler2D array (kBindlessSampledImage). Last
    //        binding in the set; VARIABLE_DESCRIPTOR_COUNT lights up.
    //        Per-prim albedo textures get written to slots by the
    //        host-side wiring in phase843.
    //
    // Non-Sponza prims keep the W8-BD avg-colour path; they never
    // sample bindings 11-13.
    constexpr std::array<cd::rhi::DescriptorSetLayoutBinding, 14> kBindings {
        cd::rhi::DescriptorSetLayoutBinding { .binding = 0,  .type = cd::rhi::DescriptorType::kUniformBuffer,         .count = 1, .stages = cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment },
        cd::rhi::DescriptorSetLayoutBinding { .binding = 1,  .type = cd::rhi::DescriptorType::kCombinedImageSampler,  .count = 1, .stages = cd::rhi::ShaderStage::kFragment },
        cd::rhi::DescriptorSetLayoutBinding { .binding = 2,  .type = cd::rhi::DescriptorType::kAccelerationStructure, .count = 1, .stages = cd::rhi::ShaderStage::kFragment },
        cd::rhi::DescriptorSetLayoutBinding { .binding = 3,  .type = cd::rhi::DescriptorType::kUniformBuffer,         .count = 1, .stages = cd::rhi::ShaderStage::kFragment },
        cd::rhi::DescriptorSetLayoutBinding { .binding = 4,  .type = cd::rhi::DescriptorType::kCombinedImageSampler,  .count = 1, .stages = cd::rhi::ShaderStage::kFragment },
        cd::rhi::DescriptorSetLayoutBinding { .binding = 5,  .type = cd::rhi::DescriptorType::kCombinedImageSampler,  .count = 1, .stages = cd::rhi::ShaderStage::kFragment },
        cd::rhi::DescriptorSetLayoutBinding { .binding = 6,  .type = cd::rhi::DescriptorType::kCombinedImageSampler,  .count = 1, .stages = cd::rhi::ShaderStage::kFragment },
        cd::rhi::DescriptorSetLayoutBinding { .binding = 7,  .type = cd::rhi::DescriptorType::kCombinedImageSampler,  .count = 1, .stages = cd::rhi::ShaderStage::kFragment },
        cd::rhi::DescriptorSetLayoutBinding { .binding = 8,  .type = cd::rhi::DescriptorType::kCombinedImageSampler,  .count = 1, .stages = cd::rhi::ShaderStage::kFragment },
        cd::rhi::DescriptorSetLayoutBinding { .binding = 9,  .type = cd::rhi::DescriptorType::kCombinedImageSampler,  .count = 1, .stages = cd::rhi::ShaderStage::kFragment },
        cd::rhi::DescriptorSetLayoutBinding { .binding = 10, .type = cd::rhi::DescriptorType::kStorageBuffer,         .count = 1, .stages = cd::rhi::ShaderStage::kFragment },
        cd::rhi::DescriptorSetLayoutBinding { .binding = 11, .type = cd::rhi::DescriptorType::kStorageBuffer,         .count = 1, .stages = cd::rhi::ShaderStage::kFragment },
        cd::rhi::DescriptorSetLayoutBinding { .binding = 12, .type = cd::rhi::DescriptorType::kStorageBuffer,         .count = 1, .stages = cd::rhi::ShaderStage::kFragment },
        cd::rhi::DescriptorSetLayoutBinding { .binding = 13, .type = cd::rhi::DescriptorType::kBindlessSampledImage,  .count = 256, .stages = cd::rhi::ShaderStage::kFragment, .bindless = true },
    };

    cd::material::MaterialDesc md {};
    // _glsl_path wins over _glsl per ADR-20260529-X5. The embedded
    // strings remain as a documented fallback for shipped binaries
    // launched without the on-disk shaders folder.
    // D-F7: guarded by the HELLO_ENGINE_USE_ON_DISK_SHADERS CMake option
    // (default ON). When OFF the on-disk paths are not set and the
    // embedded strings are used directly.
#if HELLO_ENGINE_USE_ON_DISK_SHADERS
    md.vertex_glsl_path   = kPrimVertGlslPath;
    md.fragment_glsl_path = kPrimFragGlslPath;
#endif
    md.vertex_glsl        = cd::hello_engine::kPrimVS;
    md.fragment_glsl      = cd::hello_engine::kPrimFS;
    md.color_attachment_formats = kColorFmts;
    md.depth_attachment_format  = cd::rhi::Format::kD32Float;
    md.vertex_bindings    = kPrimBindings;
    md.vertex_attributes  = kPrimAttrs;
    md.push_constants     = kPush;
    md.descriptor_bindings = kBindings;
    md.raster.cull = cd::rhi::CullMode::kNone;
    md.depth_stencil.depth_test = true;
    md.depth_stencil.depth_write = true;
    md.depth_stencil.depth_compare = cd::rhi::CompareOp::kLess;
    md.name = "hello_engine/prim";
    auto r = cd::material::Material::create(device, compiler, md);
    if (!r.has_value())
    {
        std::fprintf(
            stderr,
            "hello_engine: prim_material create failed: %.*s\n",
            static_cast<int>(r.error().message.size()),
            r.error().message.data()
        );
        return false;
    }
    *prim_material = std::move(*r);
    return true;
}


/// Build the shadow MaterialDesc and call Material::create. Shared
/// between initial spawn (spawn_materials) and hot-reload (HelloShaderWatch).
/// On success overwrites *shadow_material and returns true; on failure
/// leaves *shadow_material untouched and returns false so the previous
/// pipeline keeps rendering. Mirrors prim_recreate() — see D-F6.
[[nodiscard]] inline bool
shadow_recreate(cd::rhi::IDevice&       device,
                cd::shader::ICompiler* compiler,
                cd::material::Material* shadow_material)
{
    constexpr std::array<cd::rhi::VertexBinding, 1> kBindings {
        cd::rhi::VertexBinding { 0, sizeof(cd::asset::PrimitiveVertex), false }
    };
    constexpr std::array<cd::rhi::VertexAttribute, 4> kAttrs {
        cd::rhi::VertexAttribute { 0, 0, cd::rhi::Format::kRGB32Float, offsetof(cd::asset::PrimitiveVertex, pos)    },
        cd::rhi::VertexAttribute { 1, 0, cd::rhi::Format::kRGB32Float, offsetof(cd::asset::PrimitiveVertex, normal) },
        cd::rhi::VertexAttribute { 2, 0, cd::rhi::Format::kRG32Float,  offsetof(cd::asset::PrimitiveVertex, uv)     },
        cd::rhi::VertexAttribute { 3, 0, cd::rhi::Format::kRGB32Float, offsetof(cd::asset::PrimitiveVertex, color)  }
    };
    constexpr std::array<cd::rhi::PushConstantRange, 1> kPush {
        cd::rhi::PushConstantRange { .stages = cd::rhi::ShaderStage::kVertex,
                                     .offset = 0,
                                     .size   = static_cast<std::uint32_t>(sizeof(cd::math::Mat4f)) }
    };
    cd::material::MaterialDesc md {};
#if HELLO_ENGINE_USE_ON_DISK_SHADERS
    // D-F6: on-disk paths win over embedded strings per ADR-20260529-X5
    // precedence rule (_glsl_path > _glsl). Embedded strings stay as
    // the fallback for shipped binaries without the shaders folder.
    md.vertex_glsl_path   = kShadowVertGlslPath;
    md.fragment_glsl_path = kShadowFragGlslPath;
#endif
    md.vertex_glsl   = cd::hello_engine::kShadowVS;
    md.fragment_glsl = cd::hello_engine::kShadowFS;
    md.color_attachment_formats = {};
    md.depth_attachment_format  = cd::rhi::Format::kD32Float;
    md.vertex_bindings   = kBindings;
    md.vertex_attributes = kAttrs;
    md.push_constants = kPush;
    md.raster.cull = cd::rhi::CullMode::kBack;
    md.raster.depth_bias_enable   = true;
    md.raster.depth_bias_constant = 1.25F;
    md.raster.depth_bias_slope    = 1.75F;
    md.depth_stencil.depth_test    = true;
    md.depth_stencil.depth_write   = true;
    md.depth_stencil.depth_compare = cd::rhi::CompareOp::kLess;
    md.name = "hello_engine/shadow";
    auto r = cd::material::Material::create(device, compiler, md);
    if (!r.has_value())
    {
        std::fprintf(
            stderr,
            "hello_engine: shadow_material create failed: %.*s\n",
            static_cast<int>(r.error().message.size()),
            r.error().message.data()
        );
        return false;
    }
    *shadow_material = std::move(*r);
    return true;
}


[[nodiscard]] inline std::expected<MaterialBundle, MaterialSpawnError>
spawn_materials(cd::rhi::IDevice&             device,
                cd::shader::ICompiler*        compiler)
{
    constexpr std::array<cd::rhi::Format, 4> kColorFmts {
        cd::rhi::Format::kRGBA16Float,
        cd::rhi::Format::kRGBA16Float,
        cd::rhi::Format::kRGBA8Unorm,
        cd::rhi::Format::kRG8Unorm
    };

    MaterialBundle out {};

    // ---- Sky material -------------------------------------------------------
    {
        cd::material::MaterialDesc md {};
        md.vertex_glsl = cd::material::kAnalyticalSkyVS;
        md.fragment_glsl = cd::material::kAnalyticalSkyFS;
        md.color_attachment_formats = kColorFmts;
        constexpr std::array<cd::rhi::PushConstantRange, 1> kPush {
            cd::rhi::PushConstantRange {
                .stages = cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                .offset = 0,
                .size = static_cast<std::uint32_t>(sizeof(cd::material::AnalyticalSkyPush)) }
        };
        md.push_constants = kPush;
        md.raster.cull = cd::rhi::CullMode::kNone;
        md.depth_stencil.depth_test = false;
        md.depth_stencil.depth_write = false;
        md.name = "hello_engine/sky";
        auto r = cd::material::Material::create(device, compiler, md);
        if (!r.has_value())
            return std::unexpected(MaterialSpawnError { 7, "sky" });
        out.sky = std::move(*r);
    }

    // ---- Composite material -------------------------------------------------
    {
        constexpr std::array<cd::rhi::Format, 2> kCompFmts {
            cd::rhi::Format::kBGRA8Unorm,
            cd::rhi::Format::kBGRA8Unorm
        };
        cd::material::MaterialDesc md {};
        md.vertex_glsl = cd::post::composite::kCompositeVS;
        md.fragment_glsl = cd::post::composite::kCompositeFS;
        md.color_attachment_formats = kCompFmts;
        constexpr std::array<cd::rhi::PushConstantRange, 1> kPush {
            cd::rhi::PushConstantRange {
                .stages = cd::rhi::ShaderStage::kFragment,
                .offset = 0,
                .size = sizeof(cd::post::composite::Push) }
        };
        md.push_constants = kPush;
        constexpr std::array<cd::rhi::DescriptorSetLayoutBinding, 6> kBindings {
            cd::rhi::DescriptorSetLayoutBinding { .binding = 0, .type = cd::rhi::DescriptorType::kCombinedImageSampler, .count = 1, .stages = cd::rhi::ShaderStage::kFragment },
            cd::rhi::DescriptorSetLayoutBinding { .binding = 1, .type = cd::rhi::DescriptorType::kCombinedImageSampler, .count = 1, .stages = cd::rhi::ShaderStage::kFragment },
            cd::rhi::DescriptorSetLayoutBinding { .binding = 2, .type = cd::rhi::DescriptorType::kCombinedImageSampler, .count = 1, .stages = cd::rhi::ShaderStage::kFragment },
            cd::rhi::DescriptorSetLayoutBinding { .binding = 3, .type = cd::rhi::DescriptorType::kCombinedImageSampler, .count = 1, .stages = cd::rhi::ShaderStage::kFragment },
            cd::rhi::DescriptorSetLayoutBinding { .binding = 4, .type = cd::rhi::DescriptorType::kCombinedImageSampler, .count = 1, .stages = cd::rhi::ShaderStage::kFragment },
            cd::rhi::DescriptorSetLayoutBinding { .binding = 5, .type = cd::rhi::DescriptorType::kCombinedImageSampler, .count = 1, .stages = cd::rhi::ShaderStage::kFragment }
        };
        md.descriptor_bindings = kBindings;
        md.raster.cull = cd::rhi::CullMode::kNone;
        md.depth_stencil.depth_test = false;
        md.depth_stencil.depth_write = false;
        md.name = "hello_engine/composite";
        auto r = cd::material::Material::create(device, compiler, md);
        if (!r.has_value())
            return std::unexpected(MaterialSpawnError { 32, "composite" });
        out.composite = std::move(*r);
    }

    // ---- Bloom chain --------------------------------------------------------
    {
        constexpr std::array<cd::rhi::Format, 1> kHdrFmts { cd::rhi::Format::kRGBA16Float };
        constexpr std::array<cd::rhi::DescriptorSetLayoutBinding, 1> kBindings {
            cd::rhi::DescriptorSetLayoutBinding { .binding = 0, .type = cd::rhi::DescriptorType::kCombinedImageSampler, .count = 1, .stages = cd::rhi::ShaderStage::kFragment }
        };
        constexpr std::array<cd::rhi::PushConstantRange, 1> kPrefilterPush {
            cd::rhi::PushConstantRange { .stages = cd::rhi::ShaderStage::kFragment, .offset = 0, .size = sizeof(cd::post::bloom::PrefilterPush) }
        };
        constexpr std::array<cd::rhi::PushConstantRange, 1> kUpsamplePush {
            cd::rhi::PushConstantRange { .stages = cd::rhi::ShaderStage::kFragment, .offset = 0, .size = sizeof(cd::post::bloom::UpsamplePush) }
        };

        {
            cd::material::MaterialDesc md {};
            md.vertex_glsl = cd::post::composite::kCompositeVS;
            md.fragment_glsl = std::string_view { cd::post::bloom::kPrefilterFS };
            md.color_attachment_formats = kHdrFmts;
            md.push_constants = kPrefilterPush;
            md.descriptor_bindings = kBindings;
            md.raster.cull = cd::rhi::CullMode::kNone;
            md.depth_stencil.depth_test = false;
            md.depth_stencil.depth_write = false;
            md.name = "hello_engine/bloom/prefilter";
            auto r = cd::material::Material::create(device, compiler, md);
            if (!r.has_value())
                return std::unexpected(MaterialSpawnError { 40, "bloom/prefilter" });
            out.bloom_prefilter = std::move(*r);
        }

        {
            cd::material::MaterialDesc md {};
            md.vertex_glsl = cd::post::composite::kCompositeVS;
            md.fragment_glsl = std::string_view { cd::post::bloom::kDownsampleFS };
            md.color_attachment_formats = kHdrFmts;
            md.descriptor_bindings = kBindings;
            md.raster.cull = cd::rhi::CullMode::kNone;
            md.depth_stencil.depth_test = false;
            md.depth_stencil.depth_write = false;
            md.name = "hello_engine/bloom/downsample";
            auto r = cd::material::Material::create(device, compiler, md);
            if (!r.has_value())
                return std::unexpected(MaterialSpawnError { 41, "bloom/downsample" });
            out.bloom_downsample = std::move(*r);
        }

        {
            cd::material::MaterialDesc md {};
            md.vertex_glsl = cd::post::composite::kCompositeVS;
            md.fragment_glsl = std::string_view { cd::post::bloom::kUpsampleFS };
            md.color_attachment_formats = kHdrFmts;
            md.push_constants = kUpsamplePush;
            md.descriptor_bindings = kBindings;
            md.raster.cull = cd::rhi::CullMode::kNone;
            constexpr std::array<cd::rhi::BlendAttachmentState, 1> kBlend {
                cd::rhi::BlendAttachmentState { .blend_enable = true,
                                                .src_color    = cd::rhi::BlendFactor::kOne,
                                                .dst_color    = cd::rhi::BlendFactor::kOne,
                                                .color_op     = cd::rhi::BlendOp::kAdd,
                                                .src_alpha    = cd::rhi::BlendFactor::kOne,
                                                .dst_alpha    = cd::rhi::BlendFactor::kOne,
                                                .alpha_op     = cd::rhi::BlendOp::kAdd }
            };
            md.blend_attachments = kBlend;
            md.depth_stencil.depth_test = false;
            md.depth_stencil.depth_write = false;
            md.name = "hello_engine/bloom/upsample";
            auto r = cd::material::Material::create(device, compiler, md);
            if (!r.has_value())
                return std::unexpected(MaterialSpawnError { 42, "bloom/upsample" });
            out.bloom_upsample = std::move(*r);
        }
    }

    // ---- Prim pipeline ------------------------------------------------------
    if (!device.features().ray_query)
    {
        std::fprintf(
            stderr,
            "hello_engine: device lacks VK_KHR_ray_query; "
            "Faz 1.7 inline RT shadows require it. "
            "Re-run on RT-capable hardware or git-checkout f04b588 "
            "(pre-1.7 CSM-only ship).\n"
        );
        return std::unexpected(MaterialSpawnError { 9, "prim (ray_query gate)" });
    }
    constexpr std::array<cd::rhi::VertexBinding, 1> kPrimBindings {
        cd::rhi::VertexBinding { 0, sizeof(cd::asset::PrimitiveVertex), false }
    };
    constexpr std::array<cd::rhi::VertexAttribute, 4> kPrimAttrs {
        cd::rhi::VertexAttribute { 0, 0, cd::rhi::Format::kRGB32Float, offsetof(cd::asset::PrimitiveVertex, pos)    },
        cd::rhi::VertexAttribute { 1, 0, cd::rhi::Format::kRGB32Float, offsetof(cd::asset::PrimitiveVertex, normal) },
        cd::rhi::VertexAttribute { 2, 0, cd::rhi::Format::kRG32Float,  offsetof(cd::asset::PrimitiveVertex, uv)     },
        cd::rhi::VertexAttribute { 3, 0, cd::rhi::Format::kRGB32Float, offsetof(cd::asset::PrimitiveVertex, color)  }
    };
    // Single source of truth for the prim MaterialDesc lives in
    // prim_recreate() above so the X5/M1 hot-reload path (invoked from
    // HelloShaderWatch) rebuilds it the same way.
    if (!prim_recreate(device, compiler, &out.prim))
    {
        return std::unexpected(MaterialSpawnError { 9, "prim" });
    }

    // ---- Velocity-pass material --------------------------------------------
    {
        constexpr std::array<cd::rhi::Format, 1> kVelColorFmts { cd::rhi::Format::kRG16Float };
        constexpr std::array<cd::rhi::PushConstantRange, 1> kPush {
            cd::rhi::PushConstantRange { .stages = cd::rhi::ShaderStage::kVertex, .offset = 0, .size = 128U }
        };
        cd::material::MaterialDesc md {};
        md.vertex_glsl = std::string_view { cd::velocity::kVelocityVS };
        md.fragment_glsl = std::string_view { cd::velocity::kVelocityFS };
        md.color_attachment_formats = kVelColorFmts;
        md.depth_attachment_format = cd::rhi::Format::kD32Float;
        md.vertex_bindings = kPrimBindings;
        md.vertex_attributes = kPrimAttrs;
        md.push_constants = kPush;
        md.raster.cull = cd::rhi::CullMode::kNone;
        md.depth_stencil.depth_test = true;
        md.depth_stencil.depth_write = false;
        md.depth_stencil.depth_compare = cd::rhi::CompareOp::kLessEqual;
        md.name = "hello_engine/velocity";
        auto r = cd::material::Material::create(device, compiler, md);
        if (!r.has_value())
        {
            std::fprintf(
                stderr,
                "hello_engine: velocity_material create failed: %.*s\n",
                static_cast<int>(r.error().message.size()),
                r.error().message.data()
            );
            return std::unexpected(MaterialSpawnError { 52, "velocity" });
        }
        out.velocity = std::move(*r);
    }

    // ---- Shadow material (Faz 1.6 CSM, depth-only) -------------------------
    // D-F6: single source of truth lives in shadow_recreate() so the
    // hot-reload path rebuilds the pipeline identically.
    if (!shadow_recreate(device, compiler, &out.shadow))
        return std::unexpected(MaterialSpawnError { 10, "shadow" });

    return out;
}

} // namespace cd_sample
