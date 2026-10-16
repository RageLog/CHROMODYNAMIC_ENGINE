#version 460
// phase1031 — cd::debug_line GPU path, vertex stage.
// Minimal pos+color passthrough for kLineList debug geometry. The
// single mat4 push constant is the camera view-projection (lines are
// authored in world space by cd::debug_line::LineBatch, so no model
// matrix is needed).
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
