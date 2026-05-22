// =============================================================================
// CHROMODYNAMIC — samples/hello_skybox
//
// Analytical procedural sky. No cubemap texture upload — the entire sky
// is generated in the fragment shader from view direction. Reconstructs
// the world-space ray per fragment by inverting the view-projection,
// then evaluates a 3-band atmospheric gradient + a sun disk.
//
// The same trick (a fullscreen triangle covering NDC, fragment shader
// computes the ray) is the simplest "skybox" pattern that hits all the
// PBR-engine bases:
//   * proves the renderer can do depth-less full-screen passes
//   * provides an ambient term other shaders could sample (`ambient =
//     sample_sky(world_normal)`) without a cubemap upload pipeline yet
//   * IBL extension path: bake this analytical sky into a cubemap +
//     pre-filter for split-sum IBL when needed (Phase 5).
//
// Output: sky-blue gradient with a warm sun glow; orbiting view.
// =============================================================================
#include "SampleRuntime.hpp"

#include <cd/camera/Camera.hpp>
#include <cd/material/Material.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Vector.hpp>
#include <cd/platform/Window.hpp>
#include <cd/render/Renderer.hpp>
#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi_vulkan/VulkanDevice.hpp>
#include <cd/shader/Compiler.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

namespace
{

// Push-constant block: camera basis vectors so the fragment shader can
// reconstruct world rays without a matrix inverse. half_w / half_h
// pre-baked from fov+aspect to save a tangent per fragment.
//   0   vec4 cam_right (xyz=right basis vector, w=half_w = tan(fov/2)*aspect)
//   16  vec4 cam_up    (xyz=up    basis vector, w=half_h = tan(fov/2))
//   32  vec4 cam_fwd   (xyz=forward (toward -target+eye), w=_)
//   48  vec4 sun_dir   (xyz=normalized direction, w=intensity)
struct PushBlock
{
    std::array<float, 4> cam_right;
    std::array<float, 4> cam_up;
    std::array<float, 4> cam_fwd;
    std::array<float, 4> sun_dir;
};

static_assert(sizeof(PushBlock) == 64, "PushBlock must equal 64 B");

constexpr const char* kVS = R"glsl(
#version 450
// Fullscreen triangle without vertex buffer: derive clip-space position
// from gl_VertexIndex. Three vertices cover NDC after rasterization
// clipping — no vertex buffer bound at draw time.
layout(location = 0) out vec2 v_ndc;
void main() {
  // Magic constants: 0 → (-1,-1), 1 → (3,-1), 2 → (-1,3).
  vec2 p = vec2((gl_VertexIndex == 1) ? 3.0 : -1.0,
                (gl_VertexIndex == 2) ? 3.0 : -1.0);
  v_ndc = p;
  gl_Position = vec4(p, 1.0, 1.0);  // z=1 → pin to back of depth range.
}
)glsl";

constexpr const char* kFS = R"glsl(
#version 450
layout(push_constant) uniform PC {
  vec4 cam_right;  // xyz=right basis, w=half_w = tan(fov/2)*aspect
  vec4 cam_up;     // xyz=up    basis, w=half_h = tan(fov/2)
  vec4 cam_fwd;    // xyz=forward (toward target), w=_
  vec4 sun_dir;    // xyz=normalized direction, w=intensity
} pc;
layout(location = 0) in  vec2 v_ndc;
layout(location = 0) out vec4 out_color;

// World-space ray direction from NDC. Closed-form for a pinhole camera:
//   ray = normalize(forward + ndc.x * half_w * right + ndc.y * half_h * up)
// Vulkan NDC is Y-down so we flip the Y term to keep "+Y = up in world".
vec3 ray_dir(vec2 ndc) {
  vec3 forward = pc.cam_fwd.xyz;
  vec3 right   = pc.cam_right.xyz;
  vec3 up      = pc.cam_up.xyz;
  return normalize(forward
                 + ndc.x * pc.cam_right.w * right
                 - ndc.y * pc.cam_up.w    * up);
}

