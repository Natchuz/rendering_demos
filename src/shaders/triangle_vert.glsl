#version 450

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec3 in_color;

layout (set = 0, binding = 0) uniform FrameData {
	mat4x4 render_matix;
} frame_data;

layout (location = 0) out vec3 out_color;

void main() {
	out_color = in_color;
	gl_Position = frame_data.render_matix * vec4(in_position, 1.0f);
}
