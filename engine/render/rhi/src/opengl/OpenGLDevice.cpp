// =============================================================================
// CHROMODYNAMIC — cd/rhi/opengl/OpenGLDevice.cpp
// Phase 18.B / Wave 178 — boot-only OpenGL 4.6 backend.
// =============================================================================
#include <cd/rhi/opengl/OpenGLDevice.hpp>

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
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cd::rhi::opengl
{

#if defined(_WIN32)

namespace
{

// Phase 137 — minimum-viable WGL extension loader. opengl32.lib only
// exports GL 1.1 symbols; anything past that (DSA buffer functions,
// Direct State Access etc.) has to come from `wglGetProcAddress`.
// We bind the handful of functions create_buffer needs and leave the
// rest of the surface stubbed out.

using GLuint    = unsigned int;
using GLsizei   = int;
using GLsizeiptr = long long;  // matches Khronos khrplatform.h for x64
using GLenum    = unsigned int;
using GLbitfield = unsigned int;
using GLvoid    = void;

constexpr GLenum     kGL_NoError                  = 0;
// glMapBufferRange access bits used by create_buffer's host-visible path.
constexpr GLbitfield kGL_MapReadBit                = 0x0001;
constexpr GLbitfield kGL_MapWriteBit               = 0x0002;
constexpr GLbitfield kGL_DynamicStorageBit         = 0x0100;

// Phase 143-144 — texture / sampler enums (subset)
constexpr GLenum     kGL_Texture2D                 = 0x0DE1;
constexpr GLenum     kGL_RGBA8                     = 0x8058;
constexpr GLenum     kGL_R8                        = 0x8229;
constexpr GLenum     kGL_DEPTH_COMPONENT24         = 0x81A6;
constexpr GLenum     kGL_DEPTH24_STENCIL8          = 0x88F0;
constexpr GLenum     kGL_TextureMinFilter          = 0x2801;
constexpr GLenum     kGL_TextureMagFilter          = 0x2800;
constexpr GLenum     kGL_TextureWrapS              = 0x2802;
constexpr GLenum     kGL_TextureWrapT              = 0x2803;
constexpr GLenum     kGL_TextureWrapR              = 0x8072;
constexpr GLenum     kGL_Repeat                    = 0x2901;
constexpr GLenum     kGL_ClampToEdge               = 0x812F;
constexpr GLenum     kGL_MirroredRepeat            = 0x8370;
constexpr GLenum     kGL_Nearest                   = 0x2600;
constexpr GLenum     kGL_Linear                    = 0x2601;
constexpr GLenum     kGL_LinearMipmapLinear        = 0x2703;

// Phase 173 — draw + state enums
constexpr GLenum     kGL_COLOR_BUFFER_BIT          = 0x4000;
constexpr GLenum     kGL_DEPTH_BUFFER_BIT          = 0x0100;
constexpr GLenum     kGL_TRIANGLES                 = 0x0004;
constexpr GLenum     kGL_ARRAY_BUFFER              = 0x8892;
constexpr GLenum     kGL_ELEMENT_ARRAY_BUFFER      = 0x8893;
constexpr GLenum     kGL_UNSIGNED_SHORT            = 0x1403;
constexpr GLenum     kGL_UNSIGNED_INT              = 0x1405;

// Phase 146-147 — shader / program enums
constexpr GLenum     kGL_VertexShader              = 0x8B31;
constexpr GLenum     kGL_FragmentShader            = 0x8B30;
constexpr GLenum     kGL_GeometryShader            = 0x8DD9;
constexpr GLenum     kGL_ComputeShader             = 0x91B9;
constexpr GLenum     kGL_CompileStatus             = 0x8B81;
constexpr GLenum     kGL_LinkStatus                = 0x8B82;
constexpr GLenum     kGL_InfoLogLength             = 0x8B84;

using PFNGLCREATEBUFFERSPROC      = void (*)(GLsizei n, GLuint* buffers);
using PFNGLDELETEBUFFERSPROC      = void (*)(GLsizei n, const GLuint* buffers);
using PFNGLNAMEDBUFFERDATAPROC    = void (*)(GLuint buffer, GLsizeiptr size, const GLvoid* data, GLenum usage);
using PFNGLNAMEDBUFFERSUBDATAPROC = void (*)(GLuint buffer, GLsizeiptr offset, GLsizeiptr size, const GLvoid* data);
using PFNGLNAMEDBUFFERSTORAGEPROC = void (*)(GLuint buffer, GLsizeiptr size, const GLvoid* data, GLbitfield flags);
using PFNGLGETERRORPROC           = GLenum (*)();

// Phase 143/144 — texture + sampler DSA entry points.
using PFNGLCREATETEXTURESPROC     = void (*)(GLenum target, GLsizei n, GLuint* textures);
using PFNGLDELETETEXTURESPROC     = void (*)(GLsizei n, const GLuint* textures);
using PFNGLTEXTURESTORAGE2DPROC   = void (*)(GLuint texture, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height);
using PFNGLTEXTURESTORAGE3DPROC   = void (*)(GLuint texture, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth);
using PFNGLCREATESAMPLERSPROC     = void (*)(GLsizei n, GLuint* samplers);
using PFNGLDELETESAMPLERSPROC     = void (*)(GLsizei n, const GLuint* samplers);
using PFNGLSAMPLERPARAMETERIPROC  = void (*)(GLuint sampler, GLenum pname, int param);

// Phase 173 — draw + state entry points.
using PFNGLCLEARCOLORPROC          = void (*)(float, float, float, float);
using PFNGLCLEARPROC               = void (*)(GLbitfield);
using PFNGLVIEWPORTPROC            = void (*)(int, int, GLsizei, GLsizei);
using PFNGLSCISSORPROC             = void (*)(int, int, GLsizei, GLsizei);
using PFNGLDRAWARRAYSPROC          = void (*)(GLenum, int, GLsizei);
using PFNGLDRAWELEMENTSPROC        = void (*)(GLenum, GLsizei, GLenum, const void*);
using PFNGLBINDBUFFERPROC          = void (*)(GLenum, GLuint);
using PFNGLBINDVERTEXARRAYPROC     = void (*)(GLuint);
using PFNGLGENVERTEXARRAYSPROC     = void (*)(GLsizei, GLuint*);
using PFNGLDELETEVERTEXARRAYSPROC  = void (*)(GLsizei, const GLuint*);
using PFNGLUSEPROGRAMPROC          = void (*)(GLuint);
using PFNGLENABLEPROC              = void (*)(GLenum);
using PFNGLDISABLEPROC             = void (*)(GLenum);

// Phase 146/147 — shader compile + program link entry points.
using GLchar = char;
using PFNGLCREATESHADERPROC       = GLuint (*)(GLenum type);
using PFNGLDELETESHADERPROC       = void (*)(GLuint shader);
using PFNGLSHADERSOURCEPROC       = void (*)(GLuint shader, GLsizei count, const GLchar* const* string, const int* length);
using PFNGLCOMPILESHADERPROC      = void (*)(GLuint shader);
using PFNGLGETSHADERIVPROC        = void (*)(GLuint shader, GLenum pname, int* params);
using PFNGLGETSHADERINFOLOGPROC   = void (*)(GLuint shader, GLsizei maxLength, GLsizei* length, GLchar* infoLog);
using PFNGLCREATEPROGRAMPROC      = GLuint (*)();
using PFNGLDELETEPROGRAMPROC      = void (*)(GLuint program);
using PFNGLATTACHSHADERPROC       = void (*)(GLuint program, GLuint shader);
using PFNGLLINKPROGRAMPROC        = void (*)(GLuint program);
using PFNGLGETPROGRAMIVPROC       = void (*)(GLuint program, GLenum pname, int* params);
using PFNGLGETPROGRAMINFOLOGPROC  = void (*)(GLuint program, GLsizei maxLength, GLsizei* length, GLchar* infoLog);

struct GLLoader
{
    PFNGLCREATEBUFFERSPROC      glCreateBuffers      { nullptr };
    PFNGLDELETEBUFFERSPROC      glDeleteBuffersDSA   { nullptr };
    PFNGLNAMEDBUFFERDATAPROC    glNamedBufferData    { nullptr };
    PFNGLNAMEDBUFFERSUBDATAPROC glNamedBufferSubData { nullptr };
    PFNGLNAMEDBUFFERSTORAGEPROC glNamedBufferStorage { nullptr };
    PFNGLGETERRORPROC           glGetErrorPtr        { nullptr };

    // Phase 143/144 — texture + sampler DSA. Optional; nullptr means
    // the GL implementation lacks DSA 4.5 and the texture/sampler
    // paths still return kNotImplemented.
    PFNGLCREATETEXTURESPROC     glCreateTextures     { nullptr };
    PFNGLDELETETEXTURESPROC     glDeleteTexturesDSA  { nullptr };
    PFNGLTEXTURESTORAGE2DPROC   glTextureStorage2D   { nullptr };
    PFNGLTEXTURESTORAGE3DPROC   glTextureStorage3D   { nullptr };
    PFNGLCREATESAMPLERSPROC     glCreateSamplers     { nullptr };
    PFNGLDELETESAMPLERSPROC     glDeleteSamplersDSA  { nullptr };
    PFNGLSAMPLERPARAMETERIPROC  glSamplerParameteri  { nullptr };

    // Phase 146/147 — shader + program.
    PFNGLCREATESHADERPROC       glCreateShader       { nullptr };
    PFNGLDELETESHADERPROC       glDeleteShader       { nullptr };
    PFNGLSHADERSOURCEPROC       glShaderSource       { nullptr };
    PFNGLCOMPILESHADERPROC      glCompileShader      { nullptr };
    PFNGLGETSHADERIVPROC        glGetShaderiv        { nullptr };
    PFNGLGETSHADERINFOLOGPROC   glGetShaderInfoLog   { nullptr };
    PFNGLCREATEPROGRAMPROC      glCreateProgram      { nullptr };
    PFNGLDELETEPROGRAMPROC      glDeleteProgram      { nullptr };
    PFNGLATTACHSHADERPROC       glAttachShader       { nullptr };
    PFNGLLINKPROGRAMPROC        glLinkProgram        { nullptr };
    PFNGLGETPROGRAMIVPROC       glGetProgramiv       { nullptr };
    PFNGLGETPROGRAMINFOLOGPROC  glGetProgramInfoLog  { nullptr };

    // Phase 173 — draw + state.
    PFNGLCLEARCOLORPROC         glClearColor         { nullptr };
    PFNGLCLEARPROC              glClear              { nullptr };
    PFNGLVIEWPORTPROC           glViewport           { nullptr };
    PFNGLSCISSORPROC            glScissor            { nullptr };
    PFNGLDRAWARRAYSPROC         glDrawArrays         { nullptr };
    PFNGLDRAWELEMENTSPROC       glDrawElements       { nullptr };
    PFNGLBINDBUFFERPROC         glBindBufferGL       { nullptr };  // glBindBuffer clashes with internal naming
    PFNGLBINDVERTEXARRAYPROC    glBindVertexArray    { nullptr };
    PFNGLGENVERTEXARRAYSPROC    glGenVertexArrays    { nullptr };
    PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays { nullptr };
    PFNGLUSEPROGRAMPROC         glUseProgram         { nullptr };
    PFNGLENABLEPROC             glEnable             { nullptr };
    PFNGLDISABLEPROC            glDisable            { nullptr };

    [[nodiscard]] bool valid() const noexcept
    {
        return glCreateBuffers != nullptr
            && glNamedBufferStorage != nullptr
            && glNamedBufferSubData != nullptr
            && glDeleteBuffersDSA != nullptr;
    }
    [[nodiscard]] bool texture_valid() const noexcept
    {
        return glCreateTextures != nullptr && glTextureStorage2D != nullptr
            && glDeleteTexturesDSA != nullptr;
    }
    [[nodiscard]] bool sampler_valid() const noexcept
    {
        return glCreateSamplers != nullptr && glSamplerParameteri != nullptr
            && glDeleteSamplersDSA != nullptr;
    }
    [[nodiscard]] bool shader_valid() const noexcept
    {
        return glCreateShader != nullptr && glShaderSource != nullptr
            && glCompileShader != nullptr && glGetShaderiv != nullptr
            && glDeleteShader != nullptr;
    }
    [[nodiscard]] bool program_valid() const noexcept
    {
        return glCreateProgram != nullptr && glAttachShader != nullptr
            && glLinkProgram != nullptr && glGetProgramiv != nullptr
            && glDeleteProgram != nullptr;
    }
};

[[nodiscard]] inline GLLoader load_dsa_buffer_funcs()
{
    GLLoader L {};
    // wglGetProcAddress doesn't expose OpenGL 1.0/1.1 functions —
    // those are in opengl32.dll directly. Phase 143 hit this: the
    // DSA-3.3 sampler delete (glDeleteSamplers) resolved fine via
    // wglGetProcAddress, but glDeleteTextures (a GL-1.1 function)
    // came back NULL. Dual-resolution fixes the inconsistency.
    HMODULE opengl_dll = GetModuleHandleW(L"opengl32.dll");
    if (opengl_dll == nullptr) opengl_dll = LoadLibraryW(L"opengl32.dll");
    auto get = [opengl_dll](const char* name) -> PROC {
        PROC p = wglGetProcAddress(name);
        const auto v = reinterpret_cast<intptr_t>(p);
        if (v == 0 || v == 1 || v == 2 || v == 3 || v == -1) p = nullptr;
        if (p == nullptr && opengl_dll != nullptr)
            p = GetProcAddress(opengl_dll, name);
        return p;
    };
    L.glCreateBuffers      = reinterpret_cast<PFNGLCREATEBUFFERSPROC>(get("glCreateBuffers"));
    L.glDeleteBuffersDSA   = reinterpret_cast<PFNGLDELETEBUFFERSPROC>(get("glDeleteBuffers"));
    L.glNamedBufferData    = reinterpret_cast<PFNGLNAMEDBUFFERDATAPROC>(get("glNamedBufferData"));
    L.glNamedBufferSubData = reinterpret_cast<PFNGLNAMEDBUFFERSUBDATAPROC>(get("glNamedBufferSubData"));
    L.glNamedBufferStorage = reinterpret_cast<PFNGLNAMEDBUFFERSTORAGEPROC>(get("glNamedBufferStorage"));
    L.glGetErrorPtr        = reinterpret_cast<PFNGLGETERRORPROC>(get("glGetError"));
    // Phase 143/144 — DSA texture + sampler.
    L.glCreateTextures     = reinterpret_cast<PFNGLCREATETEXTURESPROC>(get("glCreateTextures"));
    L.glDeleteTexturesDSA  = reinterpret_cast<PFNGLDELETETEXTURESPROC>(get("glDeleteTextures"));
    L.glTextureStorage2D   = reinterpret_cast<PFNGLTEXTURESTORAGE2DPROC>(get("glTextureStorage2D"));
    L.glTextureStorage3D   = reinterpret_cast<PFNGLTEXTURESTORAGE3DPROC>(get("glTextureStorage3D"));
    L.glCreateSamplers     = reinterpret_cast<PFNGLCREATESAMPLERSPROC>(get("glCreateSamplers"));
    L.glDeleteSamplersDSA  = reinterpret_cast<PFNGLDELETESAMPLERSPROC>(get("glDeleteSamplers"));
    L.glSamplerParameteri  = reinterpret_cast<PFNGLSAMPLERPARAMETERIPROC>(get("glSamplerParameteri"));
    // Phase 146/147 — shader + program.
    L.glCreateShader      = reinterpret_cast<PFNGLCREATESHADERPROC>(get("glCreateShader"));
    L.glDeleteShader      = reinterpret_cast<PFNGLDELETESHADERPROC>(get("glDeleteShader"));
    L.glShaderSource      = reinterpret_cast<PFNGLSHADERSOURCEPROC>(get("glShaderSource"));
    L.glCompileShader     = reinterpret_cast<PFNGLCOMPILESHADERPROC>(get("glCompileShader"));
    L.glGetShaderiv       = reinterpret_cast<PFNGLGETSHADERIVPROC>(get("glGetShaderiv"));
    L.glGetShaderInfoLog  = reinterpret_cast<PFNGLGETSHADERINFOLOGPROC>(get("glGetShaderInfoLog"));
    L.glCreateProgram     = reinterpret_cast<PFNGLCREATEPROGRAMPROC>(get("glCreateProgram"));
    L.glDeleteProgram     = reinterpret_cast<PFNGLDELETEPROGRAMPROC>(get("glDeleteProgram"));
    L.glAttachShader      = reinterpret_cast<PFNGLATTACHSHADERPROC>(get("glAttachShader"));
    L.glLinkProgram       = reinterpret_cast<PFNGLLINKPROGRAMPROC>(get("glLinkProgram"));
    L.glGetProgramiv      = reinterpret_cast<PFNGLGETPROGRAMIVPROC>(get("glGetProgramiv"));
    L.glGetProgramInfoLog = reinterpret_cast<PFNGLGETPROGRAMINFOLOGPROC>(get("glGetProgramInfoLog"));
    // Phase 173 — draw + state.
    L.glClearColor        = reinterpret_cast<PFNGLCLEARCOLORPROC>(get("glClearColor"));
    L.glClear             = reinterpret_cast<PFNGLCLEARPROC>(get("glClear"));
    L.glViewport          = reinterpret_cast<PFNGLVIEWPORTPROC>(get("glViewport"));
    L.glScissor           = reinterpret_cast<PFNGLSCISSORPROC>(get("glScissor"));
    L.glDrawArrays        = reinterpret_cast<PFNGLDRAWARRAYSPROC>(get("glDrawArrays"));
    L.glDrawElements      = reinterpret_cast<PFNGLDRAWELEMENTSPROC>(get("glDrawElements"));
    L.glBindBufferGL      = reinterpret_cast<PFNGLBINDBUFFERPROC>(get("glBindBuffer"));
    L.glBindVertexArray   = reinterpret_cast<PFNGLBINDVERTEXARRAYPROC>(get("glBindVertexArray"));
    L.glGenVertexArrays   = reinterpret_cast<PFNGLGENVERTEXARRAYSPROC>(get("glGenVertexArrays"));
    L.glDeleteVertexArrays= reinterpret_cast<PFNGLDELETEVERTEXARRAYSPROC>(get("glDeleteVertexArrays"));
    L.glUseProgram        = reinterpret_cast<PFNGLUSEPROGRAMPROC>(get("glUseProgram"));
    L.glEnable            = reinterpret_cast<PFNGLENABLEPROC>(get("glEnable"));
    L.glDisable           = reinterpret_cast<PFNGLDISABLEPROC>(get("glDisable"));
    return L;
}

// Forward declare so GLCommandBuffer can hold a back-pointer.
class OpenGLDevice;

// =============================================================================
// Phase 173 — OpenGL ICommandBuffer (deferred-command record/replay).
//
// GL has implicit ordering on the bound context — every call executes
// in order on the worker thread. We model the command-buffer surface
// as a vector of std::function<void()> that submit() replays in order
// after begin/end has been called.
//
// This isn't true command-buffer parallelism (GL can't record from
// multiple threads), but it unifies the API with Vulkan/D3D12 so the
// rest of the engine sees one shape.
// =============================================================================
class GLCommandBuffer final : public cd::rhi::ICommandBuffer
{
public:
    GLCommandBuffer(OpenGLDevice* owner, const GLLoader* gl) noexcept
        : owner_ { owner }, gl_ { gl } {}
    ~GLCommandBuffer() override = default;
    GLCommandBuffer(const GLCommandBuffer&) = delete;
    GLCommandBuffer& operator=(const GLCommandBuffer&) = delete;
    GLCommandBuffer(GLCommandBuffer&&) = delete;
    GLCommandBuffer& operator=(GLCommandBuffer&&) = delete;

    void begin() override { cmds_.clear(); recording_ = true; }
    void end()   override { recording_ = false; }

    void begin_render_pass(const cd::rhi::RenderPassBeginInfo& info) override
    {
        // Apply per-attachment LoadOp::kClear → glClearColor + glClear.
        for (const auto& a : info.color_attachments)
        {
            if (a.load_op == cd::rhi::LoadOp::kClear)
            {
                const float r = a.clear_color.f32[0];
                const float g = a.clear_color.f32[1];
                const float b = a.clear_color.f32[2];
                const float al = a.clear_color.f32[3];
                cmds_.emplace_back([g_ = gl_, r, g, b, al]() {
                    if (g_->glClearColor) g_->glClearColor(r, g, b, al);
                    if (g_->glClear)      g_->glClear(kGL_COLOR_BUFFER_BIT);
                });
            }
        }
        if (info.depth_stencil != nullptr &&
            info.depth_stencil->depth_load == cd::rhi::LoadOp::kClear)
        {
            cmds_.emplace_back([g_ = gl_]() {
                if (g_->glClear) g_->glClear(kGL_DEPTH_BUFFER_BIT);
            });
        }
    }
    void end_render_pass() override {}

    void bind_graphics_pipeline(cd::rhi::GraphicsPipelineHandle h) override;  // defined below

    void bind_compute_pipeline(cd::rhi::ComputePipelineHandle) override {}
    void bind_descriptor_set(std::uint32_t, cd::rhi::DescriptorSetHandle) override {}

    void bind_vertex_buffer(std::uint32_t, cd::rhi::BufferHandle buffer, std::uint64_t) override;
    void bind_index_buffer(cd::rhi::BufferHandle buffer, std::uint64_t, cd::rhi::IndexType type) override;

    void push_constants(cd::rhi::PipelineLayoutHandle, cd::rhi::ShaderStage,
                        std::uint32_t, std::uint32_t, const void*) override {}

    void set_viewport(const cd::rhi::Viewport& vp) override
    {
        const float x = vp.x, y = vp.y, w = vp.width, h = vp.height;
        cmds_.emplace_back([g_ = gl_, x, y, w, h]() {
            if (g_->glViewport)
                g_->glViewport(static_cast<int>(x), static_cast<int>(y),
                               static_cast<GLsizei>(w), static_cast<GLsizei>(h));
        });
    }
    void set_scissor(const cd::rhi::Rect2D& r) override
    {
        const auto x = r.offset.x, y = r.offset.y;
        const auto w = r.extent.width, h = r.extent.height;
        cmds_.emplace_back([g_ = gl_, x, y, w, h]() {
            if (g_->glScissor)
                g_->glScissor(x, y, static_cast<GLsizei>(w), static_cast<GLsizei>(h));
        });
    }

    void draw(std::uint32_t vertex_count, std::uint32_t instance_count,
              std::uint32_t first_vertex, std::uint32_t first_instance) override
    {
        (void)instance_count; (void)first_instance;  // instanced draw is v1.4
        const auto fv = first_vertex, vc = vertex_count;
        cmds_.emplace_back([g_ = gl_, fv, vc]() {
            if (g_->glDrawArrays)
                g_->glDrawArrays(kGL_TRIANGLES, static_cast<int>(fv), static_cast<GLsizei>(vc));
        });
    }
    void draw_indexed(std::uint32_t index_count, std::uint32_t instance_count,
                      std::uint32_t first_index, std::int32_t vertex_offset,
                      std::uint32_t first_instance) override
    {
        (void)instance_count; (void)vertex_offset; (void)first_instance;
        const auto fi = first_index;
        const auto ic = index_count;
        const auto t  = index_type_;
        cmds_.emplace_back([g_ = gl_, fi, ic, t]() {
            if (g_->glDrawElements)
            {
                const std::size_t stride = (t == cd::rhi::IndexType::kUInt16) ? 2 : 4;
                g_->glDrawElements(kGL_TRIANGLES, static_cast<GLsizei>(ic),
                                   t == cd::rhi::IndexType::kUInt16 ? kGL_UNSIGNED_SHORT : kGL_UNSIGNED_INT,
                                   reinterpret_cast<const void*>(fi * stride));
            }
        });
    }
    void dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override {}
    void copy_buffer(cd::rhi::BufferHandle, cd::rhi::BufferHandle, std::span<const cd::rhi::BufferCopyRegion>) override {}
    void copy_buffer_to_image(cd::rhi::BufferHandle, cd::rhi::TextureHandle, std::span<const cd::rhi::BufferImageCopyRegion>) override {}
    void copy_image_to_buffer(cd::rhi::TextureHandle, cd::rhi::BufferHandle, std::span<const cd::rhi::BufferImageCopyRegion>) override {}
    void barrier(std::span<const cd::rhi::BufferBarrier>, std::span<const cd::rhi::TextureBarrier>) override {}
    void push_debug_group(std::string_view) override {}
    void pop_debug_group() override {}

    // Replay all recorded commands in order. Called from OpenGLDevice::submit.
    void replay() noexcept
    {
        for (const auto& c : cmds_) c();
    }

private:
    OpenGLDevice* owner_ { nullptr };
    const GLLoader* gl_  { nullptr };
    std::vector<std::function<void()>> cmds_;
    cd::rhi::IndexType index_type_ { cd::rhi::IndexType::kUInt16 };
    bool recording_ { false };
};

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

        // Phase 137 — bind DSA buffer entry points via wglGetProcAddress.
        // Other entry points (texture, swapchain, pipeline) stay
        // kNotImplemented until their respective phases land.
        gl_ = load_dsa_buffer_funcs();

        if (const auto* version = glGetString(GL_VERSION))
        {
            const char* v = reinterpret_cast<const char*>(version);
            // Manual digit parse so MSVC's deprecated-sscanf warning
            // doesn't fire under -Werror.
            int major = 0, minor = 0;
            const char* p = v;
            while (*p >= '0' && *p <= '9') { major = major * 10 + (*p - '0'); ++p; }
            if (*p == '.') { ++p; while (*p >= '0' && *p <= '9') { minor = minor * 10 + (*p - '0'); ++p; } }
            const auto got = static_cast<std::uint32_t>(major * 10 + minor);
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

    // Phase 137 — DSA buffer create. Texture + swapchain stay stubbed
    // until their loader-tax-equivalent lands.
    [[nodiscard]] cd::core::Result<cd::rhi::BufferHandle>
    create_buffer(const cd::rhi::BufferDesc& desc) override
    {
        if (!gl_.valid())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kNotImplemented,
                "OpenGL backend: DSA buffer entry points not exported by the driver"));
        }
        if (desc.size == 0)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "create_buffer: size must be > 0"));
        }
        GLuint id = 0;
        gl_.glCreateBuffers(1, &id);
        if (id == 0)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "glCreateBuffers returned 0"));
        }
        // Match the Vulkan VMA path: host-visible buffers get persistent-
        // mapped storage flags; GPU-only buffers use immutable storage.
        GLbitfield flags = 0;
        if (desc.memory == cd::rhi::MemoryUsage::kCpuToGpu ||
            desc.memory == cd::rhi::MemoryUsage::kGpuToCpu)
        {
            flags |= kGL_DynamicStorageBit | kGL_MapWriteBit | kGL_MapReadBit;
        }
        gl_.glNamedBufferStorage(id, static_cast<GLsizeiptr>(desc.size), nullptr, flags);
        if (gl_.glGetErrorPtr != nullptr && gl_.glGetErrorPtr() != kGL_NoError)
        {
            gl_.glDeleteBuffersDSA(1, &id);
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "glNamedBufferStorage failed"));
        }
        const auto handle_id = next_id_++;
        buffers_.emplace(handle_id, GLBuffer { id, desc.size });
        return cd::rhi::BufferHandle { handle_id, 1u };
    }
    void destroy_buffer(cd::rhi::BufferHandle h) override
    {
        auto it = buffers_.find(h.index());
        if (it == buffers_.end()) return;
        if (gl_.glDeleteBuffersDSA != nullptr)
        {
            const GLuint id = it->second.gl_id;
            gl_.glDeleteBuffersDSA(1, &id);
        }
        buffers_.erase(it);
    }
    // Phase 143 — texture create / destroy via DSA glCreateTextures +
    // glTextureStorage2D. View creation maps 1:1 to the texture in GL
    // (no separate view object — return the same gl_id).
    [[nodiscard]] cd::core::Result<cd::rhi::TextureHandle>
    create_texture(const cd::rhi::TextureDesc& desc) override
    {
        if (!gl_.texture_valid())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kNotImplemented,
                "OpenGL backend: DSA texture entry points not exported"));
        }
        if (desc.extent.width == 0 || desc.extent.height == 0)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "create_texture: extent must be > 0"));
        }
        // Map Format → GL internal format (subset; expand as needed).
        GLenum gl_ifmt = kGL_RGBA8;
        switch (desc.format)
        {
            case cd::rhi::Format::kRGBA8Unorm:  gl_ifmt = kGL_RGBA8; break;
            case cd::rhi::Format::kBGRA8Unorm:  gl_ifmt = kGL_RGBA8; break;
            case cd::rhi::Format::kR8Unorm:     gl_ifmt = kGL_R8; break;
            case cd::rhi::Format::kD24UnormS8Uint: gl_ifmt = kGL_DEPTH24_STENCIL8; break;
            case cd::rhi::Format::kD32Float:       gl_ifmt = kGL_DEPTH_COMPONENT24; break;
            default:                            gl_ifmt = kGL_RGBA8; break;
        }
        GLuint id = 0;
        gl_.glCreateTextures(kGL_Texture2D, 1, &id);
        if (id == 0)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "glCreateTextures returned 0"));
        }
        gl_.glTextureStorage2D(id,
                               static_cast<GLsizei>(desc.mip_levels),
                               gl_ifmt,
                               static_cast<GLsizei>(desc.extent.width),
                               static_cast<GLsizei>(desc.extent.height));
        if (gl_.glGetErrorPtr != nullptr && gl_.glGetErrorPtr() != kGL_NoError)
        {
            gl_.glDeleteTexturesDSA(1, &id);
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "glTextureStorage2D failed"));
        }
        const auto handle_id = next_id_++;
        textures_.emplace(handle_id, GLTexture { id, desc.extent.width, desc.extent.height });
        return cd::rhi::TextureHandle { handle_id, 1u };
    }
    void destroy_texture(cd::rhi::TextureHandle h) override
    {
        auto it = textures_.find(h.index());
        if (it == textures_.end()) return;
        if (gl_.glDeleteTexturesDSA != nullptr)
        {
            const GLuint id = it->second.gl_id;
            gl_.glDeleteTexturesDSA(1, &id);
        }
        textures_.erase(it);
    }
    // Texture views in OpenGL share the underlying texture object —
    // the "view" abstraction is a no-op accessor over the same id.
    [[nodiscard]] cd::core::Result<cd::rhi::TextureViewHandle>
    create_texture_view(const cd::rhi::TextureViewDesc& desc) override
    {
        auto it = textures_.find(desc.texture.index());
        if (it == textures_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "create_texture_view: unknown texture"));
        }
        const auto handle_id = next_id_++;
        texture_views_.emplace(handle_id, it->second.gl_id);
        return cd::rhi::TextureViewHandle { handle_id, 1u };
    }
    void destroy_texture_view(cd::rhi::TextureViewHandle h) override
    {
        texture_views_.erase(h.index());
    }
    // Phase 144 — sampler create / destroy via DSA glCreateSamplers +
    // glSamplerParameteri. Captures filter mode + wrap.
    [[nodiscard]] cd::core::Result<cd::rhi::SamplerHandle>
    create_sampler(const cd::rhi::SamplerDesc& desc) override
    {
        if (!gl_.sampler_valid())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kNotImplemented,
                "OpenGL backend: DSA sampler entry points not exported"));
        }
        GLuint id = 0;
        gl_.glCreateSamplers(1, &id);
        if (id == 0)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "glCreateSamplers returned 0"));
        }
        auto map_filter = [](cd::rhi::SamplerFilter f) -> GLenum {
            return f == cd::rhi::SamplerFilter::kNearest ? kGL_Nearest : kGL_Linear;
        };
        auto map_wrap = [](cd::rhi::SamplerAddressMode m) -> GLenum {
            switch (m)
            {
                case cd::rhi::SamplerAddressMode::kRepeat:           return kGL_Repeat;
                case cd::rhi::SamplerAddressMode::kMirroredRepeat:   return kGL_MirroredRepeat;
                case cd::rhi::SamplerAddressMode::kClampToEdge:      return kGL_ClampToEdge;
                default:                                             return kGL_ClampToEdge;
            }
        };
        gl_.glSamplerParameteri(id, static_cast<int>(kGL_TextureMinFilter),
            static_cast<int>((desc.mipmap_mode == cd::rhi::SamplerMipmapMode::kLinear)
                ? kGL_LinearMipmapLinear : map_filter(desc.min_filter)));
        gl_.glSamplerParameteri(id, static_cast<int>(kGL_TextureMagFilter),
            static_cast<int>(map_filter(desc.mag_filter)));
        gl_.glSamplerParameteri(id, static_cast<int>(kGL_TextureWrapS),
            static_cast<int>(map_wrap(desc.address_u)));
        gl_.glSamplerParameteri(id, static_cast<int>(kGL_TextureWrapT),
            static_cast<int>(map_wrap(desc.address_v)));
        gl_.glSamplerParameteri(id, static_cast<int>(kGL_TextureWrapR),
            static_cast<int>(map_wrap(desc.address_w)));
        const auto handle_id = next_id_++;
        samplers_.emplace(handle_id, id);
        return cd::rhi::SamplerHandle { handle_id, 1u };
    }
    void destroy_sampler(cd::rhi::SamplerHandle h) override
    {
        auto it = samplers_.find(h.index());
        if (it == samplers_.end()) return;
        if (gl_.glDeleteSamplersDSA != nullptr)
        {
            const GLuint id = it->second;
            gl_.glDeleteSamplersDSA(1, &id);
        }
        samplers_.erase(it);
    }
    // Phase 146 — shader compile via GLSL source. The OpenGL backend
    // expects ShaderModuleDesc::code to point at NULL-terminated GLSL
    // source (Vulkan/D3D12 expect SPIR-V/DXIL bytecode there). Caller
    // chooses based on which backend they're feeding.
    [[nodiscard]] cd::core::Result<cd::rhi::ShaderModuleHandle>
    create_shader_module(const cd::rhi::ShaderModuleDesc& desc) override
    {
        if (!gl_.shader_valid())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kNotImplemented,
                "OpenGL backend: shader entry points not exported"));
        }
        if (desc.code == nullptr || desc.code_size == 0)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "create_shader_module: code / code_size must be set"));
        }
        GLenum gl_stage = kGL_VertexShader;
        switch (desc.stage)
        {
            case cd::rhi::ShaderStage::kVertex:   gl_stage = kGL_VertexShader; break;
            case cd::rhi::ShaderStage::kFragment: gl_stage = kGL_FragmentShader; break;
            case cd::rhi::ShaderStage::kGeometry: gl_stage = kGL_GeometryShader; break;
            case cd::rhi::ShaderStage::kCompute:  gl_stage = kGL_ComputeShader; break;
            default:
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kNotImplemented,
                    "create_shader_module: unsupported shader stage on OpenGL"));
        }
        const GLuint id = gl_.glCreateShader(gl_stage);
        if (id == 0)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "glCreateShader returned 0"));
        }
        const auto* src = static_cast<const GLchar*>(desc.code);
        const int   len = static_cast<int>(desc.code_size);
        gl_.glShaderSource(id, 1, &src, &len);
        gl_.glCompileShader(id);
        int status = 0;
        gl_.glGetShaderiv(id, kGL_CompileStatus, &status);
        if (status == 0)
        {
            int log_len = 0;
            gl_.glGetShaderiv(id, kGL_InfoLogLength, &log_len);
            std::string log(static_cast<std::size_t>(std::max(0, log_len)), '\0');
            if (log_len > 0 && gl_.glGetShaderInfoLog != nullptr)
                gl_.glGetShaderInfoLog(id, log_len, nullptr, log.data());
            gl_.glDeleteShader(id);
            return std::unexpected(cd::rhi::rhi_errors::make_owning(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                std::string { "glCompileShader failed: " } + log));
        }
        const auto handle_id = next_id_++;
        shader_modules_.emplace(handle_id, id);
        return cd::rhi::ShaderModuleHandle { handle_id, 1u };
    }
    void destroy_shader_module(cd::rhi::ShaderModuleHandle h) override
    {
        auto it = shader_modules_.find(h.index());
        if (it == shader_modules_.end()) return;
        if (gl_.glDeleteShader != nullptr) gl_.glDeleteShader(it->second);
        shader_modules_.erase(it);
    }
    [[nodiscard]] cd::core::Result<cd::rhi::DescriptorSetLayoutHandle>
    create_descriptor_set_layout(const cd::rhi::DescriptorSetLayoutDesc&) override { CD_GL_NOT_IMPL_RESULT(DescriptorSetLayoutHandle); }
    void destroy_descriptor_set_layout(cd::rhi::DescriptorSetLayoutHandle) override {}
    // Phase 147 — pipeline layout is a no-op in GL (no PSO object);
    // we return an opaque handle so callers' code is symmetric with
    // Vulkan/D3D12. The handle records nothing useful right now.
    [[nodiscard]] cd::core::Result<cd::rhi::PipelineLayoutHandle>
    create_pipeline_layout(const cd::rhi::PipelineLayoutDesc&) override
    {
        const auto handle_id = next_id_++;
        pipeline_layouts_.emplace(handle_id, 0u);  // no GL counterpart
        return cd::rhi::PipelineLayoutHandle { handle_id, 1u };
    }
    void destroy_pipeline_layout(cd::rhi::PipelineLayoutHandle h) override
    {
        pipeline_layouts_.erase(h.index());
    }
    // Phase 147 — graphics pipeline: link a GL program from the
    // attached shaders + remember the captured raster/depth state.
    [[nodiscard]] cd::core::Result<cd::rhi::GraphicsPipelineHandle>
    create_graphics_pipeline(const cd::rhi::GraphicsPipelineDesc& desc) override
    {
        if (!gl_.program_valid())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kNotImplemented,
                "OpenGL backend: program entry points not exported"));
        }
        const GLuint prog = gl_.glCreateProgram();
        if (prog == 0)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                "glCreateProgram returned 0"));
        }
        auto attach_if = [&](cd::rhi::ShaderModuleHandle h) {
            if (!h.is_valid()) return;
            auto it = shader_modules_.find(h.index());
            if (it != shader_modules_.end()) gl_.glAttachShader(prog, it->second);
        };
        attach_if(desc.vertex_shader);
        attach_if(desc.fragment_shader);
        attach_if(desc.geometry_shader);
        gl_.glLinkProgram(prog);
        int status = 0;
        gl_.glGetProgramiv(prog, kGL_LinkStatus, &status);
        if (status == 0)
        {
            int log_len = 0;
            gl_.glGetProgramiv(prog, kGL_InfoLogLength, &log_len);
            std::string log(static_cast<std::size_t>(std::max(0, log_len)), '\0');
            if (log_len > 0 && gl_.glGetProgramInfoLog != nullptr)
                gl_.glGetProgramInfoLog(prog, log_len, nullptr, log.data());
            gl_.glDeleteProgram(prog);
            return std::unexpected(cd::rhi::rhi_errors::make_owning(
                cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                std::string { "glLinkProgram failed: " } + log));
        }
        const auto handle_id = next_id_++;
        graphics_pipelines_.emplace(handle_id, prog);
        return cd::rhi::GraphicsPipelineHandle { handle_id, 1u };
    }
    void destroy_graphics_pipeline(cd::rhi::GraphicsPipelineHandle h) override
    {
        auto it = graphics_pipelines_.find(h.index());
        if (it == graphics_pipelines_.end()) return;
        if (gl_.glDeleteProgram != nullptr) gl_.glDeleteProgram(it->second);
        graphics_pipelines_.erase(it);
    }
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
    // Phase 167 — OpenGL swapchain. GL has no first-class swapchain
    // object — the OS's GDI back buffer is implicit. We model it as
    // a single-image handle that the caller can acquire/present, with
    // present calling SwapBuffers on the bound HDC.
    [[nodiscard]] cd::core::Result<std::uint32_t>
    acquire_next_image(cd::rhi::SwapchainHandle h,
                       cd::rhi::SemaphoreHandle,
                       cd::rhi::FenceHandle,
                       std::uint64_t) override
    {
        if (swapchains_.find(h.index()) == swapchains_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "acquire_next_image: unknown swapchain"));
        }
        return 0u;  // single-image swapchain
    }
    [[nodiscard]] cd::core::Result<void>
    present(cd::rhi::SwapchainHandle h, std::uint32_t,
            std::span<const cd::rhi::SemaphoreHandle>) override
    {
        auto it = swapchains_.find(h.index());
        if (it == swapchains_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "present: unknown swapchain"));
        }
        if (it->second.hdc != nullptr)
            ::SwapBuffers(static_cast<HDC>(it->second.hdc));
        return {};
    }
    [[nodiscard]] cd::rhi::TextureViewHandle swapchain_image_view(cd::rhi::SwapchainHandle, std::uint32_t) const override { return {}; }
    [[nodiscard]] std::uint32_t swapchain_image_count(cd::rhi::SwapchainHandle h) const override
    {
        return swapchains_.find(h.index()) != swapchains_.end() ? 1u : 0u;
    }
    [[nodiscard]] cd::rhi::TextureHandle swapchain_image(cd::rhi::SwapchainHandle, std::uint32_t) const override { return {}; }
    // Phase 167 — host → device upload via glNamedBufferSubData (DSA).
    [[nodiscard]] cd::core::Result<void>
    upload_buffer(cd::rhi::BufferHandle h, std::uint64_t offset,
                  std::span<const std::byte> data) override
    {
        auto it = buffers_.find(h.index());
        if (it == buffers_.end())
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kInvalidArgument,
                "upload_buffer: unknown buffer"));
        }
        if (gl_.glNamedBufferSubData == nullptr)
        {
            return std::unexpected(cd::rhi::rhi_errors::make(
                cd::rhi::rhi_errors::Code::kNotImplemented,
                "upload_buffer: DSA entry point not exported"));
        }
        gl_.glNamedBufferSubData(it->second.gl_id,
                                 static_cast<GLsizeiptr>(offset),
                                 static_cast<GLsizeiptr>(data.size_bytes()),
                                 data.data());
        return {};
    }
    [[nodiscard]] cd::core::Result<void>
    download_buffer(cd::rhi::BufferHandle, std::uint64_t, std::span<std::byte>) override
    {
        return std::unexpected(cd::rhi::rhi_errors::make(
            cd::rhi::rhi_errors::Code::kNotImplemented, "OpenGL boot-only"));
    }
    [[nodiscard]] cd::core::Result<cd::rhi::SwapchainHandle>
    create_swapchain(const cd::rhi::SwapchainDesc& desc) override
    {
        // Two paths:
        //   (a) Caller passed a real HWND/HINSTANCE → use that window's
        //       HDC for SwapBuffers. The dummy GL context binds to the
        //       new HDC (wglMakeCurrent) for subsequent draws.
        //   (b) Caller passed nullptr → reuse the dummy window the
        //       device initialised with. Useful for headless tests that
        //       still want a present cycle.
        SwapchainRec rec;
        if (desc.window_handle != nullptr)
        {
            HWND hwnd = static_cast<HWND>(desc.window_handle);
            HDC hdc = GetDC(hwnd);
            if (hdc == nullptr)
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                    "create_swapchain: GetDC failed"));
            }
            PIXELFORMATDESCRIPTOR pfd {};
            pfd.nSize = sizeof(pfd);
            pfd.nVersion = 1;
            pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
            pfd.iPixelType = PFD_TYPE_RGBA;
            pfd.cColorBits = 32;
            pfd.cDepthBits = 24;
            pfd.cStencilBits = 8;
            const int pf = ChoosePixelFormat(hdc, &pfd);
            if (pf == 0 || !SetPixelFormat(hdc, pf, &pfd))
            {
                return std::unexpected(cd::rhi::rhi_errors::make(
                    cd::rhi::rhi_errors::Code::kResourceCreationFailed,
                    "create_swapchain: SetPixelFormat failed"));
            }
            // Re-bind the GL context to the new HDC.
            if (hglrc_ != nullptr) wglMakeCurrent(hdc, hglrc_);
            rec.hwnd = hwnd;
            rec.hdc = hdc;
            rec.owns_dc = true;
        }
        else
        {
            rec.hwnd = hwnd_;
            rec.hdc  = hdc_;
            rec.owns_dc = false;
        }
        rec.width  = desc.extent.width;
        rec.height = desc.extent.height;
        const auto id = next_id_++;
        swapchains_.emplace(id, rec);
        return cd::rhi::SwapchainHandle { id, 1u };
    }
    void destroy_swapchain(cd::rhi::SwapchainHandle h) override
    {
        auto it = swapchains_.find(h.index());
        if (it == swapchains_.end()) return;
        if (it->second.owns_dc && it->second.hwnd != nullptr && it->second.hdc != nullptr)
            ReleaseDC(static_cast<HWND>(it->second.hwnd), static_cast<HDC>(it->second.hdc));
        swapchains_.erase(it);
    }
    [[nodiscard]] std::unique_ptr<cd::rhi::ICommandBuffer>
    create_command_buffer(cd::rhi::QueueType) override
    {
        return std::make_unique<GLCommandBuffer>(this, &gl_);
    }
    void submit(cd::rhi::ICommandBuffer& cb) override
    {
        if (auto* gl_cb = dynamic_cast<GLCommandBuffer*>(&cb))
            gl_cb->replay();
    }
    [[nodiscard]] cd::core::Result<void>
    submit(const cd::rhi::SubmitDesc& sd) override
    {
        for (auto* cb : sd.command_buffers)
        {
            if (cb != nullptr)
                if (auto* gl_cb = dynamic_cast<GLCommandBuffer*>(cb))
                    gl_cb->replay();
        }
        return {};
    }

    // Accessors used by GLCommandBuffer for lookups during record.
    [[nodiscard]] GLuint program_of(cd::rhi::GraphicsPipelineHandle h) const noexcept
    {
        auto it = graphics_pipelines_.find(h.index());
        return it == graphics_pipelines_.end() ? 0u : it->second;
    }
    [[nodiscard]] GLuint buffer_id(cd::rhi::BufferHandle h) const noexcept
    {
        auto it = buffers_.find(h.index());
        return it == buffers_.end() ? 0u : it->second.gl_id;
    }
