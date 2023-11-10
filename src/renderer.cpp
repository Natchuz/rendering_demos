#include "renderer.h"

#include "common.h"
#include "application.h"
#include "vulkan_utilities.h"

#include <fstream>
#include <fastgltf/parser.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <unordered_map>
#include <volk.h>
#include <vulkan/vulkan_core.h>

// Private functions
void load_scene_data();
std::vector<uint8_t> load_file(const char* file_path);

void renderer_create_global_uniforms();
void renderer_create_shaders();
void renderer_destroy_shaders();
void renderer_create_pipeline();
void renderer_destroy_pipeline();
void renderer_create_sync_primitives();
void renderer_destroy_sync_primitives();
void renderer_create_upload_heap();
void renderer_destroy_upload_heap();

Mapped_Buffer_Writer::Mapped_Buffer_Writer(void* mapped_buffer_ptr)
{
	this->base_ptr   = static_cast<uint8_t*>(mapped_buffer_ptr);
	this->offset_ptr = base_ptr;
}

size_t Mapped_Buffer_Writer::write(const void* data, size_t size)
{
	size_t start_offset = offset();
	memcpy(offset_ptr, data, size);
	advance(size);
	return start_offset;
}

[[nodiscard]] size_t Mapped_Buffer_Writer::offset() const
{
	return offset_ptr - base_ptr;
}

void Mapped_Buffer_Writer::advance(size_t size)
{
	offset_ptr = offset_ptr + size;
}

void Mapped_Buffer_Writer::align_next(size_t alignment) {
	size_t to_advance = 0;
	if (alignment > 0)
	{
		to_advance = ((offset() + alignment - 1) & ~(alignment - 1)) - offset();
	}
	advance(to_advance);
}

void flush_buffer_writer(Mapped_Buffer_Writer& writer, VmaAllocator vma_allocator, VmaAllocation vma_allocation)
{
	vmaFlushAllocation(gfx_context->vma_allocator, renderer->main_upload_heap.allocation, 0,
					   writer.offset());
}

void mesh_manager_init()
{
	ZoneScopedN("Mesh manager initialization");

	mesh_manager = new Mesh_Manager{};

	// Vertex buffer
	{
		VkBufferCreateInfo creation_info = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size  = 500000000,
			.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		};

		VmaAllocationCreateInfo vma_creation_info = {
			.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
			.usage = VMA_MEMORY_USAGE_AUTO,
		};

		vmaCreateBuffer(gfx_context->vma_allocator,
						&creation_info,
						&vma_creation_info,
						&mesh_manager->vertex_buffer.buffer,
						&mesh_manager->vertex_buffer.allocation,
						nullptr);
		name_object(mesh_manager->vertex_buffer.buffer, "Vertex buffer");
	}

	// Indices buffer
	{
		VkBufferCreateInfo creation_info = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size  = 100000000,
			.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		};

		VmaAllocationCreateInfo vma_creation_info = {
			.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
			.usage = VMA_MEMORY_USAGE_AUTO,
		};

		vmaCreateBuffer(gfx_context->vma_allocator,
						&creation_info,
						&vma_creation_info,
						&mesh_manager->indices_buffer.buffer,
						&mesh_manager->indices_buffer.allocation,
						nullptr);
		name_object(mesh_manager->indices_buffer.buffer, "Indices buffer");
	}

	// Vertex buffer suballocation
	{
		VmaVirtualBlockCreateInfo create_info = { .size  = 500000000, .flags = 0 };
		vmaCreateVirtualBlock(&create_info, &mesh_manager->vertex_sub_allocator);
	}

	// Indices buffer suballocation
	{
		VmaVirtualBlockCreateInfo create_info = { .size  = 100000000, .flags = 0 };
		vmaCreateVirtualBlock(&create_info, &mesh_manager->indices_sub_allocator);
	}
}

void mesh_manager_deinit()
{
	vmaDestroyBuffer(gfx_context->vma_allocator, mesh_manager->vertex_buffer.buffer,
					 mesh_manager->vertex_buffer.allocation);
	vmaDestroyBuffer(gfx_context->vma_allocator, mesh_manager->indices_buffer.buffer,
					 mesh_manager->indices_buffer.allocation);

	delete mesh_manager;
}

void texture_manager_init()
{
	ZoneScopedN("Texture manager initialization");

	texture_manager = new Texture_Manager{};

	// Create default sampler
	VkSamplerCreateInfo default_sampler_create_info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter        = VK_FILTER_LINEAR,
		.minFilter        = VK_FILTER_LINEAR,
		.mipmapMode       = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		.addressModeU     = VK_SAMPLER_ADDRESS_MODE_REPEAT, // GLTF Specs require this,
		.addressModeV     = VK_SAMPLER_ADDRESS_MODE_REPEAT, // and this.
		.addressModeW     = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.mipLodBias       = 0,
		.anisotropyEnable = true,
		.maxAnisotropy    = 16,
		.compareEnable    = false,
		.compareOp        = VK_COMPARE_OP_NEVER,
		.minLod           = 0,
		.maxLod           = 10,
		.borderColor      = VK_BORDER_COLOR_INT_OPAQUE_WHITE,
		.unnormalizedCoordinates = false,
	};
	VkSampler default_sampler;
	vkCreateSampler(gfx_context->device, &default_sampler_create_info, nullptr, &default_sampler);
	texture_manager->samplers.push_back(default_sampler);

	// Create default texture
	Allocated_View_Image view_image; // What we will be allocating

	VkImageCreateInfo image_create_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType     = VK_IMAGE_TYPE_2D,
		.format        = VK_FORMAT_R8G8B8A8_SRGB,
		.extent        = { 1, 1, 1},
		.mipLevels     = 1,
		.arrayLayers   = 1,
		.samples       = VK_SAMPLE_COUNT_1_BIT,
		.tiling        = VK_IMAGE_TILING_OPTIMAL,
		.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		.sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	VmaAllocationCreateInfo allocation_create_info = { .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, };

	VmaAllocationInfo allocation_info;
	vmaCreateImage(gfx_context->vma_allocator, &image_create_info,
				   &allocation_create_info,&view_image.image,
				   &view_image.allocation, &allocation_info);
	name_object(view_image.image, "Default texture");

	// Create default image view
	create_default_image_view(gfx_context->device, image_create_info, view_image.image, nullptr, &view_image.view);
	name_object(view_image.view, "Default texture's view");

	texture_manager->images.push_back(view_image);
}

void texture_manager_deinit()
{
	delete texture_manager;
}

void material_manager_init()
{
	material_manager = new Material_Manager{};
	material_manager->materials.reserve(1000);

	// Allocate material buffer
	VkBufferCreateInfo buffer_create_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size  = 40000, // 40 kb = sizeof(PBR_MATERIAL) * 1000
		.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	};

	VmaAllocationCreateInfo vma_buffer_create_info = {
		.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
	};

	vmaCreateBuffer(gfx_context->vma_allocator,
					&buffer_create_info,
					&vma_buffer_create_info,
					&material_manager->material_storage_buffer.buffer,
					&material_manager->material_storage_buffer.allocation,
					nullptr);
	name_object(material_manager->material_storage_buffer.buffer, "Material storage buffer");

	// Default material
	material_manager->materials.push_back({
		.albedo_color            = { 0.0f, 0.0f, 0.0f, 1.0f},
		.albedo_texture          = Texture_Manager::DEFAULT_TEXTURE,
		.albedo_sampler          = Texture_Manager::DEFAULT_SAMPLER,
		.metalness_factor        = 1.0f,
		.roughness_factor        = 1.0f,
		.metal_roughness_texture = Texture_Manager::DEFAULT_TEXTURE,
		.metal_roughness_sampler = Texture_Manager::DEFAULT_SAMPLER,
	});
}

void material_manager_deinit()
{
	delete material_manager;
}

void renderer_init()
{
	renderer = new Renderer;

	scene_data = new Scene_Data;
	mesh_manager_init();
	texture_manager_init();
	material_manager_init();
	depth_buffer_create();

	renderer->descriptor_set_allocator = {};

	renderer_create_frame_data();
	renderer_create_global_uniforms();
	renderer_create_shaders();
	renderer_create_pipeline();
	renderer_create_sync_primitives();
	renderer_create_upload_heap();
	load_scene_data();
}

void renderer_deinit()
{
	vkDeviceWaitIdle(gfx_context->device);

	renderer_destroy_upload_heap();
	renderer_destroy_sync_primitives();
	renderer_destroy_pipeline();
	renderer_destroy_shaders();
	renderer_destroy_frame_data();
	depth_buffer_destroy();
	texture_manager_deinit();
	mesh_manager_deinit();
	delete scene_data;

	delete renderer;
}

