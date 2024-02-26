#include "renderer.h"

#include "common.h"
#include "application.h"
#include "vulkan_utilities.h"

#include <fstream>
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
void renderer_init_shadow_pass();

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

void Debug_Pass::draw_line(glm::vec3 from, glm::vec3 to, glm::vec3 color)
{
	draws.push_back({from, to, color});
}

#define PI (22.f / 7.f)

void Debug_Pass::draw_sphere(glm::vec3 center_pos, float radius, uint32_t rings, uint32_t slices, glm::vec3 color)
{
	glm::mat4x4 model = glm::translate(glm::identity<glm::mat4x4>(), center_pos)
		* glm::scale(glm::identity<glm::mat4x4>(), glm::vec3(radius));

	for (int i = 0; i < (rings + 2); i++)
	{
		for (int j = 0; j < slices; j++)
		{
			const auto DEG2RAD = (PI / 180.f);
			glm::vec3 first, second;

			first.x  = cos(DEG2RAD * (270.f + (180.f / (rings + 1.f)) * i)) * sin(DEG2RAD * (360.0 * j / slices));
			first.y  = sin(DEG2RAD * (270.f + (180.f / (rings + 1.f)) * i));
			first.z  = cos(DEG2RAD * (270.f + (180.f / (rings + 1.f)) * i)) * cos(DEG2RAD * (360.0 * j / slices));
			second.x = cos(DEG2RAD*(270 + (180.0/(rings + 1))*(i + 1)))*sin(DEG2RAD*(360.0*(j + 1)/slices));
			second.y = sin(DEG2RAD*(270 + (180.0/(rings + 1))*(i + 1)));
			second.z = cos(DEG2RAD*(270 + (180.0/(rings + 1))*(i + 1)))*cos(DEG2RAD*(360.0*(j + 1)/slices));
			first    = model * glm::vec4(first, 1.0);
			second   = model * glm::vec4(second, 1.0);
			this->draw_line(first, second, color);

			first.x  = cos(DEG2RAD*(270 + (180.0/(rings + 1))*(i + 1)))*sin(DEG2RAD*(360.0*(j + 1)/slices));
			first.y  = sin(DEG2RAD*(270 + (180.0/(rings + 1))*(i + 1)));
			first.z  = cos(DEG2RAD*(270 + (180.0/(rings + 1))*(i + 1)))*cos(DEG2RAD*(360.0*(j + 1)/slices));
			second.x = cos(DEG2RAD*(270 + (180.0/(rings + 1))*(i + 1)))*sin(DEG2RAD*(360.0*j/slices));
			second.y = sin(DEG2RAD*(270 + (180.0/(rings + 1))*(i + 1)));
			second.z = cos(DEG2RAD*(270 + (180.0/(rings + 1))*(i + 1)))*cos(DEG2RAD*(360.0*j/slices));
			first    = model * glm::vec4(first, 1.0);
			second   = model * glm::vec4(second, 1.0);
			this->draw_line(first, second, color);

			first.x  = cos(DEG2RAD*(270 + (180.0/(rings + 1))*(i + 1)))*sin(DEG2RAD*(360.0*j/slices));
			first.y  = sin(DEG2RAD*(270 + (180.0/(rings + 1))*(i + 1)));
			first.z  = cos(DEG2RAD*(270 + (180.0/(rings + 1))*(i + 1)))*cos(DEG2RAD*(360.0*j/slices));
			second.x = cos(DEG2RAD*(270 + (180.0/(rings + 1))*i))*sin(DEG2RAD*(360.0*j/slices));
			second.y = sin(DEG2RAD*(270 + (180.0/(rings + 1))*i));
			second.z = cos(DEG2RAD*(270 + (180.0/(rings + 1))*i))*cos(DEG2RAD*(360.0*j/slices));
			first    = model * glm::vec4(first, 1.0);
			second   = model * glm::vec4(second, 1.0);
			this->draw_line(first, second, color);
		}
	}
}

