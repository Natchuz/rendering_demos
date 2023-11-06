#version 450

layout (set = 0, binding = 0) uniform Global_Data
{
	mat4 pv_matrix;
} global_data;
layout (set = 0, binding = 1) uniform sampler     global_samplers[100];
layout (set = 0, binding = 2) uniform texture2D   global_sampled_textures[5000];

layout( push_constant ) uniform constants
{
	mat4 model_matrix;
	int  texture_id;
} push_constants;

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec3 in_normal;
layout (location = 2) in vec4 in_tangent;
layout (location = 3) in vec2 in_uv;

layout (location = 0) out vec3 out_normal;
layout (location = 1) out vec2 out_uv;

void main()
{
	out_normal = in_normal;
	out_uv     = in_uv;

	gl_Position = global_data.pv_matrix * push_constants.model_matrix * vec4(in_position, 1.0f);
}