void recreate_swapchain_dependent_resources()
{
	ZoneScopedN("Recreation of swapchain-dependent resources");

	depth_buffer_destroy();
	depth_buffer_create();
}

void depth_buffer_create()
{
	ZoneScopedN("Depth buffer creation");

	VkImageCreateInfo image_create_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.flags         = 0,
		.imageType     = VK_IMAGE_TYPE_2D,
		.format        = VK_FORMAT_D32_SFLOAT,
		.extent        = { gfx_context->swapchain.extent.width, gfx_context->swapchain.extent.height, 1 },
		.mipLevels     = 1,
		.arrayLayers   = 1,
		.samples       = VK_SAMPLE_COUNT_1_BIT,
		.tiling        = VK_IMAGE_TILING_OPTIMAL,
		.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		.sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	VmaAllocationCreateInfo vma_allocation_info = {
		.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
	};

	vmaCreateImage(gfx_context->vma_allocator, &image_create_info, &vma_allocation_info,
				   &renderer->depth_buffer.image, &renderer->depth_buffer.allocation,
				   nullptr);

	VkImageViewCreateInfo image_view_create_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image      = renderer->depth_buffer.image,
		.viewType   = VK_IMAGE_VIEW_TYPE_2D,
		.format     = VK_FORMAT_D32_SFLOAT,
		.components = {
			.r = VK_COMPONENT_SWIZZLE_IDENTITY,
			.g = VK_COMPONENT_SWIZZLE_IDENTITY,
			.b = VK_COMPONENT_SWIZZLE_IDENTITY,
			.a = VK_COMPONENT_SWIZZLE_IDENTITY,
		},
		.subresourceRange = {
			.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT,
			.baseMipLevel   = 0,
			.levelCount     = 1,
			.baseArrayLayer = 0,
			.layerCount     = 1,
		},
	};

	vkCreateImageView(gfx_context->device, &image_view_create_info, nullptr, &renderer->depth_buffer.view);
}

void depth_buffer_destroy()
{
	ZoneScopedN("Depth buffer destruction");

	vkDestroyImageView(gfx_context->device, renderer->depth_buffer.view, nullptr);
	vmaDestroyImage(gfx_context->vma_allocator, renderer->depth_buffer.image, renderer->depth_buffer.allocation);
}

void renderer_create_frame_data()
{
	ZoneScopedN("Frame data creation");

	renderer->buffering  = TRIPLE;
	renderer->frame_data = std::vector<Frame_Data>(renderer->buffering);

	// Command pools
	{
		ZoneScopedN("Command pools creation");

		VkCommandPoolCreateInfo command_pool_create_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = gfx_context->gfx_queue_family_index,
		};

		for (int frame_i=0; frame_i < renderer->buffering; frame_i++)
		{
			vkCreateCommandPool(gfx_context->device, &command_pool_create_info, nullptr,
								&renderer->frame_data[frame_i].command_pool);
			name_object(renderer->frame_data[frame_i].command_pool,"Main command pool (frame {})", frame_i);
		}
	}

	// Command buffers
	{
		ZoneScopedN("Command buffers allocation");

		for (int frame_i=0; frame_i < renderer->buffering; frame_i++)
		{
			auto frame_data = &renderer->frame_data[frame_i]; // Shortcut

			VkCommandBufferAllocateInfo allocate_info = {
				.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
				.commandPool        = frame_data->command_pool,
				.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
				.commandBufferCount = 1,
			};

			vkAllocateCommandBuffers(gfx_context->device, &allocate_info, &frame_data->upload_command_buffer);
			vkAllocateCommandBuffers(gfx_context->device, &allocate_info, &frame_data->draw_command_buffer);
			name_object(frame_data->upload_command_buffer,"Upload command buffer (frame {})", frame_i);
			name_object(frame_data->draw_command_buffer,  "Draw command buffer (frame {})",   frame_i);
		}
	}

	// Synchronization primitives
	{
		ZoneScopedN("Synchronization primitives creation");

		VkSemaphoreCreateInfo semaphore_create_info = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		};

		VkFenceCreateInfo fence_create_info = {
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
			.flags = VK_FENCE_CREATE_SIGNALED_BIT,
		};

		for (int frame_i=0; frame_i < renderer->buffering; frame_i++)
		{
			auto frame_data = &renderer->frame_data[frame_i]; // Shortcut

			vkCreateSemaphore(gfx_context->device, &semaphore_create_info, nullptr, &frame_data->acquire_semaphore);
			name_object(frame_data->acquire_semaphore, "Present semaphore (frame {})", frame_i);
		}
	}

	// Staging buffer
	{
		ZoneScopedN("Staging buffer allocation");

		VkBufferCreateInfo staging_buffer_create_info = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = 30000000, // 30 mb
			.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		};

		VmaAllocationCreateInfo staging_buffer_vma_create_info = {
			.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
			.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
		};

		for (int frame_i=0; frame_i < renderer->buffering; frame_i++)
		{
			auto frame_data = &renderer->frame_data[frame_i]; // Shortcut

			VmaAllocationInfo allocation_info;
			vmaCreateBuffer(gfx_context->vma_allocator,
							&staging_buffer_create_info,
							&staging_buffer_vma_create_info,
							&frame_data->staging_buffer.buffer,
							&frame_data->staging_buffer.allocation,
							&allocation_info);
			frame_data->staging_buffer_ptr = allocation_info.pMappedData;
			name_object(frame_data->staging_buffer.buffer,"Staging buffer (frame {})", frame_i);
		}
	}
}

void renderer_destroy_frame_data()
{
	ZoneScopedN("Frame data destruction");

	// Staging buffer
	for (int frame_i=0; frame_i < renderer->buffering; frame_i++)
	{
		vmaDestroyBuffer(gfx_context->vma_allocator, renderer->frame_data[frame_i].staging_buffer.buffer,
						 renderer->frame_data[frame_i].staging_buffer.allocation);
	}

	// Synchronization primitives
	for (int frame_i=0; frame_i < renderer->buffering; frame_i++)
	{
		vkDestroySemaphore(gfx_context->device, renderer->frame_data[frame_i].acquire_semaphore, nullptr);
	}

	// Command buffers
	for (int frame_i=0; frame_i < renderer->buffering; frame_i++)
	{
		auto frame_data = &renderer->frame_data[frame_i]; // Shortcut

		VkCommandBuffer buffers[] = { frame_data->upload_command_buffer,frame_data->draw_command_buffer };
		vkFreeCommandBuffers(gfx_context->device, frame_data->command_pool, 2, buffers);
	}

	// Command pools
	for (int frame_i=0; frame_i < renderer->buffering; frame_i++)
	{
		vkDestroyCommandPool(gfx_context->device, renderer->frame_data[frame_i].command_pool, nullptr);
	}
}

void renderer_create_global_uniforms()
{
	ZoneScopedN("Global uniforms creation");

	{
		ZoneScopedN("Global descriptors");

		VkDescriptorSetLayoutBinding bindings[] = {
			{ // Global uniform data
				.binding         = 0,
				.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				.descriptorCount = 1,
				.stageFlags      = VK_SHADER_STAGE_ALL,
			},
			{ // 2D Samplers
				.binding         = 1,
				.descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLER,
				.descriptorCount = 100, // Maximum 100 samplers
				.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
			{ // 2D Textures
				.binding         = 2,
				.descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
				.descriptorCount = 5000, // Maximum 5000 textures
				.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
			{ // Material buffer
				.binding         = 3,
				.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1,
				.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
			}
		};

		VkDescriptorBindingFlags flags[] = {
			0,
			VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
			VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
			0,
		};

		VkDescriptorSetLayoutBindingFlagsCreateInfo flags_create_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
			.bindingCount  = 4,
			.pBindingFlags = flags,
		};

		VkDescriptorSetLayoutCreateInfo create_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.pNext = &flags_create_info,
			.flags        = 0,
			.bindingCount = 4,
			.pBindings    = bindings,
		};

		vkCreateDescriptorSetLayout(gfx_context->device, &create_info, nullptr,
									&renderer->global_data_descriptor_set_layout);
		name_object(renderer->global_data_descriptor_set_layout, "Global data descriptor layout");

		renderer->descriptor_set_allocator.allocate(gfx_context->device, renderer->global_data_descriptor_set_layout,
													&renderer->global_data_descriptor_set);
		name_object(renderer->global_data_descriptor_set, "Global data descriptor");
	}

	// Uniform buffer
	{
		ZoneScopedN("Global uniform buffer creation");

		auto size = clamp_size_to_alignment(
			sizeof(Global_Uniform_Data),
			gfx_context->physical_device_properties.properties.limits.minUniformBufferOffsetAlignment);
		size *= renderer->buffering;

		VkBufferCreateInfo buffer_create_info = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size  = size,
			.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		};

		VmaAllocationCreateInfo vma_buffer_create_info = {
			.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
		};

		vmaCreateBuffer(gfx_context->vma_allocator,
						&buffer_create_info,
						&vma_buffer_create_info,
						&renderer->global_uniform_data_buffer.buffer,
						&renderer->global_uniform_data_buffer.allocation,
						nullptr);
		name_object(renderer->global_uniform_data_buffer.buffer, "Global data uniform buffer");
	}
}