#undef CD_GL_NOT_IMPL_RESULT

private:
    HWND  hwnd_  { nullptr };
    HDC   hdc_   { nullptr };
    HGLRC hglrc_ { nullptr };
    std::string adapter_name_;
    cd::rhi::DeviceLimits   limits_   {};
    cd::rhi::DeviceFeatures features_ {};

    // Phase 137 — DSA buffer plumbing.
    struct GLBuffer
    {
        GLuint        gl_id { 0 };
        std::uint64_t size  { 0 };
    };
    // Phase 143/144 — texture + sampler plumbing.
    struct GLTexture
    {
        GLuint        gl_id { 0 };
        std::uint32_t width { 0 };
        std::uint32_t height { 0 };
    };
    GLLoader gl_ {};
    std::unordered_map<std::uint32_t, GLBuffer>  buffers_;
    std::unordered_map<std::uint32_t, GLTexture> textures_;
    std::unordered_map<std::uint32_t, GLuint>    texture_views_;
    std::unordered_map<std::uint32_t, GLuint>    samplers_;
    // Phase 146/147 — shader + pipeline.
    std::unordered_map<std::uint32_t, GLuint>    shader_modules_;
    std::unordered_map<std::uint32_t, GLuint>    pipeline_layouts_;
    std::unordered_map<std::uint32_t, GLuint>    graphics_pipelines_;
    // Phase 167 — swapchain.
    struct SwapchainRec
    {
        void* hwnd { nullptr };  // HWND
        void* hdc  { nullptr };  // HDC
        std::uint32_t width  { 0 };
        std::uint32_t height { 0 };
        bool owns_dc { false };
    };
    std::unordered_map<std::uint32_t, SwapchainRec> swapchains_;
    std::uint32_t next_id_ { 1 };
};

