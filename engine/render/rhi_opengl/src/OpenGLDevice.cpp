// =============================================================================
// CHROMODYNAMIC — cd/rhi_opengl/OpenGLDevice.cpp
// Phase 18.B / Wave 178 — boot-only OpenGL 4.6 backend.
// =============================================================================
#include <cd/rhi_opengl/OpenGLDevice.hpp>

#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <wingdi.h>
    #include <GL/gl.h>
#elif defined(__APPLE__)
    #include <OpenGL/gl.h>
#else
    #include <GL/gl.h>
#endif

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

namespace cd::rhi_opengl
{

#if defined(_WIN32)

namespace
{

class OpenGLDevice final : public cd::rhi::IDevice
{
public:
    OpenGLDevice() noexcept = default;

    [[nodiscard]] bool initialize(const GLCreateInfo& info)
    {
        // Minimal Win32 context bring-up: dummy hidden window + pixel
        // format + wglCreateContext. The "create_dummy_window=false"
        // path expects the caller's current context to be live; we
        // just probe glGetString in that case.
        if (info.create_dummy_window)
        {
            WNDCLASSEXW wc {};
            wc.cbSize = sizeof(wc);
            wc.style = CS_OWNDC;
            wc.lpfnWndProc = DefWindowProcW;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = L"CHROMODYNAMIC.rhi_opengl.dummy";
            RegisterClassExW(&wc);
            hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"cd_gl_dummy",
                                    WS_OVERLAPPED, 0, 0, 1, 1, nullptr,
                                    nullptr, wc.hInstance, nullptr);
            if (hwnd_ == nullptr) return false;
            hdc_ = GetDC(hwnd_);
            if (hdc_ == nullptr) return false;

            PIXELFORMATDESCRIPTOR pfd {};
            pfd.nSize = sizeof(pfd);
            pfd.nVersion = 1;
            pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
            pfd.iPixelType = PFD_TYPE_RGBA;
            pfd.cColorBits = 32;
            pfd.cDepthBits = 24;
            pfd.cStencilBits = 8;
            const int pf = ChoosePixelFormat(hdc_, &pfd);
            if (pf == 0 || !SetPixelFormat(hdc_, pf, &pfd)) return false;
            hglrc_ = wglCreateContext(hdc_);
            if (hglrc_ == nullptr) return false;
            wglMakeCurrent(hdc_, hglrc_);
        }
        // Read renderer / version string.
        if (const auto* renderer = glGetString(GL_RENDERER))
            adapter_name_ = reinterpret_cast<const char*>(renderer);
        else
            adapter_name_ = "OpenGL renderer (unknown)";

        if (const auto* version = glGetString(GL_VERSION))
        {
            const char* v = reinterpret_cast<const char*>(version);
            // Manual digit parse so MSVC's deprecated-sscanf warning
            // doesn't fire under -Werror.
            int major = 0, minor = 0;
            const char* p = v;
            while (*p >= '0' && *p <= '9') { major = major * 10 + (*p - '0'); ++p; }
            if (*p == '.') { ++p; while (*p >= '0' && *p <= '9') { minor = minor * 10 + (*p - '0'); ++p; } }
            const std::uint32_t got = static_cast<std::uint32_t>(major * 10 + minor);
            if (got < info.min_version) return false;
        }
        return true;
    }

    ~OpenGLDevice() override
    {
        if (hglrc_ != nullptr)
        {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(hglrc_);
        }
        if (hdc_ != nullptr && hwnd_ != nullptr)
            ReleaseDC(hwnd_, hdc_);
        if (hwnd_ != nullptr)
            DestroyWindow(hwnd_);
    }

    // ---- Introspection (REAL) ---------------------------------------------
    [[nodiscard]] cd::rhi::Backend backend() const noexcept override
    {
        return cd::rhi::Backend::kOpenGL;
    }
    [[nodiscard]] std::string_view adapter_name() const noexcept override
    {
        return adapter_name_;
    }
    [[nodiscard]] const cd::rhi::DeviceLimits& limits() const noexcept override
    {
        return limits_;
    }
    [[nodiscard]] const cd::rhi::DeviceFeatures& features() const noexcept override
    {
        return features_;
    }
    void wait_idle() override
    {
        glFinish();
    }

    // ---- Every resource entry returns kNotImplemented at boot ------------
#define CD_GL_NOT_IMPL_RESULT(rt)                                                 \
    return std::unexpected(cd::rhi::rhi_errors::make(                             \
        cd::rhi::rhi_errors::Code::kNotImplemented,                               \
        "OpenGL backend is boot-only at v0.49.0; entry point queued for "         \
        "follow-up waves"))

