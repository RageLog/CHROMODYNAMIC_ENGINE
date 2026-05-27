// =============================================================================
// CHROMODYNAMIC — samples/hello_opengl_boot
//
// v0.49.0 / Phase 18.B — OpenGL backend boot smoke test. Mirrors
// hello_d3d12_boot's v0.27.0 milestone: prove the cd::rhi_opengl
// factory creates a real GL 4.6 context + reads back the adapter
// name. Resource / draw paths land in follow-up waves.
// =============================================================================
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Version.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi_opengl/OpenGLDevice.hpp>

#include <cstdio>

int main()
{
    std::fprintf(stdout,
                 "CHROMODYNAMIC %u.%u.%u — hello_opengl_boot\n",
                 static_cast<unsigned>(cd::core::kEngineVersion.major),
                 static_cast<unsigned>(cd::core::kEngineVersion.minor),
                 static_cast<unsigned>(cd::core::kEngineVersion.patch));

    cd::rhi_opengl::GLCreateInfo info {};
    info.app_name = "hello_opengl_boot";
    info.min_version = 30;   // GL 3.0 minimum so old laptops still pass
    info.enable_validation = false;
    auto dev_r = cd::rhi_opengl::create_gl_device(info);
    if (!dev_r.has_value())
    {
        std::fprintf(stderr,
                     "[gl] create_gl_device failed: %.*s\n",
                     static_cast<int>(dev_r.error().message.size()),
                     dev_r.error().message.data());
        return 1;
    }
    auto& device = **dev_r;

    const auto adapter = device.adapter_name();
    std::fprintf(stdout, "[gl] backend=%u  adapter=%.*s\n",
                 static_cast<unsigned>(device.backend()),
                 static_cast<int>(adapter.size()),
                 adapter.data());
    device.wait_idle();
    std::fprintf(stdout, "[gl] boot OK\n");
    return 0;
}