// ---- GLCommandBuffer out-of-class definitions (depend on OpenGLDevice) ----

void GLCommandBuffer::bind_graphics_pipeline(cd::rhi::GraphicsPipelineHandle h)
{
    const GLuint prog = owner_ ? owner_->program_of(h) : 0;
    cmds_.emplace_back([g_ = gl_, prog]() {
        if (g_->glUseProgram && prog != 0) g_->glUseProgram(prog);
    });
}

void GLCommandBuffer::bind_vertex_buffer(std::uint32_t /*binding*/,
                                         cd::rhi::BufferHandle buffer,
                                         std::uint64_t /*offset*/)
{
    const GLuint id = owner_ ? owner_->buffer_id(buffer) : 0;
    cmds_.emplace_back([g_ = gl_, id]() {
        if (g_->glBindBufferGL && id != 0)
            g_->glBindBufferGL(kGL_ARRAY_BUFFER, id);
    });
}

void GLCommandBuffer::bind_index_buffer(cd::rhi::BufferHandle buffer,
                                        std::uint64_t /*offset*/,
                                        cd::rhi::IndexType type)
{
    index_type_ = type;
    const GLuint id = owner_ ? owner_->buffer_id(buffer) : 0;
    cmds_.emplace_back([g_ = gl_, id]() {
        if (g_->glBindBufferGL && id != 0)
            g_->glBindBufferGL(kGL_ELEMENT_ARRAY_BUFFER, id);
    });
}

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

}  // namespace cd::rhi::opengl
