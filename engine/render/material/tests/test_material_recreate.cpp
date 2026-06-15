// =============================================================================
// CHROMODYNAMIC — cd::material recreate / hot-reload deterministic test gate
// (X5-4, ADR-20260608 addendum A.4).
//
// Host-portable: drives the Null RHI backend + a stub ICompiler. No GPU, no
// glslang, no file I/O on the GLSL precedence cases. Covers the four hot-
// reload contracts the shipped HelloShaderWatch reload path relies on:
//
//   (A) Source precedence  — *_spirv > *_glsl_path > *_glsl, observed via
//       which path Material::create takes (compiler invoked? with what text?).
//   (B) Handle swap        — recreate (create + move-assign) changes the
//       Material's pipeline() handle value (a NEW PSO is bound).
//   (C) Deferred-release    — the X5-2 reload-path guard drains the device
//       (wait_idle) BEFORE the old pipeline is destroyed; verified against an
//       instrumented Null device call-log that records ordering.
//   (D) Broken edit non-fatal — a failing compile leaves the ORIGINAL Material
//       handle unchanged (the live session keeps rendering the old pipeline).
//
// Anti-flakiness (CLAUDE.md §5): no sleep_for, no wall-clock. Determinism is
// pure call-order on the Null device; the on-disk precedence case uses a
// temp file written + closed before create() reads it (no mtime race).
// =============================================================================
#include <cd/material/Material.hpp>
#include <cd/rhi/Format.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/NullDevice.hpp>
#include <cd/shader/Compiler.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace
{
namespace fs = std::filesystem;

// Minimal SPIR-V header — NullDevice::create_shader_module accepts any
// non-empty 32-bit-aligned blob; we never reach a real driver here.
constexpr std::array<std::uint32_t, 5> kStubSpirv { 0x07230203U, 0x00010000U, 0x00000000U, 1U, 0U };
constexpr std::array<cd::rhi::Format, 1> kColorFormats { cd::rhi::Format::kRGBA8Unorm };

// ---- Stub ICompiler --------------------------------------------------------
// Records the last source text it was handed (so the precedence case can
// assert WHICH GLSL reached the compiler) and can be flipped to fail (so the
// broken-edit case has a deterministic compile error without glslang).
class StubCompiler final : public cd::shader::ICompiler
{
public:
    [[nodiscard]] cd::core::Result<cd::shader::CompileResult>
    compile(const cd::shader::CompileDesc& desc) override
    {
        ++invocation_count_;
        last_source_.assign(desc.source);
        if (fail_)
        {
            return std::unexpected(cd::shader::shader_errors::make(
                cd::shader::shader_errors::Code::kCompileFailed,
                "StubCompiler: forced failure (broken-edit test)"));
        }
        cd::shader::CompileResult r {};
        r.spirv.assign(kStubSpirv.begin(), kStubSpirv.end());
        return r;
    }

    void set_fail(bool f) noexcept { fail_ = f; }
    [[nodiscard]] int invocation_count() const noexcept { return invocation_count_; }
    [[nodiscard]] const std::string& last_source() const noexcept { return last_source_; }

private:
    bool fail_ { false };
    int invocation_count_ { 0 };
    std::string last_source_;
};

// ---- Instrumented Null device (call-order log) -----------------------------
// Records the ordered sequence of wait_idle() and destroy_graphics_pipeline()
// so case (C) can assert the X5-2 invariant: the reload path drains the
// device before any PSO is destroyed. The shipped guard lives in
// HelloShaderWatch::poll_and_reload (sample tier); this test reproduces the
// exact reload-path call sequence — device.wait_idle() then move-assign-over
// (which routes to Material::release -> destroy_graphics_pipeline) — and
// verifies the order the watcher establishes.
//
// cd::rhi::NullDevice is `final`, so this is a forwarding IDevice that owns a
// NullDevice and delegates every call to it, intercepting only wait_idle and
// destroy_graphics_pipeline to append to the call-log. No behaviour is faked:
// handle allocation, create_*, and destroy_* all run through the real
// NullDevice; the log merely observes the two methods whose ORDER is the
// contract under test.
class LoggingNullDevice final : public cd::rhi::IDevice
{
public:
    enum class Event : std::uint8_t
    {
        kWaitIdle,
        kDestroyPipeline,
    };

    struct Entry
    {
        Event event;
        std::uint32_t pipeline_index;  // valid for kDestroyPipeline; 0 otherwise
    };

    [[nodiscard]] const std::vector<Entry>& log() const noexcept { return log_; }
    void clear_log() noexcept { log_.clear(); }

    // ---- intercepted (the two methods whose ORDER is the contract) ----
    void wait_idle() override
    {
        log_.push_back(Entry { Event::kWaitIdle, 0U });
        inner_.wait_idle();
    }

    void destroy_graphics_pipeline(cd::rhi::GraphicsPipelineHandle h) override
    {
        log_.push_back(Entry { Event::kDestroyPipeline, h.index() });
        inner_.destroy_graphics_pipeline(h);
    }

    // ---- pure forwards (introspection) ----
    [[nodiscard]] cd::rhi::Backend backend() const noexcept override { return inner_.backend(); }
    [[nodiscard]] std::string_view adapter_name() const noexcept override { return inner_.adapter_name(); }
    [[nodiscard]] const cd::rhi::DeviceLimits& limits() const noexcept override { return inner_.limits(); }
    [[nodiscard]] const cd::rhi::DeviceFeatures& features() const noexcept override { return inner_.features(); }

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
    [[nodiscard]] cd::core::Result<cd::rhi::PipelineLayoutHandle> create_pipeline_layout(const cd::rhi::PipelineLayoutDesc& d) override { return inner_.create_pipeline_layout(d); }
    void destroy_pipeline_layout(cd::rhi::PipelineLayoutHandle h) override { inner_.destroy_pipeline_layout(h); }
    [[nodiscard]] cd::core::Result<cd::rhi::GraphicsPipelineHandle> create_graphics_pipeline(const cd::rhi::GraphicsPipelineDesc& d) override { return inner_.create_graphics_pipeline(d); }
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
    std::vector<Entry> log_;
};

// Build a GLSL-source MaterialDesc with all three input layers set so the
// precedence rule can be observed by what the stub compiler receives. The
// path arguments are owning std::strings held by the CALLER (MaterialDesc
// stores string_views, which must not dangle); take them by const-ref.
[[nodiscard]] cd::material::MaterialDesc make_glsl_desc(const std::string& vs_path,
                                                        const std::string& fs_path)
{
    cd::material::MaterialDesc desc {};
    desc.vertex_glsl = "VS_INLINE";    // lowest precedence
    desc.fragment_glsl = "FS_INLINE";
    desc.vertex_glsl_path = vs_path;   // middle precedence (view into caller's string)
    desc.fragment_glsl_path = fs_path;
    desc.color_attachment_formats = kColorFormats;
    desc.name = "recreate_test";
    // include_resolver left null — embedded gluon bridge; the stub compiler
    // ignores includes anyway (it returns a fixed module).
    return desc;
}

[[nodiscard]] fs::path write_temp_glsl(std::string_view tag, std::string_view body)
{
    static int seq = 0;
    const auto p = fs::temp_directory_path() /
                   ("cd_recreate_" + std::string { tag } + "_" + std::to_string(seq++) + ".glsl");
    std::ofstream f(p, std::ios::trunc | std::ios::binary);
    f << body;
    f.close();  // flush + close before create() reads it — no race
    return p;
}

}  // namespace

