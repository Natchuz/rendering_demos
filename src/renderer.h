#pragma once

#include "gfx_context.h"

#include <deque>
#include <stb_image.h>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

// This one is intended for managing bindless sampled textures/images
struct Texture_Manager
{
	struct Sampled_Image
	{
		AllocatedImage image;
		VkImageView    view;
		VkSampler      sampler;
	};
	typedef uint32_t Texture_Handle;

	struct Upload_Data
	{
		stbi_uc*      pixels;
		VkExtent2D    image_size;
		Texture_Handle image_handle;
	};

	std::vector<Sampled_Image> bindings;
	std::deque<Upload_Data>    upload_queue;
};

inline Texture_Manager* texture_manager;

void texture_manager_init();
void texture_manager_deinit();

// Objects "owned by frame" for double or triple buffering
struct Frame_Data
{
	VkCommandPool   command_pool;
	VkCommandBuffer upload_command_buffer;
	VkCommandBuffer draw_command_buffer;

	VkSemaphore acquire_semaphore; // Swapchain image_handle acquire event
	VkSemaphore render_semaphore;  // Same as fence below. Presentation event waits on it
	VkFence     render_fence;      // Will be signaled once all rendering operations for this frame are done
	VkSemaphore upload_semaphore;  // Signal finishing of upload operation
	VkFence     upload_fence;      // Same as above

	AllocatedBuffer staging_buffer; // Staging buffer that will update other buffers with changed data
	void*           staging_buffer_ptr;
};

struct Frame_Uniform_Data
{
	glm::mat4x4 render_matrix;
};

enum Buffering_Type : uint32_t
{
	DOUBLE = 2,
	TRIPLE = 3,
};

struct Renderer
{
	Buffering_Type          buffering;
	std::vector<Frame_Data> frame_data;

	AllocatedBuffer vertex_buffer;
	void*           vertex_buffer_ptr;
	uint32_t        vertices_count;

	AllocatedBuffer per_frame_data_buffer;

	Allocated_View_Image depth_buffer;

	VkDescriptorPool      descriptor_pool;
	VkDescriptorSet       per_frame_descriptor_set;
	VkDescriptorSetLayout per_frame_descriptor_set_layout;

	VkShaderModule vertex_shader;
	VkShaderModule fragment_shader;

	VkPipelineLayout pipeline_layout;
	VkPipeline       pipeline;
};

inline Renderer* renderer;

void renderer_init();
void renderer_deinit();
void renderer_dispatch();

// Recreate all resources that are dependent on size of swapchain, e.g. depth buffer or gbuffers.
// Call this after swapchain recreation.
void recreate_swapchain_dependent_resources();

void depth_buffer_create();
void depth_buffer_destroy();

void renderer_create_frame_data();
void renderer_destroy_frame_data();

void renderer_create_buffers();
void renderer_destroy_buffers();