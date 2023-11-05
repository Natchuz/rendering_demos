#pragma once

#include "gfx_context.h"

#include <map>
#include <deque>
#include <stb_image.h>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

/// Helper class
struct Mapped_Buffer_Writer
{
	uint8_t* base_ptr;
	uint8_t* offset_ptr;

	explicit Mapped_Buffer_Writer(void* mapped_buffer_ptr);

	[[nodiscard]] size_t offset() const;

	void advance(size_t size);
	void align_next(size_t alignment); // Align cursor

	size_t write(const void* data, size_t size); // Returns offset at which data is copied
};

void flush_buffer_writer(Mapped_Buffer_Writer& writer, VmaAllocator vma_allocator, VmaAllocation vma_allocation);

// Meshes are stored in interleaved format, all in one buffer
// TODO-FUTURE: separate position and properties stream (faster z rendering).
struct Mesh_Manager
{
	typedef uint32_t Id;

	// TODO should we separate these into two objects? (we only need indices for rendering - better cache utilization)
	struct Mesh_Description
	{
		VkDeviceSize vertex_offset;
		uint32_t     vertex_count;
		VkDeviceSize indices_offset;
		uint32_t     indices_count;
		VmaVirtualAllocation vertex_allocation;
		VmaVirtualAllocation indices_allocation;
	};

	struct Mesh_Upload
	{
		Id       id;
		void*    ptr;
		uint32_t size;
	};

	AllocatedBuffer vertex_buffer;
	AllocatedBuffer indices_buffer;
	VmaVirtualBlock vertex_sub_allocator;
	VmaVirtualBlock indices_sub_allocator;

	Id next_index;
	std::map<Id, Mesh_Description> meshes;

	inline Mesh_Description& get_mesh(Id id) { return meshes[id]; }
};

inline Mesh_Manager* mesh_manager;

struct Render_Object
{
	Mesh_Manager::Id mesh_id;
	glm::mat4        transform;
};

struct Scene_Data
{
	std::vector<Render_Object> render_objects;
};

inline Scene_Data* scene_data;

struct Texture_Manager
{
	// FIXME Both of these should actually _not_ be textures, but rather an slot allocator.
	std::vector<VkSampler>            samplers;
	std::vector<Allocated_View_Image> images;
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

	AllocatedBuffer per_frame_data_buffer;

	Allocated_View_Image depth_buffer;

	VkDescriptorPool      descriptor_pool;
	VkDescriptorSet       per_frame_descriptor_set;
	VkDescriptorSetLayout per_frame_descriptor_set_layout;

	VkShaderModule vertex_shader;
	VkShaderModule fragment_shader;

	VkPipelineLayout pipeline_layout;
	VkPipeline       pipeline;

	// Major stages of rendering a frame are controlled by timeline semaphores with value of frame number
	// adjusted to avoid having to account for first few frames
	VkSemaphore  upload_semaphore;
	VkSemaphore  render_semaphore;

	// Command pool associated with main upload heap - all operations on main upload heap are device-blocking,
	// so we can create command buffers on per-upload basic and reset it at once. This will be changed in future
	// by adding proper streaming support.
	VkCommandPool   upload_command_pool;
	VkCommandBuffer upload_command_buffer;
	AllocatedBuffer main_upload_heap;
	void*           main_upload_heap_ptr;
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