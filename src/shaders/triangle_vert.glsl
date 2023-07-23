#version 450

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec3 in_normal;
layout (location = 2) in vec2 in_uv;

layout (set = 0, binding = 0) uniform FrameData {
	mat4x4 render_matix;
} frame_data;

layout (location = 0) out vec3 out_normal;
layout (location = 1) out vec2 out_uv;

void main() {
	out_normal = in_normal;
	out_uv = in_uv;

	gl_Position = frame_data.render_matix * vec4(in_position, 1.0f);
}
