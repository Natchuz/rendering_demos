#version 450

#extension GL_EXT_nonuniform_qualifier : require

struct Global_Data
{
	mat4 pv_matrix;
};

layout (set = 0, binding = 0) uniform Global_Block { Global_Data global_data; };
layout (set = 0, binding = 1) uniform sampler    global_samplers[100];
layout (set = 0, binding = 2) uniform texture2D  global_sampled_textures[5000];

layout (push_constant) uniform Push_Constants
{
	mat4 model_matrix;
	int  texture_id;
} push_constants;

layout (location = 0) in vec3 in_normal;
layout (location = 1) in vec2 in_uv;

layout (location = 0) out vec4 out_color;

void main()
{
	//vec3 color = texture(sampler2D(global_sampled_textures[0], global_samplers[0]), in_uv).xyz;
	//out_color = vec4(color, 1.0f);

	vec4 color = texture(
		sampler2D(global_sampled_textures[nonuniformEXT(push_constants.texture_id)], global_samplers[1]),
		in_uv);
	out_color = vec4(color.xyz, 1.0f);
}
