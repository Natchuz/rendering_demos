#pragma once

#include <vector>
#include <format>
#include <volk.h>
#include <vulkan/vulkan.h>

// Returns size of block that includes alignment
size_t clamp_size_to_alignment(size_t block_size, size_t alignment);

// Create basic image view based on image creation info
VkResult create_default_image_view(VkDevice device, VkImageCreateInfo& image_create_info, VkImage image,
								   VkAllocationCallbacks* allocation_callbacks, VkImageView* image_view);

// Helper class for dealing with descriptor allocation
struct Descriptor_Set_Allocator
{
	// Configure pools
	constexpr static inline uint32_t MAX_SETS = 1000;
	constexpr static inline VkDescriptorPoolSize pool_sizes[] = {
		{ VK_DESCRIPTOR_TYPE_SAMPLER,                500  },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8000 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          8000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,   1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,   1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         2000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         2000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,       500  },
	};

	VkDescriptorPoolCreateFlags   flags; // Specify this when needed on creation
	VkDescriptorPool              current_pool;
	std::vector<VkDescriptorPool> free_pools;
	std::vector<VkDescriptorPool> used_pools;

	void allocate(VkDevice device, VkDescriptorSetLayout layout, VkDescriptorSet* set);

	void create_reserve_pool(VkDevice device);
};

void descriptor_set_allocator_deinit(Descriptor_Set_Allocator* allocator, VkDevice device);

// VK_EXT_debug_utils helpers:

template <class... Args>
void command_buffer_region_begin(VkCommandBuffer buffer, std::format_string<Args...> fmt, Args&&... args)
{
	const auto formatted = std::vformat(fmt.get(), std::make_format_args(args...));
	VkDebugUtilsLabelEXT label_info = {
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
		.pLabelName = formatted.c_str(),
	};
	vkCmdBeginDebugUtilsLabelEXT(buffer, &label_info);
}

inline void command_buffer_region_end(VkCommandBuffer buffer)
{
	vkCmdEndDebugUtilsLabelEXT(buffer);
}

template <class... Args>
void command_buffer_insert_marker(VkCommandBuffer buffer, std::format_string<Args...> fmt, Args&&... args)
{
	const auto formatted = std::vformat(fmt.get(), std::make_format_args(args...));
	VkDebugUtilsLabelEXT label_info = {
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
		.pLabelName = formatted.c_str(),
	};
	vkCmdInsertDebugUtilsLabelEXT(buffer, &label_info);
}

template <class... Args>
void queue_region_begin(VkQueue queue, std::format_string<Args...> fmt, Args&&... args)
{
	const auto formatted = std::vformat(fmt.get(), std::make_format_args(args...));
	VkDebugUtilsLabelEXT label_info = {
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
		.pLabelName = formatted.c_str(),
	};
	vkQueueBeginDebugUtilsLabelEXT(queue, &label_info);
}

inline void queue_region_end(VkQueue queue)
{
	vkQueueEndDebugUtilsLabelEXT(queue);
}

template <class... Args>
void queue_insert_marker(VkQueue queue, std::format_string<Args...> fmt, Args&&... args)
{
	const auto formatted = std::vformat(fmt.get(), std::make_format_args(args...));
	VkDebugUtilsLabelEXT label_info = {
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
		.pLabelName = formatted.c_str(),
	};
	vkQueueInsertDebugUtilsLabelEXT(queue, &label_info);
}