void debug_pass_init()
{
	debug_pass = new Debug_Pass;

	{
		VkBufferCreateInfo creation_info = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size  = 5000000,
			.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		};

		VmaAllocationCreateInfo vma_creation_info = {
			.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
			.usage = VMA_MEMORY_USAGE_AUTO,
		};

		vmaCreateBuffer(gfx_context->vma_allocator,
						&creation_info,
						&vma_creation_info,
						&debug_pass->vertex_buffer.buffer,
						&debug_pass->vertex_buffer.allocation,
						nullptr);
		name_object(debug_pass->vertex_buffer.buffer, "Debug vertex buffer");
	}

	{
		auto vert_shader_code = load_file("data/shaders/line_vert.spv");
		VkShaderModuleCreateInfo vert_shader_create_info = {
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.codeSize = vert_shader_code.size(),
			.pCode    = reinterpret_cast<const uint32_t *>(vert_shader_code.data()),
		};
		vkCreateShaderModule(gfx_context->device, &vert_shader_create_info, nullptr, &debug_pass->vertex_shader);
		name_object(debug_pass->vertex_shader, "Vertex line shader");

		auto frag_shader_code = load_file("data/shaders/line_frag.spv");
		VkShaderModuleCreateInfo frag_shader_create_info = {
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.codeSize = frag_shader_code.size(),
			.pCode = reinterpret_cast<const uint32_t *>(frag_shader_code.data()),
		};
		vkCreateShaderModule(gfx_context->device, &frag_shader_create_info, nullptr, &debug_pass->fragment_shader);
		name_object(debug_pass->fragment_shader, "Fragment line shader");
	}

	// Pipeline layout
	{
		ZoneScopedN("Pipeline layout creation");

		VkPushConstantRange push_constant_range = {
			.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
			.offset     = 0,
			.size       = (16 + 3) * sizeof(float),
		};

		VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.flags = 0,
			.setLayoutCount         = 0,
			.pSetLayouts            = nullptr,
			.pushConstantRangeCount = 1,
			.pPushConstantRanges    = &push_constant_range,
		};
		vkCreatePipelineLayout(gfx_context->device, &pipeline_layout_create_info,
							   nullptr, &debug_pass->pipeline_layout);
		name_object(debug_pass->pipeline_layout, "Debug pipeline layout");
	}

	// Pipeline
	{
		VkPipelineShaderStageCreateInfo vert_stage = {
			.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage  = VK_SHADER_STAGE_VERTEX_BIT,
			.module = debug_pass->vertex_shader,
			.pName  = "main",
		};
		VkPipelineShaderStageCreateInfo frag_stage = {
			.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = debug_pass->fragment_shader,
			.pName  = "main",
		};
		VkPipelineShaderStageCreateInfo stages[2] = {vert_stage, frag_stage};

		VkVertexInputBindingDescription binding_description = {
			.binding   = 0,
			.stride    = 3 * sizeof(float),
			.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
		};

		VkVertexInputAttributeDescription vertex_attributes[] = {
			{ // Position attribute
				.location = 0,
				.binding  = 0,
				.format   = VK_FORMAT_R32G32B32_SFLOAT,
				.offset   = 0,
			},
		};

		VkPipelineVertexInputStateCreateInfo vertex_input_state = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
			.vertexBindingDescriptionCount   = 1,
			.pVertexBindingDescriptions      = &binding_description,
			.vertexAttributeDescriptionCount = 1,
			.pVertexAttributeDescriptions    = vertex_attributes,
		};

		VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
			.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
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
			.layout              = debug_pass->pipeline_layout,
			.renderPass          = VK_NULL_HANDLE,
			.subpass             = 0,
			.basePipelineHandle  = VK_NULL_HANDLE,
		};

		vkCreateGraphicsPipelines(gfx_context->device, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr,
								  &debug_pass->pipeline);
		name_object(debug_pass->pipeline, "Debug pass line pipeline");
	}
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

	scene_data = new Scene_Data { .sun = {glm::vec3(0, 0, 1), 1.0f } };
	debug_pass_init();
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
	load_scene_data();
	renderer_init_shadow_pass();
}