std::vector<uint8_t> load_file(const char* file_path)
{
	std::ifstream file(file_path, std::ios::ate | std::ios::binary);

	if (!file.is_open())
	{
		throw std::runtime_error("Shader file open!");
	}

	size_t file_size = (size_t) file.tellg();
	std::vector<uint8_t> buffer(file_size);

	file.seekg(0);
	file.read((char*) buffer.data(), file_size);
	file.close();

	return buffer;
}

void renderer_create_shaders()
{
	ZoneScopedN("Shader creation");

	// If Spir-V shader is valid, casting bytes to 32-bit words shouldn't matter
	auto vert_shader_code = load_file("data/shaders/triangle_vert.spv");
	VkShaderModuleCreateInfo vert_shader_create_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = vert_shader_code.size(),
		.pCode    = reinterpret_cast<const uint32_t *>(vert_shader_code.data()),
	};
	vkCreateShaderModule(gfx_context->device, &vert_shader_create_info, nullptr, &renderer->vertex_shader);
	name_object(renderer->vertex_shader, "Vertex shader");

	auto frag_shader_code = load_file("data/shaders/triangle_frag.spv");
	VkShaderModuleCreateInfo frag_shader_create_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = frag_shader_code.size(),
		.pCode = reinterpret_cast<const uint32_t *>(frag_shader_code.data()),
	};
	vkCreateShaderModule(gfx_context->device, &frag_shader_create_info, nullptr, &renderer->fragment_shader);
	name_object(renderer->fragment_shader, "Fragment shader");
}

void renderer_destroy_shaders()
{
	ZoneScopedN("Shader destruction");
}

void renderer_create_pipeline()
{
	ZoneScopedN("Pipeline creation");

	// Pipeline layout
	{
		ZoneScopedN("Pipeline layout creation");

		VkDescriptorSetLayout set_layouts[] = {renderer->global_data_descriptor_set_layout };

		VkPushConstantRange push_constant_range = {
			.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
			.offset     = 0,
			.size       = 16 * sizeof(float) + 4,
		};

		VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.flags = 0,
			.setLayoutCount         = 1,
			.pSetLayouts            = set_layouts,
			.pushConstantRangeCount = 1,
			.pPushConstantRanges    = &push_constant_range,
		};
		vkCreatePipelineLayout(gfx_context->device, &pipeline_layout_create_info, nullptr, &renderer->pipeline_layout);
		name_object(renderer->pipeline_layout, "Pipeline layout");
	}

	// Pipeline
	{
		VkPipelineShaderStageCreateInfo vert_stage = {
			.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage  = VK_SHADER_STAGE_VERTEX_BIT,
			.module = renderer->vertex_shader,
			.pName  = "main",
		};
		VkPipelineShaderStageCreateInfo frag_stage = {
			.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = renderer->fragment_shader,
			.pName  = "main",
		};
		VkPipelineShaderStageCreateInfo stages[2] = {vert_stage, frag_stage};

		VkVertexInputBindingDescription binding_description = {
			.binding   = 0,
			.stride    = 12 * sizeof(float),
			.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
		};

		VkVertexInputAttributeDescription vertex_attributes[] = {
			{ // Position attribute
				.location = 0,
				.binding  = 0,
				.format   = VK_FORMAT_R32G32B32_SFLOAT,
				.offset   = 0,
			},
			{ // Normal attribute
				.location = 1,
				.binding  = 0,
				.format   = VK_FORMAT_R32G32B32_SFLOAT,
				.offset   = 3 * sizeof(float),
			},
			{ // Tangents attribute
				.location = 2,
				.binding  = 0,
				.format   = VK_FORMAT_R32G32B32A32_SFLOAT,
				.offset   = 6 * sizeof(float),
			},
			{ // UV attribute
				.location = 3,
				.binding  = 0,
				.format   = VK_FORMAT_R32G32_SFLOAT,
				.offset   = 10 * sizeof(float),
			},
		};

		VkPipelineVertexInputStateCreateInfo vertex_input_state = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
			.vertexBindingDescriptionCount   = 1,
			.pVertexBindingDescriptions      = &binding_description,
			.vertexAttributeDescriptionCount = 4,
			.pVertexAttributeDescriptions    = vertex_attributes,
		};

		VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
			.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		};

		VkPipelineRasterizationStateCreateInfo rasterization_state = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
			.depthClampEnable        = VK_FALSE,
			.rasterizerDiscardEnable = VK_FALSE,
			.polygonMode             = VK_POLYGON_MODE_FILL,
			.cullMode                = VK_CULL_MODE_NONE,
			.frontFace               = VK_FRONT_FACE_CLOCKWISE,
			.depthBiasEnable         = VK_FALSE,
			.depthBiasConstantFactor = 0.0f,
			.depthBiasClamp          = 0.0f,
			.depthBiasSlopeFactor    = 0.0f,
			.lineWidth               = 1.0f,
		};

		VkPipelineMultisampleStateCreateInfo multisample_state = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
			.rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT,
			.sampleShadingEnable   = VK_FALSE,
			.minSampleShading      = 1.0f,
			.pSampleMask           = nullptr,
			.alphaToCoverageEnable = VK_FALSE,
			.alphaToOneEnable      = VK_FALSE,
		};

		VkPipelineViewportStateCreateInfo viewport_state = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
			.viewportCount = 1,
			.scissorCount  = 1,
		};

		VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
		VkPipelineDynamicStateCreateInfo dynamic_state = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
			.dynamicStateCount = 2,
			.pDynamicStates    = dynamic_states,
		};

		VkPipelineColorBlendAttachmentState color_blend_attachment_state = {
			.blendEnable    = VK_FALSE,
			.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
		};

		VkPipelineColorBlendStateCreateInfo color_blend_state = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
			.logicOpEnable   = VK_FALSE,
			.logicOp         = VK_LOGIC_OP_COPY,
			.attachmentCount = 1,
			.pAttachments    = &color_blend_attachment_state,
		};

		VkPipelineRenderingCreateInfo pipeline_rendering_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
			.colorAttachmentCount    = 1,
			.pColorAttachmentFormats = &gfx_context->swapchain.selected_format.format,
			.depthAttachmentFormat   = VK_FORMAT_D32_SFLOAT,
			.stencilAttachmentFormat = VK_FORMAT_UNDEFINED,
		};

		VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
			.depthTestEnable       = true,
			.depthWriteEnable      = true,
			.depthCompareOp        = VK_COMPARE_OP_LESS_OR_EQUAL,
			.depthBoundsTestEnable = false,
			.stencilTestEnable     = false,
		};

		VkGraphicsPipelineCreateInfo pipeline_create_info = {
			.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
			.pNext = &pipeline_rendering_create_info,
			.flags      = 0,
			.stageCount = 2,
			.pStages    = stages,
			.pVertexInputState   = &vertex_input_state,
			.pInputAssemblyState = &input_assembly_state,
			.pViewportState      = &viewport_state,
			.pRasterizationState = &rasterization_state,
			.pMultisampleState   = &multisample_state,
			.pDepthStencilState  = &depth_stencil_state,
			.pColorBlendState    = &color_blend_state,
			.pDynamicState       = &dynamic_state,
			.layout              = renderer->pipeline_layout,
			.renderPass          = VK_NULL_HANDLE,
			.subpass             = 0,
			.basePipelineHandle  = VK_NULL_HANDLE,
		};

		vkCreateGraphicsPipelines(gfx_context->device, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr,
								  &renderer->pipeline);
		name_object(renderer->pipeline, "Main pipeline");
	}
}

void renderer_destroy_pipeline()
{
	ZoneScopedN("Pipeline destruction");
}