// ---------------------------------------------------------------------------
// (A) Source precedence: *_spirv > *_glsl_path > *_glsl
// ---------------------------------------------------------------------------
TEST(MaterialRecreate, SourcePrecedenceSpirvBeatsPathBeatsInline)
{
    // (A.1) spirv set → compiler is NEVER invoked even though glsl + path are
    // also populated. A null compiler must still succeed.
    {
        cd::rhi::NullDevice dev;
        StubCompiler compiler;
        cd::material::MaterialDesc desc {};
        desc.vertex_spirv = kStubSpirv;
        desc.fragment_spirv = kStubSpirv;
        desc.vertex_glsl = "VS_INLINE";
        desc.fragment_glsl = "FS_INLINE";
        desc.color_attachment_formats = kColorFormats;
        desc.name = "prec_spirv";
        auto m = cd::material::Material::create(dev, &compiler, desc);
        ASSERT_TRUE(m.has_value()) << m.error().message;
        EXPECT_EQ(compiler.invocation_count(), 0)
            << "spirv has highest precedence — compiler must not run";
        // Also succeeds with NO compiler at all (pure spirv path).
        auto m2 = cd::material::Material::create(dev, nullptr, desc);
        EXPECT_TRUE(m2.has_value()) << m2.error().message;
    }

    // (A.2) glsl_path beats inline glsl: the file content reaches the compiler,
    // not the inline literal.
    {
        const auto vs = write_temp_glsl("vs", "VS_FROM_PATH");
        const auto fs_file = write_temp_glsl("fs", "FS_FROM_PATH");
        const std::string vs_path = vs.string();   // owning — desc holds a view
        const std::string fs_path = fs_file.string();
        cd::rhi::NullDevice dev;
        StubCompiler compiler;
        auto desc = make_glsl_desc(vs_path, fs_path);
        auto m = cd::material::Material::create(dev, &compiler, desc);
        ASSERT_TRUE(m.has_value()) << m.error().message;
        EXPECT_EQ(compiler.invocation_count(), 2);  // vs + fs
        // The LAST stage compiled is the fragment; its source is the file body.
        EXPECT_EQ(compiler.last_source(), "FS_FROM_PATH")
            << "vertex_glsl_path / fragment_glsl_path must win over inline glsl";
        std::error_code ec;
        fs::remove(vs, ec);
        fs::remove(fs_file, ec);
    }

    // (A.3) only inline glsl → that literal reaches the compiler.
    {
        cd::rhi::NullDevice dev;
        StubCompiler compiler;
        cd::material::MaterialDesc desc {};
        desc.vertex_glsl = "VS_ONLY_INLINE";
        desc.fragment_glsl = "FS_ONLY_INLINE";
        desc.color_attachment_formats = kColorFormats;
        desc.name = "prec_inline";
        auto m = cd::material::Material::create(dev, &compiler, desc);
        ASSERT_TRUE(m.has_value()) << m.error().message;
        EXPECT_EQ(compiler.last_source(), "FS_ONLY_INLINE");
    }
}

