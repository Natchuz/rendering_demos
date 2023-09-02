#include "renderer.h"

#include "common.h"
#include "application.h"
#include "vulkan_utilities.h"

#include <fstream>
#include <tiny_obj_loader.h>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <volk.h>

// Private functions
void load_scene_data();
std::vector<uint8_t> load_file(const char* file_path);

void renderer_create_descriptors();
void renderer_destroy_descriptors();
void renderer_create_shaders();
void renderer_destroy_shaders();
void renderer_create_pipeline();
void renderer_destroy_pipeline();
void renderer_create_sync_primitives();
void renderer_destroy_sync_primitives();

void texture_manager_init()
{
	texture_manager = new Texture_Manager{};
}

void texture_manager_deinit()
{
	delete texture_manager;
}

void renderer_init()
{
	renderer = new Renderer;

	texture_manager_init();
	depth_buffer_create();
	renderer_create_frame_data();
	renderer_create_buffers();
	load_scene_data();
	renderer_create_descriptors();
	renderer_create_shaders();
	renderer_create_pipeline();
	renderer_create_sync_primitives();
}

void renderer_deinit()
{
	vkDeviceWaitIdle(gfx_context->device);

	renderer_destroy_sync_primitives();
	renderer_destroy_pipeline();
	renderer_destroy_shaders();
	renderer_destroy_descriptors();
	renderer_destroy_buffers();
	renderer_destroy_frame_data();
	depth_buffer_destroy();
	texture_manager_deinit();

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
			.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
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

void renderer_create_buffers()
{
	ZoneScopedN("Buffers creation");

	// Vertex buffer
	{
		ZoneScopedN("Vertex buffer creation");

		VkBufferCreateInfo vertex_buffer_create_info = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size  = 100000,
			.usage = VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		};

		VmaAllocationCreateInfo vma_vertex_buffer_create_info = {
			.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
			.usage = VMA_MEMORY_USAGE_AUTO,
		};

		vmaCreateBuffer(gfx_context->vma_allocator,
						&vertex_buffer_create_info,
						&vma_vertex_buffer_create_info,
						&renderer->vertex_buffer.buffer,
						&renderer->vertex_buffer.allocation,
						nullptr);
		vmaMapMemory(gfx_context->vma_allocator, renderer->vertex_buffer.allocation, &renderer->vertex_buffer_ptr);

		name_object(renderer->vertex_buffer.buffer, "Vertex buffer");
	}

	// Uniform buffer
	{
		ZoneScopedN("Uniform buffer creation");

		auto size = clamp_size_to_alignment(
			sizeof(Frame_Uniform_Data),
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
						&renderer->per_frame_data_buffer.buffer,
						&renderer->per_frame_data_buffer.allocation,
						nullptr);
		name_object(renderer->per_frame_data_buffer.buffer, "Frame data uniform buffer");
	}
}

