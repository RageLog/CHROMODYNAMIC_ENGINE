// =============================================================================
// CHROMODYNAMIC -- samples/engine/hello_engine/tests/test_hello_materials_prim_bindless.cpp
//
// Regression net for the prim hot-reload bindless-layout drift (phase864
// class of silent bindless failure). The shader-watch reload closure in
// main.cpp (add_entry "prim") once called
//
//     cd_sample::prim_recreate(d, c, &prim)          // 4th arg DEFAULTED {}
//
// while boot (spawn_materials, HelloMaterials.hpp) passes
// out.prim_bindless_layout -- so the FIRST prim shader hot-reload rebuilt
// the pipeline WITHOUT its dedicated set=1 bindless sampler-array layout
// and the bindless texture path broke silently (no validation error, no
// crash -- reflections just lose their textures). The fixed call-site now
// forwards state_->materials.prim_bindless_layout explicitly.
//
// The closure itself needs a live App, so this test pins the contract one
// level down, at prim_recreate():
//
//   (1) prim_recreate(dev, compiler, &m, valid_layout) succeeds AND the
//       pipeline layout built for the pipeline carries EXACTLY 2 set
//       layouts: the material's own set 0 + the bindless set 1 (the extra
//       layout handle is literally present, at the last slot).
//   (2) prim_recreate(dev, compiler, &m2) with the DEFAULTED 4th arg
//       succeeds AND the pipeline layout carries EXACTLY 1 set layout
//       (own set only, 0 extra) -- pinning that omitting the arg DROPS
//       the bindless set. That is precisely why every reload closure must
//       pass the layout, not just boot.
//   (3) make_prim_bindless_layout(NullDevice) yields a valid handle
//       (guards the helper the boot + reload paths both depend on).
//
// Observation point: cd::material::Material::create assembles the set-
// layout list (own set + MaterialDesc::extra_set_layouts) into
// cd::rhi::PipelineLayoutDesc::set_layouts and calls
// IDevice::create_pipeline_layout (Material.cpp phase864-multi-set block).
// The RHI GraphicsPipelineDesc only carries the resulting opaque
// PipelineLayoutHandle, so the capture MUST happen at
// create_pipeline_layout -- a forwarding Null-device wrapper (the
// LoggingNullDevice pattern from
// engine/render/material/tests/test_material_recreate.cpp) snapshots
// every PipelineLayoutDesc::set_layouts span it sees.
//
// Anti-flakiness (CLAUDE.md 5): no GPU, no glslang, no file I/O, no
// sleeps. Determinism is pure call-capture on the Null backend.
// =============================================================================

// Force the embedded-GLSL fallback path (ADR-20260529-X5 precedence:
// *_glsl_path is only set when this macro is truthy). The define is a
// PRIVATE compile definition of the cd_sample_hello_engine target, so a
// test TU must pick a value itself; 0 keeps prim_recreate off the
// cwd-relative "shaders/prim.vert.glsl" reads -> fully deterministic.
#define HELLO_ENGINE_USE_ON_DISK_SHADERS 0

#include "../HelloMaterials.hpp"

