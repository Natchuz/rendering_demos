#version 450

layout( push_constant ) uniform constants
{
    mat4 pv_matrix;
    vec3 color;
} push_constants;

layout (location = 0) in vec3 in_position;

void main()
{
    gl_Position = push_constants.pv_matrix * vec4(in_position, 1.0f);
}