void renderer_deinit()
{
	vkDeviceWaitIdle(gfx_context->device);

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

	{
		ZoneScopedN("Shadow map creation");

		VkImageCreateInfo image_create_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.flags         = 0,
			.imageType     = VK_IMAGE_TYPE_2D,
			.format        = VK_FORMAT_D32_SFLOAT,
			.extent        = { 2048, 2048, 1 },
			.mipLevels     = 1,
			.arrayLayers   = 1,
			.samples       = VK_SAMPLE_COUNT_1_BIT,
			.tiling        = VK_IMAGE_TILING_OPTIMAL,
			.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			.sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		};

		VmaAllocationCreateInfo vma_allocation_info = {
			.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
		};

		for (int frame_i=0; frame_i < renderer->buffering; frame_i++)
		{
			auto frame_data = &renderer->frame_data[frame_i]; // Shortcut

			vmaCreateImage(gfx_context->vma_allocator, &image_create_info, &vma_allocation_info,
						   &frame_data->sun_shadow_map.image, &frame_data->sun_shadow_map.allocation,
						   nullptr);

			VkImageViewCreateInfo image_view_create_info = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.image      = frame_data->sun_shadow_map.image,
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

			vkCreateImageView(gfx_context->device, &image_view_create_info, nullptr,
							  &frame_data->sun_shadow_map.view);

			texture_manager->images.push_back(frame_data->sun_shadow_map);
		}
	}
}

void renderer_destroy_frame_data()
{
	ZoneScopedN("Frame data destruction");

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

void renderer_init_shadow_pass()
{
	{
		ZoneScopedN("Shader creation");

		// If Spir-V shader is valid, casting bytes to 32-bit words shouldn't matter
		auto vert_shader_code = load_file("data/shaders/shadow_pass_vert.spv");
		VkShaderModuleCreateInfo vert_shader_create_info = {
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.codeSize = vert_shader_code.size(),
			.pCode    = reinterpret_cast<const uint32_t *>(vert_shader_code.data()),
		};
		vkCreateShaderModule(gfx_context->device, &vert_shader_create_info, nullptr, &renderer->shadow_pass.vertex_shader);
		name_object(renderer->shadow_pass.vertex_shader, "Vertex shader");

		auto frag_shader_code = load_file("data/shaders/shadow_pass_frag.spv");
		VkShaderModuleCreateInfo frag_shader_create_info = {
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.codeSize = frag_shader_code.size(),
			.pCode = reinterpret_cast<const uint32_t *>(frag_shader_code.data()),
		};
		vkCreateShaderModule(gfx_context->device, &frag_shader_create_info, nullptr, &renderer->shadow_pass.fragment_shader);
		name_object(renderer->shadow_pass.fragment_shader, "Fragment shader");
	}

	// Pipeline layout
	{
		ZoneScopedN("Pipeline layout creation");

		VkPushConstantRange push_constant_range = {
			.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
			.offset     = 0,
			.size       = 32 * sizeof(float), // Projection matrix + model matrix
		};

		VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.flags = 0,
			.setLayoutCount         = 0,
			.pSetLayouts            = nullptr,
			.pushConstantRangeCount = 1,
			.pPushConstantRanges    = &push_constant_range,
		};
		vkCreatePipelineLayout(gfx_context->device, &pipeline_layout_create_info,
							   nullptr, &renderer->shadow_pass.pipeline_layout);
		name_object(renderer->shadow_pass.pipeline_layout, "Shadow pass layout");
	}

	// Pipeline
	{
		VkPipelineShaderStageCreateInfo vert_stage = {
			.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage  = VK_SHADER_STAGE_VERTEX_BIT,
			.module = renderer->shadow_pass.vertex_shader,
			.pName  = "main",
		};
		VkPipelineShaderStageCreateInfo frag_stage = {
			.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = renderer->shadow_pass.fragment_shader,
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
		};

		VkPipelineVertexInputStateCreateInfo vertex_input_state = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
			.vertexBindingDescriptionCount   = 1,
			.pVertexBindingDescriptions      = &binding_description,
			.vertexAttributeDescriptionCount = 1,
			.pVertexAttributeDescriptions    = vertex_attributes,
		};

		VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
			.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
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

		VkPipelineDynamicStateCreateInfo dynamic_state = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
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
			.colorAttachmentCount    = 0,
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

		VkViewport viewport = {
			.x        = 0,
			.y        = (float) 2048,
			.width    = (float) 2048,
			.height   = -1 * (float) 2048,
			.minDepth = 0.0f,
			.maxDepth = 1.0f,
		};
		VkRect2D scissor = { .offset = {}, .extent = { 2048, 2048 } };

		VkPipelineViewportStateCreateInfo viewport_state = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
			.viewportCount = 1,
			.pViewports    = &viewport,
			.scissorCount  = 1,
			.pScissors     = &scissor,
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
			.layout              = renderer->shadow_pass.pipeline_layout,
			.renderPass          = VK_NULL_HANDLE,
			.subpass             = 0,
			.basePipelineHandle  = VK_NULL_HANDLE,
		};

		vkCreateGraphicsPipelines(gfx_context->device, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr,
								  &renderer->shadow_pass.pipeline);
		name_object(renderer->shadow_pass.pipeline, "Shadow pass line pipeline");
	}
}

