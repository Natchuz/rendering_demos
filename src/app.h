#pragma once

#include<vector>
#include<span>

#include<vulkan/vulkan.h>
#include<GLFW/glfw3.h>
#include<vk_mem_alloc.h>

auto init_global_libs() -> bool;
auto deinit_global_libs() -> void;

struct AllocatedBuffer {
	VkBuffer buffer;
	VmaAllocation allocation;
};

class App
{
public:
	static App *ptr;

	auto init() -> bool;
	auto deinit() -> void;
	auto run() -> void;

private:
	GLFWwindow *glfw_window;
	std::span<const char *> required_glfw_extensions;

	VkInstance instance;
	uint32_t instance_version;

	VkPhysicalDevice physical_device;
	VkDevice device;
	VkQueue gfx_queue;
	uint32_t gfx_queue_family_index;

	VmaAllocator vma_allocator;

	VkSurfaceKHR surface;

	VkSwapchainKHR swapchain;
	VkFormat swapchain_image_format;
	uint32_t swapchain_images_count;
	std::vector<VkImage> swapchain_images;
	std::vector<VkImageView> swapchain_image_views;

	VkCommandPool command_pool;
	std::vector<VkCommandBuffer> command_buffers;

	AllocatedBuffer vertex_buffer;
	void* vertex_buffer_data;

	VkSemaphore render_semaphore[2], present_semaphore[2];
	VkFence render_fence[2];
	
	VkShaderModule vertex_shader;
	VkShaderModule fragment_shader;

	VkExtent2D window_extent;

	VkPipelineLayout pipeline_layout;
	VkPipeline pipeline;

	bool is_running = false;
	uint32_t frame_index;

	auto init_vulkan() -> bool;
	auto create_instance() -> void;
	auto create_device() -> void;
    auto create_allocator() -> void;
	auto create_surface() -> void;
	auto create_swapchain(bool recreate = false) -> void;
	auto create_command_buffers() -> void;
	auto create_buffers() -> void;
	auto upload_vertex_data() -> void;
	auto create_sync_objects() -> void;
	auto create_shaders() -> void;
	auto create_pipeline() -> void;

	auto draw(uint32_t frame_index) -> void;
};