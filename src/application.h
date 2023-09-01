#pragma once

#include "platform.h"

#include <chrono>
#include <vulkan/vulkan.h>
//#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/matrix.hpp>

struct Camera
{
	glm::vec3 position;
	glm::vec3 front, up, right;
	float yaw, pitch;
	float velocity;
	float speed_multiplier;

	float mouse_sensitivity;
};

inline Camera* camera;

void camera_init();
void camera_deinit();
void camera_update();
glm::mat4x4 camera_get_view_matrix();

struct ImGui_Data
{
	VkDescriptorPool descriptor_pool;
};

inline ImGui_Data* imgui_data;

void imgui_init();
void imgui_deinit();
void imgui_new_frame();

struct Timings
{
	std::chrono::high_resolution_clock::time_point frame_time_stamp;
	float delta_time;

	bool zero_delta; // Set to make next delta_time = 0 (like first frame)
};

inline Timings* timings;

void timings_new_frame();

struct Application
{
	float rotation = 0;

	uint64_t frame_number;
};

inline Application* app;

void application_entry(Platform* p_platform);