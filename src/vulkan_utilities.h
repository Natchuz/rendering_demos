#pragma once

#include <format>
#include <volk.h>
#include <vulkan/vulkan.h>

// Returns size of block that includes alignment
size_t clamp_size_to_alignment(size_t block_size, size_t alignment);

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