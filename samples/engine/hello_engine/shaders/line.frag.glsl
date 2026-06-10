#version 460
// phase1031 — cd::debug_line GPU path, fragment stage.
// Writes the per-vertex colour into the HDR target and NEUTRAL
// values into the other three G-buffer attachments so the post
// stack stays well-defined on line pixels:
//   out_normal = 0      -> zero-length normal; SSR/AO treat as
//                          invalid and skip the pixel;
//   out_albedo = colour -> debug view mode 1 (albedo) shows lines;
//   out_mr     = (0, 1) -> non-metallic, fully rough; kills any
//                          specular response on the line pixels.
layout(location = 0) in vec4 v_color;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_normal;
layout(location = 2) out vec4 out_albedo;
layout(location = 3) out vec2 out_mr;

void main() {
  out_color  = vec4(v_color.rgb, v_color.a);
  out_normal = vec4(0.0);
  out_albedo = vec4(v_color.rgb, 1.0);
  out_mr     = vec2(0.0, 1.0);
}