#include <cd/material/Material.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/NullDevice.hpp>
#include <cd/shader/Compiler.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace
{

// Minimal SPIR-V-shaped blob -- NullDevice::create_shader_module accepts any
// non-empty 32-bit-aligned payload; no real driver is ever reached.
constexpr std::array<std::uint32_t, 5> kStubSpirv { 0x07230203U, 0x00010000U, 0x00000000U, 1U, 0U };

// ---- Stub ICompiler --------------------------------------------------------
// prim_recreate always feeds GLSL (embedded kPrimVS / kPrimFS literals), and
// Material::create rejects GLSL with a null compiler (kCompilerRequired), so
// the Null-backend path needs a stub that "compiles" everything to the fixed
// blob above. Mirrors the StubCompiler in test_material_recreate.cpp.
class StubCompiler final : public cd::shader::ICompiler
{
public:
    [[nodiscard]] cd::core::Result<cd::shader::CompileResult>
    compile(const cd::shader::CompileDesc& desc) override
    {
        static_cast<void>(desc);  // source text irrelevant here
        cd::shader::CompileResult r {};
        r.spirv.assign(kStubSpirv.begin(), kStubSpirv.end());
        return r;
    }
};

// ---- Pipeline-layout capturing Null device ---------------------------------
// cd::rhi::NullDevice is `final`, so this is a forwarding IDevice that owns a
// NullDevice and delegates every call to it (the LoggingNullDevice pattern
// from engine/render/material/tests/test_material_recreate.cpp), intercepting
// ONLY create_pipeline_layout to snapshot the set-layout list each pipeline
// layout is built over. No behaviour is faked -- handle allocation and every
// create_*/destroy_* run through the real NullDevice.
class LayoutCapturingNullDevice final : public cd::rhi::IDevice
{
public:
    /// One entry per create_pipeline_layout call: the full set-layout list
    /// (own material set first, extra_set_layouts appended after).
    [[nodiscard]] const std::vector<std::vector<cd::rhi::DescriptorSetLayoutHandle>>&
    pipeline_layout_captures() const noexcept { return captures_; }
    void clear_captures() noexcept { captures_.clear(); }

    // ---- intercepted (the capture point under test) ----
    [[nodiscard]] cd::core::Result<cd::rhi::PipelineLayoutHandle>
    create_pipeline_layout(const cd::rhi::PipelineLayoutDesc& d) override
    {
        captures_.emplace_back(d.set_layouts.begin(), d.set_layouts.end());
        return inner_.create_pipeline_layout(d);
    }

    // ---- pure forwards (introspection) ----
    [[nodiscard]] cd::rhi::Backend backend() const noexcept override { return inner_.backend(); }
    [[nodiscard]] std::string_view adapter_name() const noexcept override { return inner_.adapter_name(); }
    [[nodiscard]] const cd::rhi::DeviceLimits& limits() const noexcept override { return inner_.limits(); }
    [[nodiscard]] const cd::rhi::DeviceFeatures& features() const noexcept override { return inner_.features(); }
    void wait_idle() override { inner_.wait_idle(); }

    // ---- pure forwards (resources) ----
    [[nodiscard]] cd::core::Result<cd::rhi::BufferHandle> create_buffer(const cd::rhi::BufferDesc& d) override { return inner_.create_buffer(d); }
    void destroy_buffer(cd::rhi::BufferHandle h) override { inner_.destroy_buffer(h); }
    [[nodiscard]] cd::core::Result<cd::rhi::TextureHandle> create_texture(const cd::rhi::TextureDesc& d) override { return inner_.create_texture(d); }
    void destroy_texture(cd::rhi::TextureHandle h) override { inner_.destroy_texture(h); }
    [[nodiscard]] cd::core::Result<cd::rhi::TextureViewHandle> create_texture_view(const cd::rhi::TextureViewDesc& d) override { return inner_.create_texture_view(d); }
    void destroy_texture_view(cd::rhi::TextureViewHandle h) override { inner_.destroy_texture_view(h); }
    [[nodiscard]] cd::core::Result<cd::rhi::SamplerHandle> create_sampler(const cd::rhi::SamplerDesc& d) override { return inner_.create_sampler(d); }
    void destroy_sampler(cd::rhi::SamplerHandle h) override { inner_.destroy_sampler(h); }
    [[nodiscard]] cd::core::Result<cd::rhi::ShaderModuleHandle> create_shader_module(const cd::rhi::ShaderModuleDesc& d) override { return inner_.create_shader_module(d); }
    void destroy_shader_module(cd::rhi::ShaderModuleHandle h) override { inner_.destroy_shader_module(h); }
    [[nodiscard]] cd::core::Result<cd::rhi::DescriptorSetLayoutHandle> create_descriptor_set_layout(const cd::rhi::DescriptorSetLayoutDesc& d) override { return inner_.create_descriptor_set_layout(d); }
    void destroy_descriptor_set_layout(cd::rhi::DescriptorSetLayoutHandle h) override { inner_.destroy_descriptor_set_layout(h); }
    void destroy_pipeline_layout(cd::rhi::PipelineLayoutHandle h) override { inner_.destroy_pipeline_layout(h); }
    [[nodiscard]] cd::core::Result<cd::rhi::GraphicsPipelineHandle> create_graphics_pipeline(const cd::rhi::GraphicsPipelineDesc& d) override { return inner_.create_graphics_pipeline(d); }
    void destroy_graphics_pipeline(cd::rhi::GraphicsPipelineHandle h) override { inner_.destroy_graphics_pipeline(h); }
    [[nodiscard]] cd::core::Result<cd::rhi::ComputePipelineHandle> create_compute_pipeline(const cd::rhi::ComputePipelineDesc& d) override { return inner_.create_compute_pipeline(d); }
    void destroy_compute_pipeline(cd::rhi::ComputePipelineHandle h) override { inner_.destroy_compute_pipeline(h); }

    // ---- pure forwards (descriptor sets) ----
    [[nodiscard]] cd::core::Result<cd::rhi::DescriptorSetHandle> allocate_descriptor_set(cd::rhi::DescriptorSetLayoutHandle l) override { return inner_.allocate_descriptor_set(l); }
    void destroy_descriptor_set(cd::rhi::DescriptorSetHandle h) override { inner_.destroy_descriptor_set(h); }
    [[nodiscard]] cd::core::Result<void> update_descriptor_set(cd::rhi::DescriptorSetHandle s, std::span<const cd::rhi::DescriptorWrite> w) override { return inner_.update_descriptor_set(s, w); }

    // ---- pure forwards (sync) ----
    [[nodiscard]] cd::core::Result<cd::rhi::SemaphoreHandle> create_semaphore() override { return inner_.create_semaphore(); }
    void destroy_semaphore(cd::rhi::SemaphoreHandle h) override { inner_.destroy_semaphore(h); }
    [[nodiscard]] cd::core::Result<cd::rhi::FenceHandle> create_fence(bool s) override { return inner_.create_fence(s); }
    void destroy_fence(cd::rhi::FenceHandle h) override { inner_.destroy_fence(h); }
    [[nodiscard]] cd::core::Result<void> wait_for_fence(cd::rhi::FenceHandle f, std::uint64_t t) override { return inner_.wait_for_fence(f, t); }
    void reset_fence(cd::rhi::FenceHandle f) override { inner_.reset_fence(f); }
    [[nodiscard]] bool is_fence_signaled(cd::rhi::FenceHandle f) override { return inner_.is_fence_signaled(f); }
    [[nodiscard]] cd::core::Result<cd::rhi::TimelineSemaphoreHandle> create_timeline_semaphore(std::uint64_t v) override { return inner_.create_timeline_semaphore(v); }
    void destroy_timeline_semaphore(cd::rhi::TimelineSemaphoreHandle h) override { inner_.destroy_timeline_semaphore(h); }
    [[nodiscard]] cd::core::Result<void> wait_timeline_semaphore(cd::rhi::TimelineSemaphoreHandle h, std::uint64_t v, std::uint64_t t) override { return inner_.wait_timeline_semaphore(h, v, t); }
    [[nodiscard]] cd::core::Result<void> signal_timeline_semaphore(cd::rhi::TimelineSemaphoreHandle h, std::uint64_t v) override { return inner_.signal_timeline_semaphore(h, v); }
    [[nodiscard]] std::uint64_t timeline_semaphore_value(cd::rhi::TimelineSemaphoreHandle h) const override { return inner_.timeline_semaphore_value(h); }

    // ---- pure forwards (swapchain) ----
    [[nodiscard]] cd::core::Result<std::uint32_t> acquire_next_image(cd::rhi::SwapchainHandle s, cd::rhi::SemaphoreHandle sig, cd::rhi::FenceHandle f, std::uint64_t t) override { return inner_.acquire_next_image(s, sig, f, t); }
    [[nodiscard]] cd::core::Result<void> present(cd::rhi::SwapchainHandle s, std::uint32_t i, std::span<const cd::rhi::SemaphoreHandle> w) override { return inner_.present(s, i, w); }
    [[nodiscard]] cd::rhi::TextureViewHandle swapchain_image_view(cd::rhi::SwapchainHandle s, std::uint32_t i) const override { return inner_.swapchain_image_view(s, i); }
    [[nodiscard]] std::uint32_t swapchain_image_count(cd::rhi::SwapchainHandle s) const override { return inner_.swapchain_image_count(s); }
    [[nodiscard]] cd::rhi::TextureHandle swapchain_image(cd::rhi::SwapchainHandle s, std::uint32_t i) const override { return inner_.swapchain_image(s, i); }
    [[nodiscard]] cd::core::Result<cd::rhi::SwapchainHandle> create_swapchain(const cd::rhi::SwapchainDesc& d) override { return inner_.create_swapchain(d); }
    void destroy_swapchain(cd::rhi::SwapchainHandle h) override { inner_.destroy_swapchain(h); }

    // ---- pure forwards (staging) ----
    [[nodiscard]] cd::core::Result<void> upload_buffer(cd::rhi::BufferHandle h, std::uint64_t o, std::span<const std::byte> d) override { return inner_.upload_buffer(h, o, d); }
    [[nodiscard]] cd::core::Result<void> download_buffer(cd::rhi::BufferHandle h, std::uint64_t o, std::span<std::byte> d) override { return inner_.download_buffer(h, o, d); }

    // ---- pure forwards (command buffers) ----
    [[nodiscard]] std::unique_ptr<cd::rhi::ICommandBuffer> do_create_command_buffer(cd::rhi::QueueType q) override { return inner_.create_command_buffer(q); }
    void submit(cd::rhi::ICommandBuffer& c) override { inner_.submit(c); }
    [[nodiscard]] cd::core::Result<void> submit(const cd::rhi::SubmitDesc& d) override { return inner_.submit(d); }

private:
    cd::rhi::NullDevice inner_;
    std::vector<std::vector<cd::rhi::DescriptorSetLayoutHandle>> captures_;
};

}  // namespace

