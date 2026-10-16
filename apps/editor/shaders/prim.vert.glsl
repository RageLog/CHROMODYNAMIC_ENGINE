#version 450
layout(push_constant) uniform PC {
  mat4 mvp;
  mat4 model;            // world-space transform of this entity
  vec4 tint;
  vec4 sun_dir;          // xyz=directional dir, w=intensity
  vec4 sun_color;        // xyz=color, w=ambient
  vec4 fx_params;        // x=tonemap_op (0=Nark, 1=Hill, 2=Hable, 3=AGX)
  vec4 fx_params2;       // x=smaa, y=motion_blur, z=taa, w=dof (v1.4+)
  vec4 fx_params3;       // x=fog, y=atmosphere, z=clouds, w=light_shafts
  vec4 camera_pos;       // xyz=world camera (atmospherics distance)
  vec4 fx_params4;       // x=clearcoat, y=sheen, z=sss, w=reserved
} pc;
// Faz 1.6 CSM - light-space view-projection for shadow sampling.
// Set 0 / binding 0 is a per-frame UBO updated each draw cycle by
// the host with the current sun's ortho VP. Binding 1 (sampler) is
// declared in the fragment shader.
layout(set = 0, binding = 0) uniform Shadow {
  mat4 light_vp;
} cd_shadow;
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec3 in_color;
layout(location = 0) out vec3 v_world_pos;
layout(location = 1) out vec3 v_world_normal;
layout(location = 2) out vec3 v_albedo;
layout(location = 3) out vec4 v_shadow_pos;
layout(location = 4) out vec2 v_uv;
void main() {
  v_albedo = in_color * pc.tint.rgb;
  v_uv     = in_uv;
  vec4 wp = pc.model * vec4(in_pos, 1.0);
  v_world_pos = wp.xyz;
  // Inverse-transpose-of-model would be more correct for non-uniform
  // scale; for the sample we use model directly (scales are uniform).
  v_world_normal = normalize((pc.model * vec4(in_normal, 0.0)).xyz);
  // Shadow-space position. light_vp is set up so x,y in [-1,1] and
  // z in [0,1] for fragments inside the shadow ortho frustum. Vulkan
  // sample-side flips y to match texture v-down, done in the FS.
  v_shadow_pos = cd_shadow.light_vp * vec4(v_world_pos, 1.0);
  vec4 clip = pc.mvp * vec4(in_pos, 1.0);
  clip.y = -clip.y;
  gl_Position = clip;
}