void renderer_create_sync_primitives()
{
	ZoneScopedN("Synchronization primitives creation");

	VkSemaphoreTypeCreateInfo semaphore_type_create_info = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
		.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
		.initialValue  = renderer->buffering - 1,
	};

	VkSemaphoreCreateInfo semaphore_create_info = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		.pNext = &semaphore_type_create_info,
		.flags = 0,
	};

	vkCreateSemaphore(gfx_context->device, &semaphore_create_info, nullptr, &renderer->upload_semaphore);
	vkCreateSemaphore(gfx_context->device, &semaphore_create_info, nullptr, &renderer->render_semaphore);
	name_object(renderer->upload_semaphore, "Upload timeline semaphore");
	name_object(renderer->render_semaphore, "Render timeline semaphore");
}

void renderer_destroy_sync_primitives()
{
	ZoneScopedN("Synchronization primitives destruction");
}

void renderer_create_upload_heap()
{
	ZoneScopedN("Main upload heap creation");

	{
		ZoneScopedN("Heap allocation");

		VkBufferCreateInfo create_info = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size  = 500000000, // 500 MB
			.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		};

		VmaAllocationCreateInfo vma_create_info = {
			.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
			.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
		};

		vmaCreateBuffer(gfx_context->vma_allocator,
						&create_info,
						&vma_create_info,
						&renderer->main_upload_heap.buffer,
						&renderer->main_upload_heap.allocation,
						nullptr);
		vmaMapMemory(gfx_context->vma_allocator, renderer->main_upload_heap.allocation,
					 &renderer->main_upload_heap_ptr);

		name_object(renderer->main_upload_heap.buffer, "Main upload heap");
	}

	{
		ZoneScopedN("Command Pool and Command buffer creation");

		VkCommandPoolCreateInfo command_pool_create_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = gfx_context->gfx_queue_family_index,
		};

		vkCreateCommandPool(gfx_context->device, &command_pool_create_info, nullptr, &renderer->upload_command_pool);
		name_object(renderer->upload_command_pool, "Main upload heap command pool");

		VkCommandBufferAllocateInfo allocate_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool        = renderer->upload_command_pool,
			.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1,
		};

		vkAllocateCommandBuffers(gfx_context->device, &allocate_info, &renderer->upload_command_buffer);
		name_object(renderer->upload_command_buffer, "Main upload heap command buffer");
	}
}

void renderer_destroy_upload_heap()
{
	ZoneScopedN("Main upload heap destruction");
}

