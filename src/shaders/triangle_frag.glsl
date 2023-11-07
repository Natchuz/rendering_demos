#version 450

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_debug_printf : require

struct Global_Data
{
	mat4 pv_matrix;
};

struct PBR_Material
{
	vec4      albedo_color;
	uint      albedo_texture;
	uint      albedo_sampler;
	float     metalness_factor;
	float     roughness_factor;
	uint      metal_roughness_texture;
	uint      metal_roughness_sampler;
};

layout (set = 0, binding = 0) uniform Global_Block { Global_Data global_data; };
layout (set = 0, binding = 1) uniform sampler    global_samplers[100];
layout (set = 0, binding = 2) uniform texture2D  global_sampled_textures[5000];
layout (std430, set = 0, binding = 3) buffer  Material_Data { PBR_Material materials[]; };

layout (push_constant) uniform Push_Constants
{
	mat4 model_matrix;
	uint material_id;
} push_constants;

layout (location = 0) in vec3 in_normal;
layout (location = 1) in vec2 in_uv;

layout (location = 0) out vec4 out_color;

void main()
{
	PBR_Material material = materials[push_constants.material_id];

	vec4 color = texture(
		sampler2D(global_sampled_textures[material.albedo_texture], global_samplers[material.albedo_sampler]),
		in_uv) * material.albedo_color;
	out_color = vec4(color.xyz, 1.0f);
}