Upload_Heap::Upload_Heap(size_t initial_size)
{
	frame_number = -1;
	delete_queue = {};

	VkBufferCreateInfo buffer_create_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = initial_size,
		.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	};

	VmaAllocationCreateInfo buffer_vma_create_info = {
		.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
	};

	VmaAllocationInfo allocation_info;
	vmaCreateBuffer(gfx_context->vma_allocator,
	                &buffer_create_info,
	                &buffer_vma_create_info,
	                &upload_buffer.buffer,
	                &upload_buffer.allocation,
	                &allocation_info);
	upload_buffer_ptr = allocation_info.pMappedData;

	VmaVirtualBlockCreateInfo virtual_block_create_info = { .size = initial_size };
	vmaCreateVirtualBlock(&virtual_block_create_info, &virtual_block);
}

Upload_Heap::~Upload_Heap()
{
	//vmaUnmapMemory(gfx_context->vma_allocator, upload_buffer.allocation);
	vmaDestroyBuffer(gfx_context->vma_allocator, upload_buffer.buffer, upload_buffer.allocation);
}

void Upload_Heap::begin_frame()
{
	frame_number++;
	
	if (delete_queue.size() == 0) return;

	// Process deletes
	for (; delete_queue.front().frame < frame_number - 3; delete_queue.pop_front())
	{
		Free_Slot& slot = delete_queue.front();
		vmaVirtualFree(virtual_block, slot.block.allocation);
	}
}

Upload_Heap::Block Upload_Heap::allocate_block(size_t size, size_t alignment)
{
	VmaVirtualAllocationCreateInfo virtual_allocation_create_info = { 
		.size      = size, 
		.alignment = alignment,
		.flags     = VMA_VIRTUAL_ALLOCATION_CREATE_STRATEGY_MIN_TIME_BIT, // Want fast allocation
	};

	VmaVirtualAllocation allocation;
	VkDeviceSize         offset;
	VkResult result = vmaVirtualAllocate(virtual_block, &virtual_allocation_create_info, &allocation, &offset);

	if (result != VK_SUCCESS) spdlog::error("Could not sub-allocate upload heap"); // TODO fallback to crreating new pool

	Upload_Heap::Block block = { 
		.allocation = allocation,
		.offset     = offset,
		.size       = size,
		.ptr        = static_cast<char*>(upload_buffer_ptr) + offset,
	};
	return block;
}

