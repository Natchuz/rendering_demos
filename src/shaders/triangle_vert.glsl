#version 450

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec3 in_normal;
layout (location = 2) in vec4 in_tangent;
layout (location = 3) in vec2 in_uv;

layout( push_constant ) uniform constants
{
	mat4 model_matrix;
} push_constants;

layout (set = 0, binding = 0) uniform FrameData {
	mat4x4 pv_matrix;
} frame_data;

layout (location = 0) out vec3 out_normal;
layout (location = 1) out vec2 out_uv;

void main() {
	out_normal = in_normal;
	out_uv     = in_uv;

	gl_Position = frame_data.pv_matrix * push_constants.model_matrix * vec4(in_position, 1.0f);
}
