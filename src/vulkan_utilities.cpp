#include "vulkan_utilities.h"

#include "common.h"

#include <volk.h>

size_t clamp_size_to_alignment(size_t block_size, size_t alignment)
{
	if (alignment > 0)
	{
		return (block_size + alignment - 1) & ~(alignment - 1);
	}
	return block_size;
}

VkResult create_default_image_view(VkDevice device, VkImageCreateInfo &image_create_info, VkImage image,
								   VkAllocationCallbacks* allocation_callbacks, VkImageView *image_view)
{
	VkImageViewCreateInfo image_view_create_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image      = image,
		.viewType   = (image_create_info.imageType == VK_IMAGE_TYPE_1D) ? VK_IMAGE_VIEW_TYPE_1D :
					  (image_create_info.imageType == VK_IMAGE_TYPE_2D) ? VK_IMAGE_VIEW_TYPE_2D :
					  VK_IMAGE_VIEW_TYPE_3D,
		.format     = image_create_info.format,
		.components = {},
		.subresourceRange = {
			.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel   = 0,
			.levelCount     = image_create_info.mipLevels,
			.baseArrayLayer = 0,
			.layerCount     = image_create_info.arrayLayers,
		},
	};
	return vkCreateImageView(device, &image_view_create_info, allocation_callbacks, image_view);
}

void Descriptor_Set_Allocator::allocate(VkDevice device, VkDescriptorSetLayout layout, VkDescriptorSet *descriptor_set)
{
	ZoneScopedN("Descriptor allocation");

	// Grab pool if none is in use
	if (current_pool == VK_NULL_HANDLE)
	{
		if (free_pools.empty())
			create_reserve_pool(device);
		current_pool = free_pools.back();
		free_pools.pop_back();
		used_pools.push_back(current_pool);
	}

	VkDescriptorSetAllocateInfo allocate_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool     = current_pool,
		.descriptorSetCount = 1,
		.pSetLayouts        = &layout,
	};

	VkResult result = vkAllocateDescriptorSets(device, &allocate_info, descriptor_set);

	if (result == VK_SUCCESS) return;

	if (result == VK_ERROR_FRAGMENTED_POOL || result == VK_ERROR_OUT_OF_POOL_MEMORY)
	{
		// Grab one more pool, or create if necessary
		if (free_pools.empty())
			create_reserve_pool(device);
		current_pool = free_pools.back();
		free_pools.pop_back();
		used_pools.push_back(current_pool);

		VkResult retry_result = vkAllocateDescriptorSets(device, &allocate_info, descriptor_set);
		if (retry_result == VK_SUCCESS) return; // Success. Otherwise, problems.
	}

	// If not SUCCESS, FRAGMENTED_POOL or OUT_OF_POOL_MEMORY, then something doesn't work.
	throw std::runtime_error("BIG Problems.");
}

void Descriptor_Set_Allocator::create_reserve_pool(VkDevice device)
{
	ZoneScopedN("Reserve pool creation");

	VkDescriptorPoolCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags         = flags,
		.maxSets       = MAX_SETS,
		.poolSizeCount = std::size(pool_sizes),
		.pPoolSizes    = pool_sizes,
	};

	VkDescriptorPool descriptor_pool;
	VkResult result = vkCreateDescriptorPool(device, &create_info, nullptr, &descriptor_pool);

	if (result != VK_SUCCESS)
		throw std::runtime_error("Could not allocate descriptor pool!");

	free_pools.push_back(descriptor_pool);
}

void descriptor_set_allocator_deinit(Descriptor_Set_Allocator* allocator, VkDevice device)
{
	for (auto& pool : allocator->used_pools)
		vkDestroyDescriptorPool(device, pool, nullptr);

	for (auto& pool : allocator->free_pools)
		vkDestroyDescriptorPool(device, pool, nullptr);
}