void Upload_Heap::submit_free(Upload_Heap::Block block)
{
	Free_Slot free_slot = { .block = block, .frame = frame_number };
	delete_queue.push_back(free_slot);

	vmaFlushAllocation(gfx_context->vma_allocator, upload_buffer.allocation, block.offset, block.size);
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

	glm::mat4 view = camera_get_view_matrix();
	glm::mat4 projection = glm::perspective(
		glm::radians(70.f),
		1280.f / 720.f,
		0.1f,
		200.0f);
	glm::mat4 render_matrix = projection * view;

	size_t current_debug_pass_vertex_buffer_offset = 1000000 * frame_i;

	// Build and stage per-frame data
	{
		Upload_Heap::Block block = renderer->upload_heap.allocate_block(sizeof(Global_Uniform_Data));

		Global_Uniform_Data uniform_data = {};
		uniform_data.render_matrix = render_matrix;
		uniform_data.sun           = scene_data->sun;
		uniform_data.active_lights = std::clamp(scene_data->point_lights.size(), 0ULL, 16ULL);
		std::copy(scene_data->point_lights.begin(), scene_data->point_lights.begin() + uniform_data.active_lights,
				  uniform_data.point_lights);

		memcpy(block.ptr, &uniform_data, sizeof(Global_Uniform_Data));

		// Upload
		VkBufferCopy region = {
			.srcOffset = block.offset,
			.dstOffset = current_per_frame_data_buffer_offset,
			.size      = sizeof(Global_Uniform_Data),
		};
		vkCmdCopyBuffer(current_frame->upload_command_buffer, renderer->upload_heap.upload_buffer.buffer,
						renderer->global_uniform_data_buffer.buffer, 1, &region);

		renderer->upload_heap.submit_free(block);
	}

	// Debug pass data
	{
		if (!debug_pass->draws.empty())
		{
			size_t size = debug_pass->draws.size() * 24; // Total size of debug data
			Upload_Heap::Block block = renderer->upload_heap.allocate_block(size);
			Mapped_Buffer_Writer writer(block.ptr);

			for (auto& draw : debug_pass->draws)
			{
				writer.write(&draw.from, 12);
				writer.write(&draw.to, 12);
			}

			VkBufferCopy region = {
				.srcOffset = block.offset,
				.dstOffset = current_debug_pass_vertex_buffer_offset,
				.size      = size,
			};
			vkCmdCopyBuffer(current_frame->upload_command_buffer, renderer->upload_heap.upload_buffer.buffer,
							debug_pass->vertex_buffer.buffer, 1, &region);

			renderer->upload_heap.submit_free(block);
		}
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
		ZoneScopedN("Transition shit");

		VkImageMemoryBarrier render_transition_barrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.dstAccessMask       = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout           = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
			.image               = current_frame->sun_shadow_map.image,
			.subresourceRange    = {
				.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT,
				.baseMipLevel   = 0,
				.levelCount     = 1,
				.baseArrayLayer = 0,
				.layerCount     = 1,
			},
		};

		vkCmdPipelineBarrier(
			current_frame->draw_command_buffer,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &render_transition_barrier);
	}

	{
		VkClearValue depth_clear_value = { .depthStencil = { .depth = 1 } };

		VkRenderingAttachmentInfo depth_attachment_info = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView   = current_frame->sun_shadow_map.view,
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
				.extent = {2048, 2048},
			},
			.layerCount = 1,
			.viewMask   = 0,
			.colorAttachmentCount = 0,
			.pDepthAttachment     = &depth_attachment_info,
		};

		{
			ZoneScopedN("Pipeline bind");
			vkCmdBindPipeline(current_frame->draw_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->shadow_pass.pipeline);
		}

		vkCmdBeginRendering(current_frame->draw_command_buffer, &rendering_info);
		command_buffer_region_begin(current_frame->draw_command_buffer, "Shadow map");
		{
			ZoneScopedN("Drawing");

			glm::vec3 position = glm::vec3(-9.f, 22.f, 3.f);
			glm::mat4 pos = glm::translate(glm::identity<glm::mat4>(), position);
			glm::mat4 proj = glm::ortho(0.0f, 800.0f, 0.0f, 600.0f, 0.1f, 100.0f);
			glm::mat4 light_space = proj * pos * glm::rotate(glm::identity<glm::mat4>(), 3.14f/2.f, glm::vec3(0.62, 0, 0.777));

			vkCmdPushConstants(current_frame->draw_command_buffer, renderer->shadow_pass.pipeline_layout,
							   VK_SHADER_STAGE_ALL_GRAPHICS, 16 * sizeof(float), 16 * sizeof(float), &light_space);

			for (auto& render_object : scene_data->render_objects)
			{
				Mesh_Manager::Mesh_Description mesh = mesh_manager->get_mesh(render_object.mesh_id);
				vkCmdBindVertexBuffers(current_frame->draw_command_buffer, 0, 1, &mesh_manager->vertex_buffer.buffer,
									   &mesh.vertex_offset);
				vkCmdBindIndexBuffer(current_frame->draw_command_buffer, mesh_manager->indices_buffer.buffer,
									 mesh.indices_offset, VK_INDEX_TYPE_UINT16);
				vkCmdPushConstants(current_frame->draw_command_buffer, renderer->shadow_pass.pipeline_layout,
								   VK_SHADER_STAGE_ALL_GRAPHICS, 0, 16 * sizeof(float), &render_object.transform);
				vkCmdDrawIndexed(current_frame->draw_command_buffer, mesh.indices_count, 1, 0, 0, 1);
			}
		}
		command_buffer_region_end(current_frame->draw_command_buffer);
		vkCmdEndRendering(current_frame->draw_command_buffer);
	}

	{
		ZoneScopedN("Transition shit");

		VkImageMemoryBarrier render_transition_barrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.oldLayout     = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
			.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.image               = current_frame->sun_shadow_map.image,
			.subresourceRange    = {
				.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT,
				.baseMipLevel   = 0,
				.levelCount     = 1,
				.baseArrayLayer = 0,
				.layerCount     = 1,
			},
		};

		vkCmdPipelineBarrier(
			current_frame->draw_command_buffer,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &render_transition_barrier);
	}

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

	command_buffer_region_begin(current_frame->draw_command_buffer, "Debug pass");
	{
		VkRenderingAttachmentInfo swapchain_attachment_info = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView   = swapchain_image.view,
			.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.resolveMode = VK_RESOLVE_MODE_NONE,
			.loadOp      = VK_ATTACHMENT_LOAD_OP_LOAD,
			.storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
		};

		VkRenderingAttachmentInfo depth_attachment_info = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView   = renderer->depth_buffer.view,
			.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
			.resolveMode = VK_RESOLVE_MODE_NONE,
			.loadOp      = VK_ATTACHMENT_LOAD_OP_LOAD,
			.storeOp     = VK_ATTACHMENT_STORE_OP_NONE,
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
			ZoneScopedN("Pipeline bind");
			vkCmdBindPipeline(current_frame->draw_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, debug_pass->pipeline);
		}

		vkCmdBeginRendering(current_frame->draw_command_buffer, &rendering_info);
		command_buffer_region_begin(current_frame->draw_command_buffer, "Rendering");
		{
			ZoneScopedN("Drawing");

			for (size_t draw_index = 0; draw_index < debug_pass->draws.size(); draw_index++)
			{
				Debug_Pass::Draws& draw = debug_pass->draws[draw_index];

				auto offset = current_debug_pass_vertex_buffer_offset + draw_index * sizeof(float) * 6; // 6 floats per draw
				vkCmdBindVertexBuffers(current_frame->draw_command_buffer, 0, 1,
									   &debug_pass->vertex_buffer.buffer, &offset);
				vkCmdPushConstants(current_frame->draw_command_buffer, debug_pass->pipeline_layout,
								   VK_SHADER_STAGE_ALL_GRAPHICS, 0, 16 * sizeof(float), &render_matrix);
				vkCmdPushConstants(current_frame->draw_command_buffer, debug_pass->pipeline_layout,
								   VK_SHADER_STAGE_ALL_GRAPHICS, 16 * sizeof(float), 12, glm::value_ptr(draw.color));
				vkCmdDraw(current_frame->draw_command_buffer, 2, 1, 0, 0);
			}
		}
		command_buffer_region_end(current_frame->draw_command_buffer);
		vkCmdEndRendering(current_frame->draw_command_buffer);

		debug_pass->draws.clear();
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