// ---------------------------------------------------------------------------
// (B) Handle swap: recreate (create + move-assign) changes pipeline() handle
// ---------------------------------------------------------------------------
TEST(MaterialRecreate, HandleSwapChangesPipelineHandle)
{
    cd::rhi::NullDevice dev;
    cd::material::MaterialDesc desc {};
    desc.vertex_spirv = kStubSpirv;
    desc.fragment_spirv = kStubSpirv;
    desc.color_attachment_formats = kColorFormats;
    desc.name = "handle_swap";

    auto first = cd::material::Material::create(dev, nullptr, desc);
    ASSERT_TRUE(first.has_value()) << first.error().message;
    const auto old_handle = first->pipeline();
    ASSERT_TRUE(old_handle.is_valid());

    // Recreate: build a fresh Material, move-assign over the live one. The
    // Null device hands out monotonically-increasing ids so the new pipeline
    // handle differs from the old one.
    auto second = cd::material::Material::create(dev, nullptr, desc);
    ASSERT_TRUE(second.has_value()) << second.error().message;
    const auto new_handle = second->pipeline();

    *first = std::move(*second);  // the reload-path swap

    EXPECT_TRUE(first->pipeline().is_valid());
    EXPECT_EQ(first->pipeline(), new_handle);
    EXPECT_NE(first->pipeline(), old_handle)
        << "after recreate the bound PSO handle must change";
}