void renderer_destroy_buffers()
{
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

void load_scene_data()
{
	ZoneScopedN("Loading scene data");

	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn, err;
	auto filename = "assets/colored_suzanne/suzanne.obj";

	tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filename);

	if (!warn.empty())
	{
		spdlog::warn(warn);
	}

	if (!err.empty())
	{
		throw std::runtime_error(err);
	}

	std::vector<float> vertex_data = {};
	renderer->vertices_count = 0;

	for (auto &shape : shapes)
	{
		size_t index_offset = 0;

		for (size_t face = 0; face < shape.mesh.num_face_vertices.size(); face++)
		{
			for (size_t vert = 0; vert < 3; vert++)
			{
				tinyobj::index_t idx = shape.mesh.indices[index_offset + vert];

				tinyobj::real_t vx = attrib.vertices[3 * idx.vertex_index + 0];
				tinyobj::real_t vy = attrib.vertices[3 * idx.vertex_index + 1];
				tinyobj::real_t vz = attrib.vertices[3 * idx.vertex_index + 2];
				tinyobj::real_t nx = attrib.normals[3 * idx.normal_index + 0];
				tinyobj::real_t ny = attrib.normals[3 * idx.normal_index + 1];
				tinyobj::real_t nz = attrib.normals[3 * idx.normal_index + 2];
				tinyobj::real_t u  = attrib.texcoords[2 * idx.texcoord_index + 0];
				tinyobj::real_t v  = attrib.texcoords[2 * idx.texcoord_index + 1];

				vertex_data.insert(vertex_data.end(), {vx, vy, vz, nx, ny, nz, u, 1-v}); // Vulkan UV fix
				renderer->vertices_count += 1;
			}

			index_offset += 3;
		}
	}

	spdlog::info("Loaded object, with {} vertices ({} bytes)", renderer->vertices_count, vertex_data.size() * sizeof(float));
	memcpy(renderer->vertex_buffer_ptr, vertex_data.data(), vertex_data.size() * sizeof(float));
	vmaFlushAllocation(gfx_context->vma_allocator, renderer->vertex_buffer.allocation, 0, vertex_data.size() * sizeof(float));

	// Texture

	auto texture = load_file("assets/colored_suzanne/ColorTexture.png");
	int32_t width, height, channels;
	auto pixels = stbi_load_from_memory(texture.data(), texture.size(), &width, &height, &channels, STBI_rgb_alpha);

	VkImageCreateInfo image_create_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = VK_FORMAT_R8G8B8A8_SRGB,
		.extent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	VmaAllocationCreateInfo allocation_create_info = {
		.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
	};

	AllocatedImage image = {};
	VmaAllocationInfo allocation_info;
	vmaCreateImage(gfx_context->vma_allocator, &image_create_info, &allocation_create_info,
				   &image.handle, &image.allocation, &allocation_info);
	name_object(image.handle, "Color texture");

	VkImageViewCreateInfo image_view_create_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = image.handle,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = VK_FORMAT_R8G8B8A8_SRGB,
		.components = {
			.r = VK_COMPONENT_SWIZZLE_IDENTITY,
			.g = VK_COMPONENT_SWIZZLE_IDENTITY,
			.b = VK_COMPONENT_SWIZZLE_IDENTITY,
			.a = VK_COMPONENT_SWIZZLE_IDENTITY,
		},
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
	};
	VkImageView image_view;
	vkCreateImageView(gfx_context->device, &image_view_create_info, nullptr, &image_view);

	VkSamplerCreateInfo sampler_create_info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.mipLodBias = 0,
		.anisotropyEnable = false,
		.maxAnisotropy = 16,
		.compareEnable = false,
		.compareOp = VK_COMPARE_OP_NEVER,
		.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE,
		.unnormalizedCoordinates = false,
	};
	VkSampler sampler;
	vkCreateSampler(gfx_context->device, &sampler_create_info, nullptr, &sampler);

	texture_manager->bindings.push_back({
											   .image = image,
											   .view = image_view,
											   .sampler = sampler,
										   });
	texture_manager->upload_queue.push_back({
											 .pixels = pixels,
											 .image_size = { static_cast<uint32_t>(width), static_cast<uint32_t>(height) },
											 .image_handle = static_cast<Texture_Manager::Texture_Handle>(texture_manager->bindings.size() - 1),
										 });
}

