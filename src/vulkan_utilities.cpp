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