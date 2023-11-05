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