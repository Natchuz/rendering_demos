#version 450

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec3 in_color;

layout (set = 0, binding = 0) uniform FrameData {
	float color;
} frame_data;

layout (location = 0) out vec3 out_color;

void main() {
	out_color = in_color * vec3(frame_data.color);
	gl_Position = vec4(in_position, 1.0f);
}
