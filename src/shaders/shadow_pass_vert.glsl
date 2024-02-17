#version 450

layout( push_constant ) uniform constants
{
    mat4 model_matrix;
    mat4 light_space_matrix;
} push_constants;

layout (location = 0) in vec3 in_position;

void main()
{
    gl_Position = push_constants.light_space_matrix * push_constants.model_matrix * vec4(in_position, 1.0f);
}