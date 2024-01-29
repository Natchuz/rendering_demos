#version 450

layout( push_constant ) uniform constants
{
    mat4 pv_matrix;
    vec3 color;
} push_constants;

layout (location = 0) out vec4 out_color;

void main()
{
    out_color = vec4(push_constants.color, 1.0);
}