void main() {
  vec3 dir = ray_dir(v_ndc);

  // 3-band atmosphere: zenith blue → horizon haze → ground.
  float h = dir.y;  // -1..1
  vec3 zenith  = vec3(0.18, 0.42, 0.85);
  vec3 horizon = vec3(0.78, 0.90, 1.00);
  vec3 ground  = vec3(0.08, 0.07, 0.10);
  vec3 sky;
  if (h >= 0.0) {
    float t = pow(h, 0.6);
    sky = mix(horizon, zenith, t);
  } else {
    float t = pow(-h, 0.5);
    sky = mix(horizon, ground, t);
  }

  // Sun disk + glow.
  vec3 L = normalize(-pc.sun_dir.xyz);
  float cos_a = clamp(dot(dir, L), 0.0, 1.0);
  // Sharp disk inside 2° (cos ≈ 0.9994) → soft glow out to 30°.
  float disk = smoothstep(0.9994, 0.9998, cos_a);
  float glow = pow(cos_a, 64.0);
  vec3 sun_color = vec3(1.0, 0.92, 0.78) * pc.sun_dir.w;
  vec3 result = sky + sun_color * (disk * 8.0 + glow * 0.6);

  // Reinhard + gamma.
  result = result / (result + vec3(1.0));
  result = pow(result, vec3(1.0 / 2.2));
  out_color = vec4(result, 1.0);
}
)glsl";

}  // namespace

