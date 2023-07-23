#version 450

layout (location = 0) in vec3 in_normal;
layout (location = 1) in vec2 in_uv;

layout (location = 0) out vec4 out_color;

layout (set = 0, binding = 1) uniform sampler2D textures[];

void main()
{
	vec3 color = texture(textures[0], in_uv).xyz;
	out_color = vec4(color, 1.0f);
}
