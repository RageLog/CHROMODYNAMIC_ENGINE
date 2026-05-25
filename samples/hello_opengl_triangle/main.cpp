// =============================================================================
// CHROMODYNAMIC — samples/hello_opengl_triangle
//
// Phase 167 / v0.99.88 — OpenGL swapchain + a real triangle on screen.
//
// Boots cd::rhi_opengl, creates a platform window, binds the engine's
// swapchain to it (Phase 167's new path), and draws a triangle each
// frame for 120 frames. The draw call uses raw GL (no command-buffer
// abstraction yet — that's v1.3+); cd::rhi handles device boot +
// buffer + shader + pipeline + swapchain present.
// =============================================================================
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#endif

#include <cd/core/Version.hpp>
#include <cd/platform/Window.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/Pipeline.hpp>
#include <cd/rhi_opengl/OpenGLDevice.hpp>

#include <cstdio>
#include <cstring>

#if defined(_WIN32)

namespace
{

// GL 2.0+ entry points we need for the actual draw. wglGetProcAddress.
using GLenum     = unsigned int;
using GLuint     = unsigned int;
using GLint      = int;
using GLsizei    = int;
using GLsizeiptr = long long;
using GLbitfield = unsigned int;
using GLfloat    = float;
using GLboolean  = unsigned char;

constexpr GLenum kGL_COLOR_BUFFER_BIT  = 0x4000;
constexpr GLenum kGL_TRIANGLES         = 0x0004;
constexpr GLenum kGL_ARRAY_BUFFER      = 0x8892;
constexpr GLenum kGL_STATIC_DRAW       = 0x88E4;
constexpr GLenum kGL_FLOAT             = 0x1406;

using PFNGLCLEARCOLORPROC          = void (*)(GLfloat, GLfloat, GLfloat, GLfloat);
using PFNGLCLEARPROC               = void (*)(GLbitfield);
using PFNGLVIEWPORTPROC            = void (*)(GLint, GLint, GLsizei, GLsizei);
using PFNGLGENVERTEXARRAYSPROC     = void (*)(GLsizei, GLuint*);
using PFNGLBINDVERTEXARRAYPROC     = void (*)(GLuint);
using PFNGLGENBUFFERSPROC          = void (*)(GLsizei, GLuint*);
using PFNGLBINDBUFFERPROC          = void (*)(GLenum, GLuint);
using PFNGLBUFFERDATAPROC          = void (*)(GLenum, GLsizeiptr, const void*, GLenum);
using PFNGLVERTEXATTRIBPOINTERPROC = void (*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
using PFNGLENABLEVERTEXATTRIBARRAYPROC = void (*)(GLuint);
using PFNGLUSEPROGRAMPROC          = void (*)(GLuint);
using PFNGLDRAWARRAYSPROC          = void (*)(GLenum, GLint, GLsizei);
using PFNGLCREATEPROGRAMPROC       = GLuint (*)();
using PFNGLCREATESHADERPROC        = GLuint (*)(GLenum);
using PFNGLSHADERSOURCEPROC        = void (*)(GLuint, GLsizei, const char* const*, const int*);
using PFNGLCOMPILESHADERPROC       = void (*)(GLuint);
using PFNGLATTACHSHADERPROC        = void (*)(GLuint, GLuint);
using PFNGLLINKPROGRAMPROC         = void (*)(GLuint);

constexpr GLenum kGL_VERTEX_SHADER   = 0x8B31;
constexpr GLenum kGL_FRAGMENT_SHADER = 0x8B30;

struct GL
{
    PFNGLCLEARCOLORPROC          glClearColor;
    PFNGLCLEARPROC               glClear;
    PFNGLVIEWPORTPROC            glViewport;
    PFNGLGENVERTEXARRAYSPROC     glGenVertexArrays;
    PFNGLBINDVERTEXARRAYPROC     glBindVertexArray;
    PFNGLGENBUFFERSPROC          glGenBuffers;
    PFNGLBINDBUFFERPROC          glBindBuffer;
    PFNGLBUFFERDATAPROC          glBufferData;
    PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer;
    PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray;
    PFNGLUSEPROGRAMPROC          glUseProgram;
    PFNGLDRAWARRAYSPROC          glDrawArrays;
    PFNGLCREATEPROGRAMPROC       glCreateProgram;
    PFNGLCREATESHADERPROC        glCreateShader;
    PFNGLSHADERSOURCEPROC        glShaderSource;
    PFNGLCOMPILESHADERPROC       glCompileShader;
    PFNGLATTACHSHADERPROC        glAttachShader;
    PFNGLLINKPROGRAMPROC         glLinkProgram;
};

[[nodiscard]] GL load_gl()
{
    GL g {};
    HMODULE dll = GetModuleHandleW(L"opengl32.dll");
    auto get = [dll](const char* name) -> PROC {
        PROC p = wglGetProcAddress(name);
        const auto v = reinterpret_cast<intptr_t>(p);
        if (v == 0 || v == 1 || v == 2 || v == 3 || v == -1) p = nullptr;
        if (p == nullptr && dll != nullptr) p = GetProcAddress(dll, name);
        return p;
    };
    g.glClearColor              = reinterpret_cast<PFNGLCLEARCOLORPROC>(get("glClearColor"));
    g.glClear                   = reinterpret_cast<PFNGLCLEARPROC>(get("glClear"));
    g.glViewport                = reinterpret_cast<PFNGLVIEWPORTPROC>(get("glViewport"));
    g.glGenVertexArrays         = reinterpret_cast<PFNGLGENVERTEXARRAYSPROC>(get("glGenVertexArrays"));
    g.glBindVertexArray         = reinterpret_cast<PFNGLBINDVERTEXARRAYPROC>(get("glBindVertexArray"));
    g.glGenBuffers              = reinterpret_cast<PFNGLGENBUFFERSPROC>(get("glGenBuffers"));
    g.glBindBuffer              = reinterpret_cast<PFNGLBINDBUFFERPROC>(get("glBindBuffer"));
    g.glBufferData              = reinterpret_cast<PFNGLBUFFERDATAPROC>(get("glBufferData"));
    g.glVertexAttribPointer     = reinterpret_cast<PFNGLVERTEXATTRIBPOINTERPROC>(get("glVertexAttribPointer"));
    g.glEnableVertexAttribArray = reinterpret_cast<PFNGLENABLEVERTEXATTRIBARRAYPROC>(get("glEnableVertexAttribArray"));
    g.glUseProgram              = reinterpret_cast<PFNGLUSEPROGRAMPROC>(get("glUseProgram"));
    g.glDrawArrays              = reinterpret_cast<PFNGLDRAWARRAYSPROC>(get("glDrawArrays"));
    g.glCreateProgram           = reinterpret_cast<PFNGLCREATEPROGRAMPROC>(get("glCreateProgram"));
    g.glCreateShader            = reinterpret_cast<PFNGLCREATESHADERPROC>(get("glCreateShader"));
    g.glShaderSource            = reinterpret_cast<PFNGLSHADERSOURCEPROC>(get("glShaderSource"));
    g.glCompileShader           = reinterpret_cast<PFNGLCOMPILESHADERPROC>(get("glCompileShader"));
    g.glAttachShader            = reinterpret_cast<PFNGLATTACHSHADERPROC>(get("glAttachShader"));
    g.glLinkProgram             = reinterpret_cast<PFNGLLINKPROGRAMPROC>(get("glLinkProgram"));
    return g;
}

constexpr const char* kVs = R"glsl(
#version 330 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec3 a_col;
out vec3 v_col;
void main() {
  v_col = a_col;
  gl_Position = vec4(a_pos, 0.0, 1.0);
}
)glsl";

constexpr const char* kFs = R"glsl(
#version 330 core
in  vec3 v_col;
out vec4 frag;
void main() { frag = vec4(v_col, 1.0); }
)glsl";