void renderer_create_descriptors()
{
	ZoneScopedN("Descriptors creation");

	// Create descriptor set layout
	{
		ZoneScopedN("Layout creation");

		VkDescriptorSetLayoutBinding set_layout_bindings[] = {
			{
				.binding         = 0,
				.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				.descriptorCount = 1,
				.stageFlags      = VK_SHADER_STAGE_ALL_GRAPHICS,
			},
			{
				.binding         = 1,
				.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 10,
				.stageFlags      = VK_SHADER_STAGE_ALL_GRAPHICS,
			},
		};

		VkDescriptorBindingFlags flags[] = {0, VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT};

		VkDescriptorSetLayoutBindingFlagsCreateInfo layout_flags_create_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
			.bindingCount  = 2,
			.pBindingFlags = flags,
		};

		VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.pNext = &layout_flags_create_info,
			.flags = 0,
			.bindingCount = 2,
			.pBindings    = set_layout_bindings,
		};

		vkCreateDescriptorSetLayout(gfx_context->device, &descriptor_set_layout_create_info, nullptr,
									&renderer->per_frame_descriptor_set_layout);
		name_object(renderer->per_frame_descriptor_set_layout, "per_frame_descriptor_set_layout");
	}

	// Create descriptor pool
	{
		ZoneScopedN("Pool creation");

		VkDescriptorPoolSize pool_sizes[] = {
			{
				.type  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				.descriptorCount = 10,
			},
			{
				.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 100,
			},
		};

		VkDescriptorPoolCreateInfo descriptor_pool_create_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.flags         = 0,
			.maxSets       = 10,
			.poolSizeCount = 2,
			.pPoolSizes    = pool_sizes,
		};

		vkCreateDescriptorPool(gfx_context->device, &descriptor_pool_create_info, nullptr, &renderer->descriptor_pool);
		name_object(renderer->descriptor_pool, "Main descriptor pool");
	}

	// Allocate descriptor set
	{
		ZoneScopedN("Descriptor set allocation");

		uint32_t counts[] = { 1 };

		VkDescriptorSetVariableDescriptorCountAllocateInfo descriptor_variable_size_allocate_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
			.descriptorSetCount = 1,
			.pDescriptorCounts  = counts,
		};

		VkDescriptorSetAllocateInfo descriptor_set_allocate_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.pNext = &descriptor_variable_size_allocate_info,
			.descriptorPool     = renderer->descriptor_pool,
			.descriptorSetCount = 1,
			.pSetLayouts        = &renderer->per_frame_descriptor_set_layout,
		};

		vkAllocateDescriptorSets(gfx_context->device, &descriptor_set_allocate_info,
								 &renderer->per_frame_descriptor_set);
		name_object(renderer->per_frame_descriptor_set, "per_frame_descriptor_set");
	}

	// Temporary write to descriptor
	{
		ZoneScopedN("Temporary write to descriptor");

		VkDescriptorBufferInfo descriptor_buffer_info = {
			.buffer = renderer->per_frame_data_buffer.buffer,
			.offset = 0,
			.range  = sizeof(Frame_Uniform_Data),
		};

		VkDescriptorImageInfo image_info = {
			.sampler     = texture_manager->bindings[0].sampler,
			.imageView   = texture_manager->bindings[0].view,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};

		VkWriteDescriptorSet descriptor_set_writes[] = {
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = renderer->per_frame_descriptor_set,
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				.pBufferInfo = &descriptor_buffer_info,
			},
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = renderer->per_frame_descriptor_set,
				.dstBinding = 1,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &image_info,
			}
		};

		vkUpdateDescriptorSets(gfx_context->device, 2, descriptor_set_writes, 0, nullptr);
	}
}