int main(int argc, char** argv)
{
    const cd::sample::Runtime runtime = cd::sample::parse_runtime(argc, argv);

    cd::platform::WindowDesc wd {};
    wd.title = "CHROMODYNAMIC — hello_skybox (procedural atmosphere)";
    wd.width = 1280;
    wd.height = 720;
    auto window_r = cd::platform::create_window(wd);
    if (!window_r.has_value())
        return 1;
    auto& window = **window_r;

    cd::rhi_vulkan::VulkanCreateInfo vci {};
    auto device_r = cd::rhi_vulkan::create_vulkan_device(vci);
    if (!device_r.has_value())
        return 2;
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
        return 3;
    auto& renderer = *renderer_r;

    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr)
        return 4;

    constexpr std::array<cd::rhi::Format, 1> kColorFormats { cd::rhi::Format::kBGRA8Unorm };
    constexpr std::array<cd::rhi::PushConstantRange, 1> kPush {
        cd::rhi::PushConstantRange { .stages = cd::rhi::ShaderStage::kVertex
                                                | cd::rhi::ShaderStage::kFragment,
                                    .offset = 0,
                                    .size = static_cast<std::uint32_t>(sizeof(PushBlock)) }
    };
    cd::material::MaterialDesc md {};
    md.vertex_glsl = kVS;
    md.fragment_glsl = kFS;
    md.color_attachment_formats = kColorFormats;
    md.push_constants = kPush;
    md.depth_stencil.depth_test = false;
    md.depth_stencil.depth_write = false;
    md.name = "hello_skybox";
    auto mat_r = cd::material::Material::create(device, compiler.get(), md);
    if (!mat_r.has_value())
        return 5;
    auto& material = *mat_r;

    std::printf("hello_skybox: ready. Analytical atmosphere via fullscreen fragment. ESC to exit.\n");
    std::fflush(stdout);

    std::vector<cd::platform::OSEvent> events;
    events.reserve(64);
    bool needs_rebuild = false;
    float orbit_angle = 0.0F;
    std::uint32_t frame_idx = 0;

    while (true)
    {
        if (!runtime.should_continue(frame_idx))
            window.request_close();
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
        if (needs_rebuild)
        {
            if (window.width() == 0 || window.height() == 0)
                continue;
            if (!renderer.recreate_swapchain({ window.width(), window.height() }).has_value())
                continue;
            needs_rebuild = false;
        }
        if (!runtime.no_spin)
            orbit_angle += 0.003F;

        auto frame_r = renderer.begin_frame();
        if (!frame_r.has_value())
        {
            if (frame_r.error().code ==
                static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            {
                needs_rebuild = true;
                continue;
            }
            return 6;
        }
        auto& frame = *frame_r;
        auto& cmd = *frame.command_buffer;

        std::array<cd::rhi::ColorAttachmentInfo, 1> color_attach {
            cd::rhi::ColorAttachmentInfo {
                                          .view = frame.swapchain_image_view,
                                          .load_op = cd::rhi::LoadOp::kClear,
                                          .store_op = cd::rhi::StoreOp::kStore,
                                          .clear_color = { .f32 = { 0.0F, 0.0F, 0.0F, 1.0F } } }
        };
        cd::rhi::RenderPassBeginInfo rp {};
        rp.render_area = cd::rhi::Rect2D {
            { 0, 0 },
            frame.extent
        };
        rp.color_attachments = color_attach;
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
        material.apply(cmd);

        cd::camera::Camera cam {};
        cam.eye = { 8.0F * std::sin(orbit_angle), 2.0F, 8.0F * std::cos(orbit_angle) };
        cam.target = { 0.0F, 0.0F, 0.0F };
        cam.fov_y = 1.0F;
        cam.near_z = 0.1F;
        cam.far_z = 100.0F;
        const float aspect = static_cast<float>(frame.extent.width) / static_cast<float>(frame.extent.height);

        // Camera basis. forward = (target - eye) normalized. right =
        // normalize(forward × world_up). up = right × forward.
        cd::math::Vec3f forward {
            cam.target.x - cam.eye.x,
            cam.target.y - cam.eye.y,
            cam.target.z - cam.eye.z,
        };
        const float fl = std::sqrt(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
        forward.x /= fl;
        forward.y /= fl;
        forward.z /= fl;
        constexpr cd::math::Vec3f world_up { 0.0F, 1.0F, 0.0F };
        cd::math::Vec3f right { forward.y * world_up.z - forward.z * world_up.y,
                                forward.z * world_up.x - forward.x * world_up.z,
                                forward.x * world_up.y - forward.y * world_up.x };
        const float rl = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
        right.x /= rl;
        right.y /= rl;
        right.z /= rl;
        cd::math::Vec3f up { right.y * forward.z - right.z * forward.y,
                             right.z * forward.x - right.x * forward.z,
                             right.x * forward.y - right.y * forward.x };
        const float half_h = std::tan(cam.fov_y * 0.5F);
        const float half_w = half_h * aspect;

        PushBlock pb {};
        pb.cam_right = { right.x, right.y, right.z, half_w };
        pb.cam_up = { up.x, up.y, up.z, half_h };
        pb.cam_fwd = { forward.x, forward.y, forward.z, 0.0F };
        pb.sun_dir = { -0.4F, -0.7F, -0.6F, 3.5F };

        cmd.push_constants(material.pipeline_layout(),
                           cd::rhi::ShaderStage::kVertex | cd::rhi::ShaderStage::kFragment,
                           /*offset=*/0,
                           static_cast<std::uint32_t>(sizeof(pb)),
                           &pb);
        // Fullscreen triangle: 3 vertices, no vertex buffer.
        cmd.draw(3, 1, 0, 0);

        cmd.end_render_pass();

        auto end_r = renderer.end_frame();
        if (!end_r.has_value())
        {
            if (end_r.error().code ==
                static_cast<std::uint32_t>(cd::render::render_errors::Code::kSwapchainOutOfDate))
            {
                needs_rebuild = true;
                continue;
            }
            return 7;
        }
        ++frame_idx;
    }

    renderer.wait_idle();
    std::printf("hello_skybox: clean exit (%u frames).\n", frame_idx);
    return 0;
}