void load_scene_data()
{
	ZoneScopedN("Loading scene data");

	using namespace fastgltf;

	auto start_time = std::chrono::high_resolution_clock::now();

	Parser parser;
	std::filesystem::path gltf_file = "assets/Sponza/glTF/Sponza.gltf";

	spdlog::info("Loading GLTF 2.0 file {}", gltf_file.string());

	GltfDataBuffer gltf_data;
	gltf_data.loadFromFile(gltf_file);

	auto gltf = parser.loadGLTF(&gltf_data, gltf_file.parent_path(),
	                            Options::LoadExternalBuffers | Options::LoadExternalImages);

	if (gltf->parse() != Error::None)
		throw std::runtime_error("GLTF Problem");

	auto asset = gltf->getParsedAsset();

	auto upload_writer = Mapped_Buffer_Writer(renderer->main_upload_heap_ptr);

	// Texture and sampler data loading into texture_manager.
	// This design might seem weird, but gathering all textures in one place opens doors to easier migration to
	// bindless in the future.

	struct Image_Upload {
		size_t            image_index;
		int               height, width;
		VkDeviceSize      upload_offset;
	};

	std::vector<Image_Upload>          image_uploads;
	std::unordered_map<size_t, size_t> asset_map_images; // Maps index of GLTF image to index in texture_manager

	// Upload default texture when we're at this
	{
		uint8_t pixel_data[] = { 255, 255, 255, 255 };
		upload_writer.align_next(4); // Offset need to be multiple of texel size (4)
		VkDeviceSize offset = upload_writer.write(pixel_data, 4);
		image_uploads.push_back({Texture_Manager::DEFAULT_TEXTURE, 1, 1, offset, });
	}

	for (size_t asset_image_index = 0; asset_image_index < asset->images.size(); asset_image_index++)
	{
		auto& image = asset->images[asset_image_index];

		auto data = std::get<sources::Vector>(image.data);
		
		if (data.mimeType == MimeType::None)
			throw std::runtime_error("GLTF Problem");

		int width, height, channels;
		auto pixels = stbi_load_from_memory(data.bytes.data(), static_cast<int>(data.bytes.size()),
											&width, &height, &channels, STBI_rgb_alpha);

		if (pixels == nullptr)
			throw std::runtime_error("GLTF Problem");

		Allocated_View_Image view_image; // What we will be allocating

		VkImageCreateInfo image_create_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.imageType     = VK_IMAGE_TYPE_2D,
			.format        = VK_FORMAT_R8G8B8A8_SRGB,
			.extent        = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1},
			.mipLevels     = width == 4 ? 1u : 10u,
			.arrayLayers   = 1,
			.samples       = VK_SAMPLE_COUNT_1_BIT,
			.tiling        = VK_IMAGE_TILING_OPTIMAL,
			.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
			.sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		};

		VmaAllocationCreateInfo allocation_create_info = { .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, };

		VmaAllocationInfo allocation_info;
		vmaCreateImage(gfx_context->vma_allocator, &image_create_info,
					   &allocation_create_info,&view_image.image,
					   &view_image.allocation, &allocation_info);
		name_object(view_image.image, "Loaded image {} {}", asset_image_index, image.name);

		// Create default image view
		create_default_image_view(gfx_context->device, image_create_info, view_image.image, nullptr, &view_image.view);
		name_object(view_image.view, "Loaded image view {} {}", asset_image_index, image.name);

		// Put in texture_manager
		texture_manager->images.push_back(view_image);
		size_t image_index = texture_manager->images.size() - 1;
		asset_map_images[asset_image_index] = image_index;

		// Push to upload heap and enqueue for upload
		upload_writer.align_next(4); // Offset need to be multiple of texel size (4)
		VkDeviceSize offset = upload_writer.write(pixels, height * width * 4);
		image_uploads.push_back({
			.image_index   = image_index,
			.height        = height,
			.width         = width,
			.upload_offset = offset,
		});

		// Free from stb_image
		stbi_image_free(pixels);
	}

	// Read and crate all samplers

	std::unordered_map<size_t, size_t> asset_map_samplers; // Maps index of GLTF image to index in texture_manager

	for (size_t asset_sampler_index = 0; asset_sampler_index < asset->samplers.size(); asset_sampler_index++) {
		auto& sampler = asset->samplers[asset_sampler_index];

		VkSamplerAddressMode address_mode_u =
			(sampler.wrapS == Wrap::ClampToEdge)    ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE   :
			(sampler.wrapS == Wrap::MirroredRepeat) ? VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT :
			VK_SAMPLER_ADDRESS_MODE_REPEAT; // Wrap::Repeat

		VkSamplerAddressMode address_mode_v =
			(sampler.wrapT == Wrap::ClampToEdge)    ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE   :
			(sampler.wrapT == Wrap::MirroredRepeat) ? VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT :
			VK_SAMPLER_ADDRESS_MODE_REPEAT; // Wrap::Repeat

		VkFilter mag_filter = VK_FILTER_LINEAR;
		if (sampler.magFilter.has_value() && sampler.magFilter.value() == Filter::Nearest)
		{
			mag_filter = VK_FILTER_NEAREST; // Mag filter can only have linear or nearest.
		}

		VkFilter min_filter             = VK_FILTER_LINEAR;
		VkSamplerMipmapMode mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		if (sampler.minFilter.has_value())
		{
			auto gltf_f = sampler.minFilter.value();
			min_filter = (gltf_f == Filter::NearestMipMapNearest || gltf_f == Filter::NearestMipMapLinear)
				? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
			mipmap_mode = (gltf_f == Filter::NearestMipMapNearest || gltf_f == Filter::LinearMipMapNearest)
						 ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
		}

		// TODO FIX this doesn't work!!! Sampler selection in shader is broken. I'll leave anisotropy off and
		// low mip max lod for debug purposes
		VkSamplerCreateInfo sampler_create_info = {
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.magFilter        = mag_filter,
			.minFilter        = min_filter,
			.mipmapMode       = mipmap_mode,
			.addressModeU     = address_mode_u,
			.addressModeV     = address_mode_v,
			.addressModeW     = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.mipLodBias       = 0,
			.anisotropyEnable = false,
			.maxAnisotropy    = 16,
			.compareEnable    = false,
			.compareOp        = VK_COMPARE_OP_NEVER,
			.borderColor      = VK_BORDER_COLOR_INT_OPAQUE_WHITE,
			.unnormalizedCoordinates = false,
		};
		VkSampler our_sampler;
		vkCreateSampler(gfx_context->device, &sampler_create_info, nullptr, &our_sampler);
		name_object(our_sampler, "Loaded sampler {} {}", asset_sampler_index, sampler.name);

		// Put in texture_manager
		texture_manager->samplers.push_back(our_sampler);
		size_t sampler_index = texture_manager->samplers.size() - 1;
		asset_map_samplers[asset_sampler_index] = sampler_index;
	}

	// Material parsing.

	std::unordered_map<size_t, size_t> asset_map_materials; // Maps index of GLTF image to index in material_manager

	for (size_t asset_material_index = 0; asset_material_index < asset->materials.size(); asset_material_index++) {
		auto &material = asset->materials[asset_material_index];

		if (!material.pbrData.has_value())
			throw std::runtime_error("GLTF Problem");
		PBRData& pbr_data = material.pbrData.value();

		uint32_t albedo_texture          = Texture_Manager::DEFAULT_TEXTURE;
		uint32_t albedo_sampler          = Texture_Manager::DEFAULT_SAMPLER;
		uint32_t metal_roughness_texture = Texture_Manager::DEFAULT_TEXTURE;
		uint32_t metal_roughness_sampler = Texture_Manager::DEFAULT_SAMPLER;

		// Find base color texture and sampler
		if (pbr_data.baseColorTexture.has_value())
		{
			auto& base_color_texture = pbr_data.baseColorTexture.value();

			if (base_color_texture.texCoordIndex != 0)
				throw std::runtime_error("GLTF Problem");

			auto image_index = asset->textures[base_color_texture.textureIndex].imageIndex;
			if (!image_index.has_value())
				throw std::runtime_error("GLTF Problem");
			albedo_texture = asset_map_images[image_index.value()];

			auto sampler_index = asset->textures[base_color_texture.textureIndex].samplerIndex;
			if (sampler_index.has_value())
			{
				albedo_sampler = asset_map_samplers[sampler_index.value()];
			}
		}

		// Find metalness+roughness texture and sampler
		if (pbr_data.metallicRoughnessTexture.has_value())
		{
			auto& mr_texture = pbr_data.metallicRoughnessTexture.value();

			if (mr_texture.texCoordIndex != 0)
				throw std::runtime_error("GLTF Problem");

			auto image_index = asset->textures[mr_texture.textureIndex].imageIndex;
			if (!image_index.has_value())
				throw std::runtime_error("GLTF Problem");
			metal_roughness_texture = asset_map_images[image_index.value()];

			auto sampler_index = asset->textures[mr_texture.textureIndex].samplerIndex;
			if (sampler_index.has_value())
			{
				metal_roughness_sampler = asset_map_samplers[sampler_index.value()];
			}
		}

		PBR_Material pbr_material = {
			.albedo_color            = glm::make_vec4(pbr_data.baseColorFactor.data()),
			.albedo_texture          = albedo_texture,
			.albedo_sampler          = albedo_sampler,
			.metalness_factor        = pbr_data.metallicFactor,
			.roughness_factor        = pbr_data.roughnessFactor,
			.metal_roughness_texture = metal_roughness_texture,
			.metal_roughness_sampler = metal_roughness_sampler,
		};

		// Put it in material manager
		material_manager->materials.push_back(pbr_material);
		size_t material_index = material_manager->materials.size() - 1;
		asset_map_materials[asset_material_index] = material_index;
	}

	uint32_t material_data_size = material_manager->materials.size() * sizeof(PBR_Material);
	VkDeviceSize material_data_offset = upload_writer.write(material_manager->materials.data(), material_data_size);

	// Meshes parsing and loading

	std::vector<VkBufferCopy> vertex_copies;
	std::vector<VkBufferCopy> indices_copies;

	// This maps single GLTF mesh into set of our meshes
	// (each mesh might have multiple primitives, which we consider separate meshes)
	struct Primitive
	{
		Mesh_Manager::Id mesh_id;
		uint32_t         material_id;
	};

	std::unordered_map<size_t, std::vector<Primitive>> asset_map_meshes;

	for (size_t mesh_index = 0; mesh_index < asset->meshes.size(); mesh_index++)
	{
		auto& mesh = asset->meshes[mesh_index];
		asset_map_meshes[mesh_index] = {}; // Initialize primitives list

		// Each primitive will be separate mesh
		for (auto& primitive : mesh.primitives)
		{
			// Right now, only handle triangles (conversion from other types will be implemented later)
			if (primitive.type != PrimitiveType::Triangles)
				throw std::runtime_error("GLTF Problem");

			// We don't generate indices as of now
			if (!primitive.indicesAccessor.has_value())
				throw std::runtime_error("GLTF Problem");

			// Check if all required attributes are present
			bool attributes_present =
				primitive.attributes.contains("POSITION") &&
				primitive.attributes.contains("NORMAL") &&
				primitive.attributes.contains("TEXCOORD_0");

			bool has_tangent = primitive.attributes.contains("TANGENT");

			if (!attributes_present)
				throw std::runtime_error("GLTF Problem");

			if (!has_tangent)
			{
				spdlog::info("Missing tangent!"); // Todo better logging
			}

			auto indices_accessor  = asset->accessors[primitive.indicesAccessor.value()];
			auto position_accessor = asset->accessors[primitive.attributes["POSITION"]];
			auto normal_accessor   = asset->accessors[primitive.attributes["NORMAL"]];
			auto tangent_accessor  = asset->accessors[primitive.attributes["TANGENT"]];
			auto texcoord_accessor = asset->accessors[primitive.attributes["TEXCOORD_0"]];

			// So eh, apparently, texcoord can be float, uint8_t or uint16_t... we might need to convert.
			if (texcoord_accessor.componentType != ComponentType::Float)
				throw std::runtime_error("GLTF Problem");

			// Also, indices can also be of multiple types...
			if (indices_accessor.componentType != ComponentType::UnsignedShort)
				throw std::runtime_error("GLTF Problem");

			// All attributes accessors has matching counts. This is enforced by the specs
			size_t attr_count = position_accessor.count;

			// Save offset of vertex region
			VkDeviceSize vertex_src_offset = upload_writer.offset();

			for (size_t offset = 0; offset < attr_count; offset++)
			{
				auto p = getAccessorElement<glm::vec3>(*asset, position_accessor, offset);
				auto n = getAccessorElement<glm::vec3>(*asset, normal_accessor,   offset);
				auto tx= getAccessorElement<glm::vec2>(*asset, texcoord_accessor, offset);

				auto tg = (has_tangent)
						  ? getAccessorElement<glm::vec4>(*asset, tangent_accessor,  offset)
						  : glm::vec4(0, 0, 0, 0);

				auto attr = { p.x, p.y, p.z, n.x, n.y, n.z, tg.x, tg.y, tg.z, tg.w, tx.x, tx.y }; // Vulkan UV fix
				upload_writer.write(attr.begin(), 12 * sizeof(float));
			}

			// Save offset of indices region
			VkDeviceSize indices_src_offset = upload_writer.offset();

			// Copy indices
			copyFromAccessor<uint16_t>(*asset, indices_accessor, upload_writer.offset_ptr);
			upload_writer.advance(indices_accessor.count * sizeof(uint16_t));

			// Allocate mesh and indices
			VkResult alloc_result;
			VmaVirtualAllocationCreateInfo vertex_allocation_info = { .size = attr_count * sizeof(float) * 12 };
			VmaVirtualAllocation vertex_allocation;
			VkDeviceSize vertex_dst_offset;
			alloc_result = vmaVirtualAllocate(mesh_manager->vertex_sub_allocator,
											  &vertex_allocation_info, &vertex_allocation,
											  &vertex_dst_offset);
			if (alloc_result != VK_SUCCESS)
				throw std::runtime_error("GLTF Problem");

			VmaVirtualAllocationCreateInfo indices_allocation_info = { .size = indices_accessor.count * sizeof(uint16_t) };
			VmaVirtualAllocation indices_allocation;
			VkDeviceSize indices_dst_offset;
			alloc_result = vmaVirtualAllocate(mesh_manager->indices_sub_allocator,
											  &indices_allocation_info,&indices_allocation,
											  &indices_dst_offset);
			if (alloc_result != VK_SUCCESS)
				throw std::runtime_error("GLTF Problem");

			Mesh_Manager::Mesh_Description mesh_description = {
				.vertex_offset  = vertex_dst_offset,
				.vertex_count   = static_cast<uint32_t>(attr_count),
				.indices_offset = indices_dst_offset,
				.indices_count  = static_cast<uint32_t>(indices_accessor.count),
				.vertex_allocation  = vertex_allocation,
				.indices_allocation = indices_allocation,
			};

			Mesh_Manager::Id mesh_id = mesh_manager->next_index++;
			mesh_manager->meshes[mesh_id] = mesh_description;

			vertex_copies.push_back({
										.srcOffset = vertex_src_offset,
										.dstOffset = vertex_dst_offset,
										.size      = vertex_allocation_info.size,
									});

			indices_copies.push_back({
										 .srcOffset = indices_src_offset,
										 .dstOffset = indices_dst_offset,
										 .size      = indices_allocation_info.size,
									 });

			// Get material index
			uint32_t material_id = (primitive.materialIndex.has_value())
				? asset_map_materials[primitive.materialIndex.value()] : Material_Manager::DEFAULT_MATERIAL;

			asset_map_meshes[mesh_index].push_back({ .mesh_id = mesh_id, .material_id = material_id });
		}
	}

	// Now, let's start traversing entire scene and node hierarchy

	struct Enqueued_Node
	{
		glm::mat4 parent_transform;
		size_t    node_id;
	};
	std::deque<Enqueued_Node> nodes_queue;

	// Enqueue root nodes from scenes for traversal
	for (auto& scene : asset->scenes)
	{
		for (auto& node_id : scene.nodeIndices)
		{
			nodes_queue.push_back({
									  .parent_transform = glm::translate(glm::mat4 {1.0f}, glm::vec3 {0.0f, 0.0f, 0.0f}),
									  .node_id = node_id,
								  });
		}
	}

	while (!nodes_queue.empty())
	{
		Enqueued_Node enqueued_node = nodes_queue.front();
		Node node = asset->nodes[enqueued_node.node_id];

		// Compose matrix if needed
		glm::mat4 transform_matrix;
		if (std::holds_alternative<Node::TransformMatrix>(node.transform))
		{
			transform_matrix = glm::make_mat4(std::get<Node::TransformMatrix>(node.transform).data());
		}
		else
		{
			Node::TRS trs = std::get<Node::TRS>(node.transform);
			auto t = glm::translate(glm::mat4 { 1.0f }, glm::make_vec3(trs.translation.data()));
			auto r = glm::mat4_cast(glm::make_quat(trs.rotation.data()));
			auto s = glm::scale(glm::mat4 { 1.0f }, glm::make_vec3(trs.scale.data()));
			transform_matrix = t * r * s;
		}

		for (auto& child_id : node.children)
		{
			nodes_queue.push_back({
									  .parent_transform = transform_matrix * enqueued_node.parent_transform,
									  .node_id = child_id,
								  });
		}

		// Finally, spawn scene objects
		if (node.meshIndex.has_value())
		{
			for (Primitive& primitive : asset_map_meshes[node.meshIndex.value()])
			{
				Render_Object render_object = {
					.mesh_id     = primitive.mesh_id,
					.material_id = primitive.material_id,
					.transform   = transform_matrix,
				};
				scene_data->render_objects.push_back(render_object);
			}
		}

		nodes_queue.pop_front();
	}

	{
		ZoneScopedN("Upload command submission");

		flush_buffer_writer(upload_writer, gfx_context->vma_allocator, renderer->main_upload_heap.allocation);

		VkCommandBufferBeginInfo begin_info = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, };
		vkBeginCommandBuffer(renderer->upload_command_buffer, &begin_info);

		// Copy buffers
		vkCmdCopyBuffer(renderer->upload_command_buffer, renderer->main_upload_heap.buffer,
						mesh_manager->vertex_buffer.buffer, vertex_copies.size(), vertex_copies.data());
		vkCmdCopyBuffer(renderer->upload_command_buffer, renderer->main_upload_heap.buffer,
						mesh_manager->indices_buffer.buffer, indices_copies.size(), indices_copies.data());

		VkBufferCopy material_copy = {
			.srcOffset = material_data_offset,
			.dstOffset = 0,
			.size      = material_data_size,
		};
		vkCmdCopyBuffer(renderer->upload_command_buffer, renderer->main_upload_heap.buffer,
						material_manager->material_storage_buffer.buffer, 1, &material_copy);

		// Enqueue upload of all pending textures
		for (auto& image_upload : image_uploads)
		{
			auto vk_image = texture_manager->images[image_upload.image_index].image;

			uint32_t mip_levels = image_upload.height == 1 || image_upload.height == 4 ? 1 : 10;

			// Transition first mip to copy layout
			VkImageMemoryBarrier to_transfer_dst_barrier = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = 0,
				.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
				.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				.image         = vk_image,
				.subresourceRange = {
					.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel   = 0,
					.levelCount     = 1,
					.baseArrayLayer = 0,
					.layerCount     = 1,
				},
			};
			vkCmdPipelineBarrier(renderer->upload_command_buffer, VK_PIPELINE_STAGE_HOST_BIT,
								 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
								 &to_transfer_dst_barrier);

			// Enqueue copy
			VkBufferImageCopy region = {
				.bufferOffset = image_upload.upload_offset,
				.imageSubresource = {
					.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
					.mipLevel       = 0,
					.baseArrayLayer = 0,
					.layerCount     = 1,
				},
				.imageExtent = {
					.width   = static_cast<uint32_t>(image_upload.width),
					.height  = static_cast<uint32_t>(image_upload.height),
					.depth   = 1,
				},
			};
			vkCmdCopyBufferToImage(renderer->upload_command_buffer, renderer->main_upload_heap.buffer,
								   vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

			// Await transfer for mip 0
			VkImageMemoryBarrier await_transfer_barrier = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
				.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				.image         = vk_image,
				.subresourceRange = {
					.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel   = 0,
					.levelCount     = 1,
					.baseArrayLayer = 0,
					.layerCount     = 1,
				},
			};
			vkCmdPipelineBarrier(renderer->upload_command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
								 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
								 &await_transfer_barrier);

			// Generate mipmaps
			for (uint32_t dst_mip = 1; dst_mip < mip_levels; dst_mip++)
			{
				// Move dst mip to trransfer dst
				VkImageMemoryBarrier mip_to_transfer_dst = {
					.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
					.srcAccessMask = 0,
					.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
					.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED,
					.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					.image         = vk_image,
					.subresourceRange = {
						.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
						.baseMipLevel   = dst_mip,
						.levelCount     = 1,
						.baseArrayLayer = 0,
						.layerCount     = 1,
					},
				};
				vkCmdPipelineBarrier(renderer->upload_command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
									 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
									 &mip_to_transfer_dst);

				VkImageBlit2 regions = {
					.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
					.srcSubresource = {
						.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
						.mipLevel       = dst_mip - 1,
						.baseArrayLayer = 0,
						.layerCount     = 1,
					},
					.srcOffsets = {
						{ 0, 0, 0 },
						{ (image_upload.width >> dst_mip - 1), (image_upload.width >> dst_mip - 1), 1 }
					},
					.dstSubresource = {
						.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
						.mipLevel       = dst_mip,
						.baseArrayLayer = 0,
						.layerCount     = 1,
					},
					.dstOffsets = {
						{ 0, 0, 0 },
						{ (image_upload.width >> dst_mip), (image_upload.width >> dst_mip), 1 }
					},
				};

				VkBlitImageInfo2 blit_info = {
					.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
					.srcImage       = vk_image,
					.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					.dstImage       = vk_image,
					.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					.regionCount    = 1,
					.pRegions       = &regions,
					.filter         = VK_FILTER_LINEAR,
				};

				vkCmdBlitImage2(renderer->upload_command_buffer, &blit_info);

				// Move dst mip to trransfer src
				VkImageMemoryBarrier mip_to_transfer_src = {
					.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
					.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
					.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
					.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					.image         = vk_image,
					.subresourceRange = {
						.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
						.baseMipLevel   = dst_mip,
						.levelCount     = 1,
						.baseArrayLayer = 0,
						.layerCount     = 1,
					},
				};
				vkCmdPipelineBarrier(renderer->upload_command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
									 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
									 &mip_to_transfer_src);
			}

			// Immediately transition into proper layout
			VkImageMemoryBarrier from_transform_transition_barrier = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT,
				.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
				.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.image         = vk_image,
				.subresourceRange = {
					.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel   = 0,
					.levelCount     = mip_levels,
					.baseArrayLayer = 0,
					.layerCount     = 1,
				},
			};

			vkCmdPipelineBarrier(renderer->upload_command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
								 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
								 &from_transform_transition_barrier);
		}

		vkEndCommandBuffer(renderer->upload_command_buffer);

		VkCommandBufferSubmitInfo command_buffer_submit_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
			.commandBuffer = renderer->upload_command_buffer,
		};

		VkSubmitInfo2 submit_info = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
			.waitSemaphoreInfoCount   = 0,
			.commandBufferInfoCount   = 1,
			.pCommandBufferInfos      = &command_buffer_submit_info,
			.signalSemaphoreInfoCount = 0,
		};
		vkQueueSubmit2(gfx_context->gfx_queue, 1, &submit_info, VK_NULL_HANDLE);

		// Block entire upload operation
		{
			ZoneScopedN("Upload command execution");
			vkDeviceWaitIdle(gfx_context->device);
		}
	}

	auto finish_time = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::duration<float>>(finish_time - start_time);
	spdlog::info("Scene loaded! [{:.2f}s]", duration.count());

	// Temporary write to descriptor.
	// This is vallid only for now, as we don't have support for dynamic
	// texture streaming. Everything is loaded up-front.
	
	// Bind uniform buffer
	VkDescriptorBufferInfo global_uniform_descriptor = {
		.buffer = renderer->global_uniform_data_buffer.buffer,
		.offset = 0,
		.range  = sizeof(Global_Uniform_Data),
	};

	// Samplers
	std::vector<VkDescriptorImageInfo> sampler_descriptor_updates;
	for (uint32_t sampler_index = 0; sampler_index < texture_manager->images.size(); sampler_index++)
	{
		VkDescriptorImageInfo update_info = { .sampler = texture_manager->samplers[0] };
		sampler_descriptor_updates.push_back(update_info);
	}

	// Images
	std::vector<VkDescriptorImageInfo> image_descriptor_updates;
	for (uint32_t image_index = 0; image_index < texture_manager->images.size(); image_index++)
	{
		VkDescriptorImageInfo update_info = {
			.imageView   = texture_manager->images[image_index].view,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};
		image_descriptor_updates.push_back(update_info);
	}

	// Bind material storage buffer
	VkDescriptorBufferInfo material_storage_descriptor = {
		.buffer = material_manager->material_storage_buffer.buffer,
		.offset = 0,
		.range  = 40000,
	};

	VkWriteDescriptorSet descriptor_set_writes[] = {
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet          = renderer->global_data_descriptor_set,
			.dstBinding      = 0,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.pBufferInfo     = &global_uniform_descriptor,
		},
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet          = renderer->global_data_descriptor_set,
			.dstBinding      = 1,
			.dstArrayElement = 0,
			.descriptorCount = static_cast<uint32_t>(sampler_descriptor_updates.size()),
			.descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLER,
			.pImageInfo      = sampler_descriptor_updates.data(),
		},
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet          = renderer->global_data_descriptor_set,
			.dstBinding      = 2,
			.dstArrayElement = 0,
			.descriptorCount = static_cast<uint32_t>(image_descriptor_updates.size()),
			.descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
			.pImageInfo      = image_descriptor_updates.data(),
		},
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet          = renderer->global_data_descriptor_set,
			.dstBinding      = 3,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.pBufferInfo     = &material_storage_descriptor,
		},
	};

	vkUpdateDescriptorSets(gfx_context->device, 4, descriptor_set_writes, 0, nullptr);
}