// ---------------------------------------------------------------------------
// (1) With the layout passed (the boot path AND the FIXED reload closure in
//     main.cpp add_entry "prim"): the prim pipeline layout carries exactly
//     2 set layouts -- own set 0 + the bindless set 1, and set 1 IS the
//     handle make_prim_bindless_layout produced.
// ---------------------------------------------------------------------------
TEST(HelloMaterialsPrimBindless, RecreateWithLayoutCarriesDedicatedBindlessSet)
{
    // Arrange
    LayoutCapturingNullDevice dev;
    StubCompiler compiler;
    const cd::rhi::DescriptorSetLayoutHandle bindless =
        cd_sample::make_prim_bindless_layout(dev);
    ASSERT_TRUE(bindless.is_valid());
    dev.clear_captures();  // only watch the prim_recreate call itself
    cd::material::Material prim {};

    // Act
    const bool ok = cd_sample::prim_recreate(dev, &compiler, &prim, bindless);

    // Assert
    ASSERT_TRUE(ok);
    EXPECT_TRUE(prim.is_valid());
    const auto& caps = dev.pipeline_layout_captures();
    ASSERT_EQ(caps.size(), 1U) << "prim_recreate must build exactly one pipeline layout";
    ASSERT_EQ(caps.front().size(), 2U)
        << "prim pipeline layout must carry own set 0 + dedicated bindless set 1 "
           "(phase864-bindless-dedicated-set)";
    EXPECT_TRUE(caps.front().front().is_valid());  // material's own set 0
    EXPECT_EQ(caps.front().back(), bindless)
        << "set index 1 must be EXACTLY the make_prim_bindless_layout handle";
    EXPECT_NE(caps.front().front(), bindless)
        << "own set 0 and the bindless set must be distinct layouts";
}

