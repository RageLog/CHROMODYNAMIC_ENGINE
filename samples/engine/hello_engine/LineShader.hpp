// =============================================================================
// LineShader.hpp
// -----------------------------------------------------------------------------
// phase1031 — embedded GLSL fallback for the cd::debug_line GPU path.
// AUTO-SYNCED with samples/engine/hello_engine/shaders/line.{vert,frag}.glsl;
// the on-disk copies win at runtime per the ADR-20260529-X5 precedence
// rule (_glsl_path > _glsl). Keep in lockstep with the .glsl files.
// =============================================================================
#pragma once

namespace cd::hello_engine
{

inline constexpr const char* kLineVS = R"glsl(
#version 460
layout(push_constant) uniform PC {
  mat4 view_proj;
} pc;

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec4 in_color;

layout(location = 0) out vec4 v_color;

void main() {
  v_color = in_color;
  gl_Position = pc.view_proj * vec4(in_pos, 1.0);
}
)glsl";

inline constexpr const char* kLineFS = R"glsl(
#version 460
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
)glsl";

}  // namespace cd::hello_engine
