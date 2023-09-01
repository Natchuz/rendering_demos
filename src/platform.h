#pragma once

#include <cstdint>
#include <string>
#include <span>
#include <vulkan/vulkan.h>

struct Size
{
	uint32_t width;
	uint32_t height;
};

struct Window_Params
{
	std::string name;
	Size size;
};

class Platform
{
public:
	virtual void poll_events() = 0;

	// Windowing functions
	// window_init and window_destroy will be called _exactly_ once (only one window is supported at the moment)
	virtual void window_init(Window_Params params) = 0;
	virtual void window_destroy() = 0;
	virtual void window_set_name(std::string name) = 0;
	virtual void window_set_size(uint32_t width, uint32_t height) = 0;
	virtual Size window_get_size() = 0;
	virtual bool window_requested_to_close() = 0;

	virtual bool check_presentation_support(VkInstance instance,
											VkPhysicalDevice physical_device,
											uint32_t family_queue) = 0;
	virtual void create_surface(VkInstance instance, VkSurfaceKHR* surface) = 0;
	virtual std::span<const char *> get_required_extensions() = 0; // Return required vulkan extensions

	// ImGui related functions.
	// Temporary, will be replaced by custom, universal implementation on top of abstracted input, windowing, etc.
	virtual void imgui_init() = 0;
	virtual void imgui_shutdown() = 0;
	virtual void imgui_new_frame() = 0;

	// Input related functions
	virtual void fill_input() = 0;
};

inline Platform* platform;