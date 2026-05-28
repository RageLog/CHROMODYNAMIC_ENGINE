// SPDX: see PrimShader.hpp banner.
#pragma once

namespace cd::hello_engine
{

inline constexpr const char* kShadowVS = R"glsl(
#version 450
layout(push_constant) uniform PC { mat4 light_mvp; } pc;
layout(location = 0) in vec3 in_pos;
void main() { gl_Position = pc.light_mvp * vec4(in_pos, 1.0); }
)glsl";

inline constexpr const char* kShadowFS = R"glsl(
#version 450
void main() {}
)glsl";

}  // namespace cd::hello_engine