void renderer_destroy_descriptors()
{
	ZoneScopedN("Descriptors destruction");
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

		VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.flags = 0,
			.setLayoutCount = 1,
			.pSetLayouts = &renderer->per_frame_descriptor_set_layout,
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
			.stride    = 8 * sizeof(float),
			.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
		};

		VkVertexInputAttributeDescription vertex_attributes[3];

		vertex_attributes[0] = { // Position attribute
			.location = 0,
			.binding  = 0,
			.format   = VK_FORMAT_R32G32B32_SFLOAT,
			.offset   = 0,
		};

		vertex_attributes[1] = { // Normal attribute
			.location = 1,
			.binding  = 0,
			.format   = VK_FORMAT_R32G32B32_SFLOAT,
			.offset   = 3 * sizeof(float),
		};

		vertex_attributes[2] = { // UV attribute
			.location = 2,
			.binding  = 0,
			.format   = VK_FORMAT_R32G32_SFLOAT,
			.offset   = 6 * sizeof(float),
		};

		VkPipelineVertexInputStateCreateInfo vertex_input_state = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
			.vertexBindingDescriptionCount   = 1,
			.pVertexBindingDescriptions      = &binding_description,
			.vertexAttributeDescriptionCount = 3,
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
		sizeof(Frame_Uniform_Data),
		gfx_context->physical_device_properties.properties.limits.minUniformBufferOffsetAlignment) * frame_i;

	// Begin recording
	VkCommandBufferBeginInfo upload_begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	vkBeginCommandBuffer(current_frame->upload_command_buffer, &upload_begin_info); // Implicit reset
	command_buffer_region_begin(current_frame->upload_command_buffer, "Upload stage");

	// Enqueue upload of all pending textures
	{
		ZoneScopedN("Upload image data");

		for (auto upload_data: texture_manager->upload_queue)
		{
			auto image_size = upload_data.image_size.width * upload_data.image_size.height * 4;

			memcpy(staging_buffer_ptr + linear_allocator, upload_data.pixels, image_size);
			stbi_image_free(upload_data.pixels); // Pixels are no longer required on RAM

			// Transition to copy layout
			VkImageMemoryBarrier to_transfer_transition_barrier = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = 0,
				.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
				.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				.image         = texture_manager->bindings[upload_data.image_handle].image.handle,
				.subresourceRange = {
					.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel   = 0,
					.levelCount     = 1,
					.baseArrayLayer = 0,
					.layerCount     = 1,
				},
			};
			vkCmdPipelineBarrier(current_frame->upload_command_buffer, VK_PIPELINE_STAGE_HOST_BIT,
								 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
								 &to_transfer_transition_barrier);

			// Enqueue copy
			VkBufferImageCopy region = {
				.imageSubresource = {
					.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
					.mipLevel       = 0,
					.baseArrayLayer = 0,
					.layerCount     = 1,
				},
				.imageExtent = {
					.width   = upload_data.image_size.width,
					.height  = upload_data.image_size.height,
					.depth   = 1,
				},
			};
			vkCmdCopyBufferToImage(current_frame->upload_command_buffer, current_frame->staging_buffer.buffer,
								   texture_manager->bindings[upload_data.image_handle].image.handle,
								   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
			linear_allocator += image_size;

			// Immediately transition into proper layout
			VkImageMemoryBarrier from_transform_transition_barrier = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
				.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.image         = texture_manager->bindings[upload_data.image_handle].image.handle,
				.subresourceRange = {
					.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel   = 0,
					.levelCount     = 1,
					.baseArrayLayer = 0,
					.layerCount     = 1,
				},
			};
			vkCmdPipelineBarrier(current_frame->upload_command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
								 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
								 &from_transform_transition_barrier);
		}
		texture_manager->upload_queue.clear();
	}

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

		auto uniform_data = reinterpret_cast<Frame_Uniform_Data*>(staging_buffer_ptr + linear_allocator);

		uniform_data->render_matrix = render_matrix;

		// Upload
		VkBufferCopy region = {
			.srcOffset = linear_allocator,
			.dstOffset = current_per_frame_data_buffer_offset,
			.size      = sizeof(Frame_Uniform_Data),
		};
		vkCmdCopyBuffer(current_frame->upload_command_buffer, current_frame->staging_buffer.buffer,
						renderer->per_frame_data_buffer.buffer, 1, &region);

		linear_allocator += sizeof(Frame_Uniform_Data);
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

		VkClearValue color_clear_value = {.color = {.float32 = {0, 0, 0.2, 1}}};

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
									1, &renderer->per_frame_descriptor_set,
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

			VkDeviceSize offsets = 0;
			vkCmdBindVertexBuffers(current_frame->draw_command_buffer, 0, 1, &renderer->vertex_buffer.buffer, &offsets);
			vkCmdDraw(current_frame->draw_command_buffer, renderer->vertices_count, 1, 0, 0);
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