#version 450

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_debug_printf : require

struct Directional_Light
{
	vec3  direction;
	float intensity;
};

struct Point_Light
{
	vec3  position;
	float intensity;
	float radius;
};

struct Global_Data
{
	mat4              pv_matrix;
	Directional_Light sun_data;
	uint              active_lights;
	Point_Light       point_lights[16];
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
layout (location = 2) in vec3 in_world_position;

layout (location = 0) out vec4 out_color;

void main()
{
	PBR_Material material = materials[push_constants.material_id];

	vec4 albedo_color = texture(
		sampler2D(global_sampled_textures[material.albedo_texture], global_samplers[material.albedo_sampler]),
		in_uv) * material.albedo_color;

	float attenuation = 0;

	attenuation += global_data.sun_data.intensity
		* clamp(dot(in_normal, global_data.sun_data.direction), 0.0f, 1.0f);

	for (uint i = 0; i < global_data.active_lights; i++)
	{
		Point_Light light = global_data.point_lights[i];

		vec3  lv = light.position - in_world_position;
		vec3  l  = normalize(lv);
		float r  = length(lv);

		float r_min = 0.01;
		float r0    = light.radius;

		float f_win = pow(clamp(1 - pow(r / 100 , 4), 0, 1), 2);

		attenuation += pow(r0 / max(r, r_min), 2) * f_win * light.intensity * clamp(dot(in_normal, l), 0, 1);
	}

	vec4 color = albedo_color * clamp(attenuation, 0, 1);
	out_color = vec4(color.xyz, 1.0f);
}