// ---------------------------------------------------------------------------
// (2) With the 4th arg DEFAULTED ({}): the pipeline layout carries only the
//     material's own set -- 0 extra layouts. This is the exact silent drop
//     the pre-fix reload closure shipped: `prim_recreate(d, c, &prim)` in
//     main.cpp's shader-watch add_entry "prim" rebuilt the prim pipeline
//     WITHOUT set=1 on the first hot-reload, and the bindless sampler-array
//     path died with no error. The success + count==1 pair documents WHY
//     every call-site must forward materials.prim_bindless_layout.
// ---------------------------------------------------------------------------
TEST(HelloMaterialsPrimBindless, RecreateWithDefaultedArgDropsBindlessSet)
{
    // Arrange
    LayoutCapturingNullDevice dev;
    StubCompiler compiler;
    cd::material::Material prim {};

    // Act -- NOTE: deliberately no 4th argument (the buggy closure's shape).
    const bool ok = cd_sample::prim_recreate(dev, &compiler, &prim);

    // Assert -- it SUCCEEDS (that is what makes the bug silent) but the
    // pipeline layout has zero extra set layouts: no set 1, no bindless.
    ASSERT_TRUE(ok);
    EXPECT_TRUE(prim.is_valid());
    const auto& caps = dev.pipeline_layout_captures();
    ASSERT_EQ(caps.size(), 1U) << "prim_recreate must build exactly one pipeline layout";
    EXPECT_EQ(caps.front().size(), 1U)
        << "defaulted bindless_set_layout must yield own set 0 ONLY (0 extra "
           "set layouts) -- the silent phase864-class drop this test pins";
    EXPECT_TRUE(caps.front().front().is_valid());  // own set 0 still present
}

// ---------------------------------------------------------------------------
// (3) Negative/helper guard: make_prim_bindless_layout on the Null backend
//     yields a valid handle (boot exits with code 9 when it does not), and
//     a default-constructed handle -- the defaulted 4th arg -- is invalid,
//     which is the predicate prim_recreate branches on.
// ---------------------------------------------------------------------------
TEST(HelloMaterialsPrimBindless, MakePrimBindlessLayoutYieldsValidHandle)
{
    // Arrange
    LayoutCapturingNullDevice dev;

    // Act
    const cd::rhi::DescriptorSetLayoutHandle layout =
        cd_sample::make_prim_bindless_layout(dev);

    // Assert
    EXPECT_TRUE(layout.is_valid());
    EXPECT_FALSE(cd::rhi::DescriptorSetLayoutHandle {}.is_valid())
        << "the defaulted arg must read as invalid, or prim_recreate would "
           "append a null set layout instead of skipping it";
}