void renderer_dispatch()
{
	ZoneScopedN("Renderer dispatch");

	auto frame_i = app->frame_number % renderer->buffering;
	auto current_frame = &renderer->frame_data[frame_i];

	// Use the latter to refer to current frame in timeline semaphores. Use first to refer to the same frame,
	// but renderer->buffering frames ago.
	auto previous_timeline_frame_i = app->frame_number;
	auto current_timeline_frame_i = app->frame_number + renderer->buffering;

	// Await same frame's previous upload fence
	{
		ZoneScopedN("Waiting on previous upload");

		uint64_t semaphore_await_value = previous_timeline_frame_i;
		VkSemaphoreWaitInfo wait_info = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
			.semaphoreCount = 1,
			.pSemaphores    = &renderer->upload_semaphore,
			.pValues        = &semaphore_await_value,
		};
		vkWaitSemaphores(gfx_context->device, &wait_info, UINT64_MAX);
	}

	char* staging_buffer_ptr = static_cast<char *>(current_frame->staging_buffer_ptr);
	size_t linear_allocator = 0;

	// This is the offset inside per frame data buffer that will be used during this frame
	size_t current_per_frame_data_buffer_offset = clamp_size_to_alignment(
		sizeof(Global_Uniform_Data),
		gfx_context->physical_device_properties.properties.limits.minUniformBufferOffsetAlignment) * frame_i;

	// Begin recording
	VkCommandBufferBeginInfo upload_begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	vkBeginCommandBuffer(current_frame->upload_command_buffer, &upload_begin_info); // Implicit reset
	command_buffer_region_begin(current_frame->upload_command_buffer, "Upload stage");

	// Build and stage per-frame data
	{
		ZoneScopedN("Build per frame uniform data");

		glm::mat4 view = camera_get_view_matrix();
		glm::mat4 projection = glm::perspective(
			glm::radians(70.f),
			1280.f / 720.f,
			0.1f,
			200.0f);
		// projection[1][1] *= -1;
		/*glm::mat4 model = glm::rotate(
			glm::mat4{1.0f},
			rotation,
			glm::vec3(0, 1, 0));
		glm::mat4 render_matrix = projection * view * model;*/
		glm::mat4 render_matrix = projection * view;

		auto uniform_data = reinterpret_cast<Global_Uniform_Data*>(staging_buffer_ptr + linear_allocator);

		uniform_data->render_matrix = render_matrix;

		// Upload
		VkBufferCopy region = {
			.srcOffset = linear_allocator,
			.dstOffset = current_per_frame_data_buffer_offset,
			.size      = sizeof(Global_Uniform_Data),
		};
		vkCmdCopyBuffer(current_frame->upload_command_buffer, current_frame->staging_buffer.buffer,
						renderer->global_uniform_data_buffer.buffer, 1, &region);

		linear_allocator += sizeof(Global_Uniform_Data);
	}

	command_buffer_region_end(current_frame->upload_command_buffer);
	vkEndCommandBuffer(current_frame->upload_command_buffer);

	{
		ZoneScopedN("Submit staging buffer");

		VkSemaphoreSubmitInfo wait_info = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = renderer->upload_semaphore,
			.value     = previous_timeline_frame_i,
			.stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
		};

		VkCommandBufferSubmitInfo command_buffer_submit_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
			.commandBuffer = current_frame->upload_command_buffer,
		};

		VkSemaphoreSubmitInfo signal_info = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = renderer->upload_semaphore,
			.value     = current_timeline_frame_i,
			.stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
		};

		VkSubmitInfo2 submit_info = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
			.waitSemaphoreInfoCount   = 1,
			.pWaitSemaphoreInfos      = &wait_info,
			.commandBufferInfoCount   = 1,
			.pCommandBufferInfos      = &command_buffer_submit_info,
			.signalSemaphoreInfoCount = 1,
			.pSignalSemaphoreInfos    = &signal_info,
		};
		vkQueueSubmit2(gfx_context->gfx_queue, 1, &submit_info, VK_NULL_HANDLE);
	}

	// Await same frame's previous render fence
	{
		ZoneScopedN("Waiting on render");

		uint64_t semaphore_await_value = previous_timeline_frame_i;
		VkSemaphoreWaitInfo wait_info = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
			.semaphoreCount = 1,
			.pSemaphores    = &renderer->render_semaphore,
			.pValues        = &semaphore_await_value,
		};
		vkWaitSemaphores(gfx_context->device, &wait_info, UINT64_MAX);
	}

	// Acquire swapchain image_handle and recreate swapchain if necessary
	Combined_View_Image swapchain_image;
	uint32_t swapchain_image_index;
	{
		ZoneScopedN("Swapchain image_handle acquiring");

		auto acquire_result = vkAcquireNextImageKHR(gfx_context->device, gfx_context->swapchain.handle, UINT64_MAX,
													current_frame->acquire_semaphore, VK_NULL_HANDLE,
													&swapchain_image_index);
		swapchain_image = gfx_context->swapchain.images[swapchain_image_index];

		if (acquire_result == VK_SUBOPTIMAL_KHR || acquire_result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			ZoneScopedN("Swapchain recreation request");

			// In theory, if we get VK_ERROR_OUT_OF_DATE_KHR, but we'll have minimized window, the extent will be
			// (0, 0) and swapchain will not be recreated, this no resources will be created. This is may crash,
			// since well, we need a swapchain (although we could skip rendering at all, but meh, idk).
			// But, this is yet to be observed behaviour, hope it doesn't happen. Maybe, there is a valid usage
			// guarantee for this???

			{
				ZoneScopedN("Idling on previous frames");

				// We could wait on fences, but I'm lazy
				vkDeviceWaitIdle(gfx_context->device);
			}

			if (recreate_swapchain())
			{
				recreate_swapchain_dependent_resources();

				// FIXME: Following unfortunately will result in validation error, since present semaphore isn't
				// being "reset" on swapchain destroy.
				vkAcquireNextImageKHR(gfx_context->device, gfx_context->swapchain.handle, UINT64_MAX,
									  current_frame->acquire_semaphore, VK_NULL_HANDLE, &swapchain_image_index);
				swapchain_image = gfx_context->swapchain.images[swapchain_image_index];
			}
		}
	}

	// Begin rendering command buffer
	VkCommandBufferBeginInfo draw_begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	vkBeginCommandBuffer(current_frame->draw_command_buffer, &draw_begin_info);

	{
		ZoneScopedN("Transition to color attachment layout");

		VkImageSubresourceRange range = {
			.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel   = 0,
			.levelCount     = 1,
			.baseArrayLayer = 0,
			.layerCount     = 1,
		};

		VkImageMemoryBarrier render_transition_barrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.dstAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED, // Legal, since we're clearing this image_handle on render load anyway
			.newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image               = swapchain_image.image,
			.subresourceRange    = range,
		};

		vkCmdPipelineBarrier(
			current_frame->draw_command_buffer,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &render_transition_barrier);
	}

	command_buffer_region_begin(current_frame->draw_command_buffer, "Main draw pass");
	{
		ZoneScopedN("Main draw pass");

		VkClearValue color_clear_value = {.color = {.float32 = {0.2, 0.2, 0.2, 1}}};

		VkRenderingAttachmentInfo swapchain_attachment_info = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView   = swapchain_image.view,
			.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.resolveMode = VK_RESOLVE_MODE_NONE,
			.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
			.clearValue  = color_clear_value,
		};

		VkClearValue depth_clear_value = { .depthStencil = { .depth = 1 } };

		VkRenderingAttachmentInfo depth_attachment_info = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView   = renderer->depth_buffer.view,
			.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
			.resolveMode = VK_RESOLVE_MODE_NONE,
			.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
			.clearValue  = depth_clear_value,
		};

		VkRenderingInfo rendering_info = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
			.flags = 0,
			.renderArea = {
				.offset = {}, // Zero
				.extent = gfx_context->swapchain.extent,
			},
			.layerCount = 1,
			.viewMask   = 0,
			.colorAttachmentCount = 1,
			.pColorAttachments    = &swapchain_attachment_info,
			.pDepthAttachment     = &depth_attachment_info,
		};

		VkViewport viewport = {
			.x        = 0,
			.y        = (float) gfx_context->swapchain.extent.height,
			.width    = (float) gfx_context->swapchain.extent.width,
			.height   = -1 * (float) gfx_context->swapchain.extent.height,
			.minDepth = 0.0f,
			.maxDepth = 1.0f,
		};
		VkRect2D scissor = { .offset = {}, .extent = gfx_context->swapchain.extent };

		vkCmdSetViewport(current_frame->draw_command_buffer, 0, 1, &viewport);
		vkCmdSetScissor(current_frame->draw_command_buffer, 0, 1, &scissor);

		{
			ZoneScopedN("Bind descriptor");

			uint32_t offset = current_per_frame_data_buffer_offset;
			vkCmdBindDescriptorSets(current_frame->draw_command_buffer,
									VK_PIPELINE_BIND_POINT_GRAPHICS,
									renderer->pipeline_layout,
									0,
									1, &renderer->global_data_descriptor_set,
									1, &offset);
		}

		{
			ZoneScopedN("Pipeline bind");
			vkCmdBindPipeline(current_frame->draw_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->pipeline);
		}

		vkCmdBeginRendering(current_frame->draw_command_buffer, &rendering_info);
		command_buffer_region_begin(current_frame->draw_command_buffer, "Rendering");
		{
			ZoneScopedN("Drawing");

			for (auto& render_object : scene_data->render_objects)
			{
				Mesh_Manager::Mesh_Description mesh = mesh_manager->get_mesh(render_object.mesh_id);
				vkCmdBindVertexBuffers(current_frame->draw_command_buffer, 0, 1, &mesh_manager->vertex_buffer.buffer,
									   &mesh.vertex_offset);
				vkCmdBindIndexBuffer(current_frame->draw_command_buffer, mesh_manager->indices_buffer.buffer,
									 mesh.indices_offset, VK_INDEX_TYPE_UINT16);
				vkCmdPushConstants(current_frame->draw_command_buffer, renderer->pipeline_layout,
								   VK_SHADER_STAGE_ALL_GRAPHICS, 0, 16 * sizeof(float), &render_object.transform);
				vkCmdPushConstants(current_frame->draw_command_buffer, renderer->pipeline_layout,
								   VK_SHADER_STAGE_ALL_GRAPHICS, 16 * sizeof(float), 4, &render_object.material_id);
				vkCmdDrawIndexed(current_frame->draw_command_buffer, mesh.indices_count, 1, 0, 0, 1);
			}
		}
		command_buffer_region_end(current_frame->draw_command_buffer);
		vkCmdEndRendering(current_frame->draw_command_buffer);
	}
	command_buffer_region_end(current_frame->draw_command_buffer);

	command_buffer_region_begin(current_frame->draw_command_buffer, "ImGui draw pass");
	{
		ZoneScopedN("ImGui draw pass");

		VkRenderingAttachmentInfo imgui_pass_swapchain_attachment_info = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = swapchain_image.view,
			.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.resolveMode = VK_RESOLVE_MODE_NONE,
			.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		};

		VkRenderingInfo imgui_pass_rendering_info = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
			.flags = 0,
			.renderArea = {
				.offset = {}, // Zero
				.extent = gfx_context->swapchain.extent,
			},
			.layerCount = 1,
			.viewMask = 0,
			.colorAttachmentCount = 1,
			.pColorAttachments = &imgui_pass_swapchain_attachment_info,
		};
		vkCmdBeginRendering(current_frame->draw_command_buffer, &imgui_pass_rendering_info);
		{
			ImGui::Render();
			ImDrawData *draw_data = ImGui::GetDrawData();
			ImGui_ImplVulkan_RenderDrawData(draw_data, current_frame->draw_command_buffer);
		}
		vkCmdEndRendering(current_frame->draw_command_buffer);
	}
	command_buffer_region_end(current_frame->draw_command_buffer);

	{
		ZoneScopedN("Transition to present layout");

		VkImageSubresourceRange range = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		};

		VkImageMemoryBarrier present_transition_barrier{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = swapchain_image.image,
			.subresourceRange = range,
		};

		vkCmdPipelineBarrier(
			current_frame->draw_command_buffer,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &present_transition_barrier);
	}

	vkEndCommandBuffer(current_frame->draw_command_buffer);

	{
		ZoneScopedN("Submit draw");

		VkSemaphoreSubmitInfo wait_info[] = {
			{
				.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
				.semaphore = current_frame->acquire_semaphore,
				.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
			},
			{
				.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
				.semaphore = renderer->upload_semaphore,
				.value     = previous_timeline_frame_i,
				.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
			},
		};

		VkCommandBufferSubmitInfo command_buffer_submit_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
			.commandBuffer = current_frame->draw_command_buffer,
		};

		VkSemaphoreSubmitInfo signal_info[] = {
			{
				.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
				.semaphore = current_frame->acquire_semaphore,
				.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
			},
			{
				.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
				.semaphore = renderer->render_semaphore,
				.value     = current_timeline_frame_i,
				.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
			},
		};

		VkSubmitInfo2 submit_info = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
			.waitSemaphoreInfoCount   = 2,
			.pWaitSemaphoreInfos      = wait_info,
			.commandBufferInfoCount   = 1,
			.pCommandBufferInfos      = &command_buffer_submit_info,
			.signalSemaphoreInfoCount = 2,
			.pSignalSemaphoreInfos    = signal_info,
		};
		vkQueueSubmit2(gfx_context->gfx_queue, 1, &submit_info, VK_NULL_HANDLE);
	}

	{
		ZoneScopedN("Submit present");

		VkPresentInfoKHR present_info = {
			.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores    = &current_frame->acquire_semaphore,
			.swapchainCount     = 1,
			.pSwapchains        = &gfx_context->swapchain.handle,
			.pImageIndices      = &swapchain_image_index,
		};
		vkQueuePresentKHR(gfx_context->gfx_queue, &present_info);
	}
}