[[nodiscard]] GLuint compile_and_link(const GL& gl)
{
    GLuint vs = gl.glCreateShader(kGL_VERTEX_SHADER);
    GLuint fs = gl.glCreateShader(kGL_FRAGMENT_SHADER);
    const int vs_len = static_cast<int>(std::strlen(kVs));
    const int fs_len = static_cast<int>(std::strlen(kFs));
    const char* vs_src = kVs;
    const char* fs_src = kFs;
    gl.glShaderSource(vs, 1, &vs_src, &vs_len);
    gl.glCompileShader(vs);
    gl.glShaderSource(fs, 1, &fs_src, &fs_len);
    gl.glCompileShader(fs);
    GLuint prog = gl.glCreateProgram();
    gl.glAttachShader(prog, vs);
    gl.glAttachShader(prog, fs);
    gl.glLinkProgram(prog);
    return prog;
}

}  // namespace

#endif  // _WIN32

int main()
{
    std::printf("CHROMODYNAMIC %u.%u.%u - hello_opengl_triangle\n",
                static_cast<unsigned>(cd::core::kEngineVersion.major),
                static_cast<unsigned>(cd::core::kEngineVersion.minor),
                static_cast<unsigned>(cd::core::kEngineVersion.patch));

#if !defined(_WIN32)
    std::printf("[gl] Windows-only sample; this build is non-Windows.\n");
    return 0;
#else
    // ---- 1. Create platform window ----
    cd::platform::WindowDesc wd {};
    wd.width  = 640;
    wd.height = 480;
    wd.title  = "hello_opengl_triangle";
    auto win_r = cd::platform::create_window(wd);
    if (!win_r.has_value())
    {
        std::fprintf(stderr, "[gl] create_window failed\n");
        return 1;
    }
    auto& win = **win_r;

    // ---- 2. Boot GL device with NO dummy window so we can target the real one ----
    cd::rhi_opengl::GLCreateInfo info {};
    info.app_name = "hello_opengl_triangle";
    info.min_version = 30;
    info.enable_validation = false;
    info.create_dummy_window = true;  // device uses its dummy for initial probing
    auto dev_r = cd::rhi_opengl::create_gl_device(info);
    if (!dev_r.has_value())
    {
        std::fprintf(stderr, "[gl] create_gl_device failed\n");
        return 2;
    }
    auto& device = **dev_r;
    std::printf("[gl] adapter: %.*s\n",
                static_cast<int>(device.adapter_name().size()),
                device.adapter_name().data());

    // ---- 3. Bind the engine swapchain to the real window ----
    cd::rhi::SwapchainDesc sd {};
    sd.window_handle = win.native_window_handle();
    sd.display_handle = nullptr;
    sd.extent = { 640, 480 };
    sd.image_count = 1;
    sd.format = cd::rhi::Format::kBGRA8Unorm;
    auto sc_r = device.create_swapchain(sd);
    if (!sc_r.has_value())
    {
        std::fprintf(stderr, "[gl] create_swapchain failed: %.*s\n",
                     static_cast<int>(sc_r.error().message.size()),
                     sc_r.error().message.data());
        return 3;
    }
    auto sc = *sc_r;
    std::printf("[gl] swapchain bound to native HWND\n");

    // ---- 4. Load raw GL function pointers for the draw call ----
    GL gl = load_gl();
    if (gl.glClear == nullptr || gl.glDrawArrays == nullptr ||
        gl.glGenVertexArrays == nullptr || gl.glLinkProgram == nullptr)
    {
        std::fprintf(stderr, "[gl] missing GL 3.0+ entry points\n");
        return 4;
    }

    // ---- 5. VAO + VBO + shader for the triangle ----
    GLuint vao = 0;
    gl.glGenVertexArrays(1, &vao);
    gl.glBindVertexArray(vao);

    constexpr float verts[] = {
        // x,    y,    r,    g,    b
         0.0F,  0.6F, 1.0F, 0.25F, 0.20F,  // top — red
        -0.6F, -0.5F, 0.20F, 1.0F, 0.30F,  // left — green
         0.6F, -0.5F, 0.25F, 0.40F, 1.0F,  // right — blue
    };
    GLuint vbo = 0;
    gl.glGenBuffers(1, &vbo);
    gl.glBindBuffer(kGL_ARRAY_BUFFER, vbo);
    gl.glBufferData(kGL_ARRAY_BUFFER, sizeof(verts), verts, kGL_STATIC_DRAW);
    gl.glEnableVertexAttribArray(0);
    gl.glEnableVertexAttribArray(1);
    gl.glVertexAttribPointer(0, 2, kGL_FLOAT, 0, 5 * sizeof(float), nullptr);
    gl.glVertexAttribPointer(1, 3, kGL_FLOAT, 0, 5 * sizeof(float),
                             reinterpret_cast<const void*>(2 * sizeof(float)));

    GLuint prog = compile_and_link(gl);
    gl.glUseProgram(prog);

    // ---- 6. Frame loop ----
    gl.glViewport(0, 0, 640, 480);
    std::printf("[gl] entering frame loop (120 frames)...\n");
    for (int frame = 0; frame < 120; ++frame)
    {
        std::vector<cd::platform::OSEvent> events;
        (void)win.pump_events(events);
        for (const auto& e : events)
        {
            if (e.kind == cd::platform::OSEventKind::kClose) frame = 999;
        }

        auto img = device.acquire_next_image(sc, {}, {}, 0);
        if (!img.has_value()) break;

        // Animated background.
        const float t = static_cast<float>(frame) * 0.03F;
        gl.glClearColor(0.05F + 0.05F * t, 0.07F, 0.12F, 1.0F);
        gl.glClear(kGL_COLOR_BUFFER_BIT);
        gl.glDrawArrays(kGL_TRIANGLES, 0, 3);

        (void)device.present(sc, *img, {});
        Sleep(16);
    }

    device.destroy_swapchain(sc);
    std::printf("[gl] hello_opengl_triangle done — visible OpenGL triangle drawn.\n");
    return 0;
#endif
}