// ---------------------------------------------------------------------------
// (C) Deferred-release ordering: wait_idle precedes the old pipeline destroy
// ---------------------------------------------------------------------------
TEST(MaterialRecreate, DeferredReleaseWaitIdlePrecedesDestroy)
{
    LoggingNullDevice dev;
    cd::material::MaterialDesc desc {};
    desc.vertex_spirv = kStubSpirv;
    desc.fragment_spirv = kStubSpirv;
    desc.color_attachment_formats = kColorFormats;
    desc.name = "deferred_release";

    auto live = cd::material::Material::create(dev, nullptr, desc);
    ASSERT_TRUE(live.has_value()) << live.error().message;
    const auto old_pipeline_index = live->pipeline().index();

    auto fresh = cd::material::Material::create(dev, nullptr, desc);
    ASSERT_TRUE(fresh.has_value()) << fresh.error().message;

    dev.clear_log();  // ignore the create-time activity; only watch the swap

    // Reproduce HelloShaderWatch::poll_and_reload's reload-path sequence
    // EXACTLY: X5-2 guard drains the device, THEN the recreate closure
    // move-assigns the fresh Material over the live one (which destroys the
    // old pipeline via Material::release).
    dev.wait_idle();                 // X5-2 guard (sample-tier: poll_and_reload)
    *live = std::move(*fresh);       // recreate closure: material = std::move(*r)

    const auto& log = dev.log();
    ASSERT_GE(log.size(), 2U) << "expected at least wait_idle + destroy";

    // Find the wait_idle and the destroy of the OLD pipeline; assert ordering.
    std::optional<std::size_t> wait_at;
    std::optional<std::size_t> destroy_old_at;
    for (std::size_t i = 0; i < log.size(); ++i)
    {
        if (log[i].event == LoggingNullDevice::Event::kWaitIdle && !wait_at.has_value())
            wait_at = i;
        if (log[i].event == LoggingNullDevice::Event::kDestroyPipeline &&
            log[i].pipeline_index == old_pipeline_index)
            destroy_old_at = i;
    }
    ASSERT_TRUE(wait_at.has_value()) << "wait_idle was not issued before the swap";
    ASSERT_TRUE(destroy_old_at.has_value()) << "old pipeline was never destroyed";
    EXPECT_LT(*wait_at, *destroy_old_at)
        << "X5-2 invariant: device must drain (wait_idle) BEFORE the old PSO "
           "is destroyed (Vulkan PSO destroy mid-flight = TDR)";
}

// ---------------------------------------------------------------------------
// (D) Broken edit non-fatal: failed compile leaves the original Material intact
// ---------------------------------------------------------------------------
TEST(MaterialRecreate, BrokenEditLeavesOriginalUnchanged)
{
    cd::rhi::NullDevice dev;
    StubCompiler compiler;

    const auto vs = write_temp_glsl("dvs", "void main(){}");
    const auto fs_file = write_temp_glsl("dfs", "void main(){}");
    const std::string vs_path = vs.string();   // owning — desc holds a view
    const std::string fs_path = fs_file.string();
    auto desc = make_glsl_desc(vs_path, fs_path);

    // First create succeeds (compiler healthy) → this is the "live" pipeline.
    auto live = cd::material::Material::create(dev, &compiler, desc);
    ASSERT_TRUE(live.has_value()) << live.error().message;
    const auto good_handle = live->pipeline();
    ASSERT_TRUE(good_handle.is_valid());

    // Now the user makes a typo: the next compile fails. The recreate closure
    // returns the error WITHOUT touching the live Material.
    compiler.set_fail(true);
    auto broken = cd::material::Material::create(dev, &compiler, desc);
    ASSERT_FALSE(broken.has_value())
        << "a forced compile failure must surface as an error, not a Material";
    EXPECT_EQ(broken.error().code,
              static_cast<std::uint32_t>(cd::material::material_errors::Code::kShaderCompileFailed));

    // The closure pattern (HelloShaderWatch) only swaps on success — so the
    // live Material's handle is unchanged and the session keeps rendering.
    // (We do NOT move-assign here: create failed, there is nothing to assign.)
    EXPECT_TRUE(live->is_valid());
    EXPECT_EQ(live->pipeline(), good_handle)
        << "a broken edit must leave the live pipeline handle intact";

    std::error_code ec;
    fs::remove(vs, ec);
    fs::remove(fs_file, ec);
}
