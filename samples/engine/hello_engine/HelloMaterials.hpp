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
#include <cd/post_bloom/Bloom.hpp>
#include <cd/post_composite/Composite.hpp>
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
        md.vertex_glsl = cd::post_composite::kCompositeVS;
        md.fragment_glsl = cd::post_composite::kCompositeFS;
        md.color_attachment_formats = kCompFmts;
        constexpr std::array<cd::rhi::PushConstantRange, 1> kPush {
            cd::rhi::PushConstantRange {
                .stages = cd::rhi::ShaderStage::kFragment,
                .offset = 0,
                .size = sizeof(cd::post_composite::Push) }
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
            cd::rhi::PushConstantRange { .stages = cd::rhi::ShaderStage::kFragment, .offset = 0, .size = sizeof(cd::post_bloom::PrefilterPush) }
        };
        constexpr std::array<cd::rhi::PushConstantRange, 1> kUpsamplePush {
            cd::rhi::PushConstantRange { .stages = cd::rhi::ShaderStage::kFragment, .offset = 0, .size = sizeof(cd::post_bloom::UpsamplePush) }
        };

        {
            cd::material::MaterialDesc md {};
            md.vertex_glsl = cd::post_composite::kCompositeVS;
            md.fragment_glsl = std::string_view { cd::post_bloom::kPrefilterFS };
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
            md.vertex_glsl = cd::post_composite::kCompositeVS;
            md.fragment_glsl = std::string_view { cd::post_bloom::kDownsampleFS };
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
            md.vertex_glsl = cd::post_composite::kCompositeVS;
            md.fragment_glsl = std::string_view { cd::post_bloom::kUpsampleFS };
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
    {
        constexpr std::array<cd::rhi::PushConstantRange, 1> kPush {
            cd::rhi::PushConstantRange {
                .stages = cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                .offset = 0,
                .size   = static_cast<std::uint32_t>(sizeof(cd::hello_engine::PrimPush)) }
        };
        constexpr std::array<cd::rhi::DescriptorSetLayoutBinding, 11> kBindings {
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
            cd::rhi::DescriptorSetLayoutBinding { .binding = 10, .type = cd::rhi::DescriptorType::kStorageBuffer,         .count = 1, .stages = cd::rhi::ShaderStage::kFragment }
        };
        cd::material::MaterialDesc md {};
        md.vertex_glsl = cd::hello_engine::kPrimVS;
        md.fragment_glsl = cd::hello_engine::kPrimFS;
        md.color_attachment_formats = kColorFmts;
        md.depth_attachment_format = cd::rhi::Format::kD32Float;
        md.vertex_bindings = kPrimBindings;
        md.vertex_attributes = kPrimAttrs;
        md.push_constants = kPush;
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
            return std::unexpected(MaterialSpawnError { 9, "prim" });
        }
        out.prim = std::move(*r);
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
    {
        constexpr std::array<cd::rhi::PushConstantRange, 1> kPush {
            cd::rhi::PushConstantRange { .stages = cd::rhi::ShaderStage::kVertex,
                                         .offset = 0,
                                         .size   = static_cast<std::uint32_t>(sizeof(cd::math::Mat4f)) }
        };
        cd::material::MaterialDesc md {};
        md.vertex_glsl = cd::hello_engine::kShadowVS;
        md.fragment_glsl = cd::hello_engine::kShadowFS;
        md.color_attachment_formats = {};
        md.depth_attachment_format = cd::rhi::Format::kD32Float;
        md.vertex_bindings = kPrimBindings;
        md.vertex_attributes = kPrimAttrs;
        md.push_constants = kPush;
        md.raster.cull = cd::rhi::CullMode::kBack;
        md.raster.depth_bias_enable = true;
        md.raster.depth_bias_constant = 1.25F;
        md.raster.depth_bias_slope = 1.75F;
        md.depth_stencil.depth_test = true;
        md.depth_stencil.depth_write = true;
        md.depth_stencil.depth_compare = cd::rhi::CompareOp::kLess;
        md.name = "hello_engine/shadow";
        auto r = cd::material::Material::create(device, compiler, md);
        if (!r.has_value())
            return std::unexpected(MaterialSpawnError { 10, "shadow" });
        out.shadow = std::move(*r);
    }

    return out;
}

} // namespace cd_sample