    [[nodiscard]] cd::core::Result<cd::rhi::BufferHandle>
    create_buffer(const cd::rhi::BufferDesc&) override { CD_GL_NOT_IMPL_RESULT(BufferHandle); }
    void destroy_buffer(cd::rhi::BufferHandle) override {}
    [[nodiscard]] cd::core::Result<cd::rhi::TextureHandle>
    create_texture(const cd::rhi::TextureDesc&) override { CD_GL_NOT_IMPL_RESULT(TextureHandle); }
    void destroy_texture(cd::rhi::TextureHandle) override {}
    [[nodiscard]] cd::core::Result<cd::rhi::TextureViewHandle>
    create_texture_view(const cd::rhi::TextureViewDesc&) override { CD_GL_NOT_IMPL_RESULT(TextureViewHandle); }
    void destroy_texture_view(cd::rhi::TextureViewHandle) override {}
    [[nodiscard]] cd::core::Result<cd::rhi::SamplerHandle>
    create_sampler(const cd::rhi::SamplerDesc&) override { CD_GL_NOT_IMPL_RESULT(SamplerHandle); }
    void destroy_sampler(cd::rhi::SamplerHandle) override {}
    [[nodiscard]] cd::core::Result<cd::rhi::ShaderModuleHandle>
    create_shader_module(const cd::rhi::ShaderModuleDesc&) override { CD_GL_NOT_IMPL_RESULT(ShaderModuleHandle); }
    void destroy_shader_module(cd::rhi::ShaderModuleHandle) override {}
    [[nodiscard]] cd::core::Result<cd::rhi::DescriptorSetLayoutHandle>
    create_descriptor_set_layout(const cd::rhi::DescriptorSetLayoutDesc&) override { CD_GL_NOT_IMPL_RESULT(DescriptorSetLayoutHandle); }
    void destroy_descriptor_set_layout(cd::rhi::DescriptorSetLayoutHandle) override {}
    [[nodiscard]] cd::core::Result<cd::rhi::PipelineLayoutHandle>
    create_pipeline_layout(const cd::rhi::PipelineLayoutDesc&) override { CD_GL_NOT_IMPL_RESULT(PipelineLayoutHandle); }
    void destroy_pipeline_layout(cd::rhi::PipelineLayoutHandle) override {}
    [[nodiscard]] cd::core::Result<cd::rhi::GraphicsPipelineHandle>
    create_graphics_pipeline(const cd::rhi::GraphicsPipelineDesc&) override { CD_GL_NOT_IMPL_RESULT(GraphicsPipelineHandle); }
    void destroy_graphics_pipeline(cd::rhi::GraphicsPipelineHandle) override {}
    [[nodiscard]] cd::core::Result<cd::rhi::ComputePipelineHandle>
    create_compute_pipeline(const cd::rhi::ComputePipelineDesc&) override { CD_GL_NOT_IMPL_RESULT(ComputePipelineHandle); }
    void destroy_compute_pipeline(cd::rhi::ComputePipelineHandle) override {}
    [[nodiscard]] cd::core::Result<cd::rhi::DescriptorSetHandle>
    allocate_descriptor_set(cd::rhi::DescriptorSetLayoutHandle) override { CD_GL_NOT_IMPL_RESULT(DescriptorSetHandle); }
    void destroy_descriptor_set(cd::rhi::DescriptorSetHandle) override {}
    [[nodiscard]] cd::core::Result<void>
    update_descriptor_set(cd::rhi::DescriptorSetHandle, std::span<const cd::rhi::DescriptorWrite>) override
    {
        return std::unexpected(cd::rhi::rhi_errors::make(
            cd::rhi::rhi_errors::Code::kNotImplemented, "OpenGL boot-only"));
    }
    [[nodiscard]] cd::core::Result<cd::rhi::SemaphoreHandle>
    create_semaphore() override { CD_GL_NOT_IMPL_RESULT(SemaphoreHandle); }
    void destroy_semaphore(cd::rhi::SemaphoreHandle) override {}
    [[nodiscard]] cd::core::Result<cd::rhi::FenceHandle>
    create_fence(bool) override { CD_GL_NOT_IMPL_RESULT(FenceHandle); }
    void destroy_fence(cd::rhi::FenceHandle) override {}
    [[nodiscard]] cd::core::Result<void>
    wait_for_fence(cd::rhi::FenceHandle, std::uint64_t) override
    {
        return std::unexpected(cd::rhi::rhi_errors::make(
            cd::rhi::rhi_errors::Code::kNotImplemented, "OpenGL boot-only"));
    }
    void reset_fence(cd::rhi::FenceHandle) override {}
    [[nodiscard]] bool is_fence_signaled(cd::rhi::FenceHandle) override { return false; }
    [[nodiscard]] cd::core::Result<cd::rhi::TimelineSemaphoreHandle>
    create_timeline_semaphore(std::uint64_t) override { CD_GL_NOT_IMPL_RESULT(TimelineSemaphoreHandle); }
    void destroy_timeline_semaphore(cd::rhi::TimelineSemaphoreHandle) override {}
    [[nodiscard]] cd::core::Result<void>
    wait_timeline_semaphore(cd::rhi::TimelineSemaphoreHandle, std::uint64_t, std::uint64_t) override
    {
        return std::unexpected(cd::rhi::rhi_errors::make(
            cd::rhi::rhi_errors::Code::kNotImplemented, "OpenGL boot-only"));
    }
    [[nodiscard]] cd::core::Result<void>
    signal_timeline_semaphore(cd::rhi::TimelineSemaphoreHandle, std::uint64_t) override
    {
        return std::unexpected(cd::rhi::rhi_errors::make(
            cd::rhi::rhi_errors::Code::kNotImplemented, "OpenGL boot-only"));
    }
    [[nodiscard]] std::uint64_t timeline_semaphore_value(cd::rhi::TimelineSemaphoreHandle) const override
    {
        return 0;
    }
    [[nodiscard]] cd::core::Result<std::uint32_t>
    acquire_next_image(cd::rhi::SwapchainHandle, cd::rhi::SemaphoreHandle, cd::rhi::FenceHandle, std::uint64_t) override
    {
        return std::unexpected(cd::rhi::rhi_errors::make(
            cd::rhi::rhi_errors::Code::kNotImplemented, "OpenGL boot-only"));
    }
    [[nodiscard]] cd::core::Result<void>
    present(cd::rhi::SwapchainHandle, std::uint32_t, std::span<const cd::rhi::SemaphoreHandle>) override
    {
        return std::unexpected(cd::rhi::rhi_errors::make(
            cd::rhi::rhi_errors::Code::kNotImplemented, "OpenGL boot-only"));
    }
    [[nodiscard]] cd::rhi::TextureViewHandle swapchain_image_view(cd::rhi::SwapchainHandle, std::uint32_t) const override { return {}; }
    [[nodiscard]] std::uint32_t swapchain_image_count(cd::rhi::SwapchainHandle) const override { return 0; }
    [[nodiscard]] cd::rhi::TextureHandle swapchain_image(cd::rhi::SwapchainHandle, std::uint32_t) const override { return {}; }
    [[nodiscard]] cd::core::Result<void>
    upload_buffer(cd::rhi::BufferHandle, std::uint64_t, std::span<const std::byte>) override
    {
        return std::unexpected(cd::rhi::rhi_errors::make(
            cd::rhi::rhi_errors::Code::kNotImplemented, "OpenGL boot-only"));
    }
    [[nodiscard]] cd::core::Result<void>
    download_buffer(cd::rhi::BufferHandle, std::uint64_t, std::span<std::byte>) override
    {
        return std::unexpected(cd::rhi::rhi_errors::make(
            cd::rhi::rhi_errors::Code::kNotImplemented, "OpenGL boot-only"));
    }
    [[nodiscard]] cd::core::Result<cd::rhi::SwapchainHandle>
    create_swapchain(const cd::rhi::SwapchainDesc&) override { CD_GL_NOT_IMPL_RESULT(SwapchainHandle); }
    void destroy_swapchain(cd::rhi::SwapchainHandle) override {}
    [[nodiscard]] std::unique_ptr<cd::rhi::ICommandBuffer>
    create_command_buffer(cd::rhi::QueueType) override { return nullptr; }
    void submit(cd::rhi::ICommandBuffer&) override {}
    [[nodiscard]] cd::core::Result<void>
    submit(const cd::rhi::SubmitDesc&) override
    {
        return std::unexpected(cd::rhi::rhi_errors::make(
            cd::rhi::rhi_errors::Code::kNotImplemented, "OpenGL boot-only"));
    }
#undef CD_GL_NOT_IMPL_RESULT

private:
    HWND  hwnd_  { nullptr };
    HDC   hdc_   { nullptr };
    HGLRC hglrc_ { nullptr };
    std::string adapter_name_;
    cd::rhi::DeviceLimits   limits_   {};
    cd::rhi::DeviceFeatures features_ {};
};

}  // namespace

cd::core::Result<std::unique_ptr<cd::rhi::IDevice>>
create_gl_device(GLCreateInfo info)
{
    auto dev = std::make_unique<OpenGLDevice>();
    if (!dev->initialize(info))
    {
        return std::unexpected(cd::rhi::rhi_errors::make(
            cd::rhi::rhi_errors::Code::kBackendInitFailed,
            "OpenGL boot failed: no driver, version too low, or wgl create failed"));
    }
    return std::unique_ptr<cd::rhi::IDevice>(std::move(dev));
}

#else  // _WIN32 — non-Windows path is a stub for v0.49.0

cd::core::Result<std::unique_ptr<cd::rhi::IDevice>>
create_gl_device(GLCreateInfo /*info*/)
{
    return std::unexpected(cd::rhi::rhi_errors::make(
        cd::rhi::rhi_errors::Code::kBackendInitFailed,
        "OpenGL backend boot path only ships on Windows at v0.49.0; "
        "Linux/macOS context-creation lands in a follow-up wave"));
}

#endif  // _WIN32

}  // namespace cd::rhi_opengl
