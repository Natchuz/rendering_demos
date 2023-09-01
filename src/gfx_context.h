#pragma once

#include <format>
#include <vector>
#include <vulkan/vulkan.h>
#include <volk.h>
#include <vk_mem_alloc.h>

struct AllocatedBuffer
{
	VkBuffer      buffer;
	VmaAllocation allocation;
};

struct AllocatedImage
{
	VkImage       handle;
	VmaAllocation allocation;
};

struct Combined_View_Image
{
	VkImage     image;
	VkImageView view;
};

struct Allocated_View_Image : Combined_View_Image
{
	VmaAllocation allocation;
};

// Ignore the pChain of VkPhysicalDeviceVulkan1xProperties.
struct Physical_Device_Properties
{
	VkPhysicalDeviceProperties         properties;
	VkPhysicalDeviceVulkan11Properties properties11;
	VkPhysicalDeviceVulkan12Properties properties12;
	VkPhysicalDeviceVulkan13Properties properties13;
};

// List of optional features that our renderer support if running on capable device
struct Renderer_Capabilities
{
};

// Swapchain is for now the only member of gfx_context that desperately needs separate struct.
struct Swapchain
{
	VkSwapchainKHR handle;
	VkSurfaceKHR   surface;

	VkSurfaceCapabilitiesKHR        surface_capabilities;
	std::vector<VkPresentModeKHR>   present_modes;
	std::vector<VkSurfaceFormatKHR> formats;
	VkPresentModeKHR                selected_present_mode;
	VkSurfaceFormatKHR              selected_format;

	VkExtent2D extent; // Size of swapchain, use this for creating window-dependent resources like depth buffer, etc.

	std::vector<Combined_View_Image> images;
	uint32_t                         images_count; // This is commonly used, but images.size() is uint64_t.
};

// Recreates swapchain based on selected_* values in swapchain struct. Requeries capabilities.
// IMPORTANT: may *not* recreate swapchain, for example if window get minimized, resulting in (0,0) surface extents.
// If it doesn't recreate swapchain, this function will return false, and you should use it to avoid recreating
// framebuffer-size-dependent resources when unnecessary.
bool recreate_swapchain();

// More separate structs might be needed in future when multithreading is introduced
struct Gfx_Context
{
	VkInstance instance;
	uint32_t   instance_version;

	VkDebugUtilsMessengerEXT debug_utils_messenger;

	VkDevice                   device;
	VkPhysicalDevice           physical_device;
	Physical_Device_Properties physical_device_properties;

	VkQueue  gfx_queue;
	uint32_t gfx_queue_family_index;

	VmaAllocator vma_allocator;
	Swapchain    swapchain;
};

inline Gfx_Context* gfx_context;

void gfx_context_init(); // Requires initialized platform
void gfx_context_deinit();

#define DEFINE_NAME_OBJECT(VK_TYPE, ENUM_TYPE)                                          \
	template <class... Args>                                                            \
	void name_object(VK_TYPE handle, std::format_string<Args...> fmt, Args&&... args)   \
    {                                                                                   \
    	const auto formatted = std::vformat(fmt.get(), std::make_format_args(args...)); \
        VkDebugUtilsObjectNameInfoEXT name_info = {                                     \
			.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,                \
			.objectType   = ENUM_TYPE,                                                  \
			.objectHandle = reinterpret_cast<uint64_t>(handle),                         \
			.pObjectName  = formatted.c_str(),                                          \
		};                                                                              \
		vkSetDebugUtilsObjectNameEXT(gfx_context->device, &name_info);                  \
    }

DEFINE_NAME_OBJECT(VkInstance,                      VK_OBJECT_TYPE_INSTANCE);
DEFINE_NAME_OBJECT(VkPhysicalDevice,                VK_OBJECT_TYPE_PHYSICAL_DEVICE);
DEFINE_NAME_OBJECT(VkDevice,                        VK_OBJECT_TYPE_DEVICE);
DEFINE_NAME_OBJECT(VkQueue,                         VK_OBJECT_TYPE_QUEUE);
DEFINE_NAME_OBJECT(VkSemaphore,                     VK_OBJECT_TYPE_SEMAPHORE);
DEFINE_NAME_OBJECT(VkCommandBuffer,                 VK_OBJECT_TYPE_COMMAND_BUFFER);
DEFINE_NAME_OBJECT(VkFence,                         VK_OBJECT_TYPE_FENCE);
DEFINE_NAME_OBJECT(VkDeviceMemory,                  VK_OBJECT_TYPE_DEVICE_MEMORY);
DEFINE_NAME_OBJECT(VkBuffer,                        VK_OBJECT_TYPE_BUFFER);
DEFINE_NAME_OBJECT(VkImage,                         VK_OBJECT_TYPE_IMAGE);
DEFINE_NAME_OBJECT(VkEvent,                         VK_OBJECT_TYPE_EVENT);
DEFINE_NAME_OBJECT(VkQueryPool,                     VK_OBJECT_TYPE_QUERY_POOL);
DEFINE_NAME_OBJECT(VkBufferView,                    VK_OBJECT_TYPE_BUFFER_VIEW);
DEFINE_NAME_OBJECT(VkImageView,                     VK_OBJECT_TYPE_IMAGE_VIEW);
DEFINE_NAME_OBJECT(VkShaderModule,                  VK_OBJECT_TYPE_SHADER_MODULE);
DEFINE_NAME_OBJECT(VkPipelineCache,                 VK_OBJECT_TYPE_PIPELINE_CACHE);
DEFINE_NAME_OBJECT(VkPipelineLayout,                VK_OBJECT_TYPE_PIPELINE_LAYOUT);
DEFINE_NAME_OBJECT(VkRenderPass,                    VK_OBJECT_TYPE_RENDER_PASS);
DEFINE_NAME_OBJECT(VkPipeline,                      VK_OBJECT_TYPE_PIPELINE);
DEFINE_NAME_OBJECT(VkDescriptorSetLayout,           VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT);
DEFINE_NAME_OBJECT(VkSampler,                       VK_OBJECT_TYPE_SAMPLER);
DEFINE_NAME_OBJECT(VkDescriptorPool,                VK_OBJECT_TYPE_DESCRIPTOR_POOL);
DEFINE_NAME_OBJECT(VkDescriptorSet,                 VK_OBJECT_TYPE_DESCRIPTOR_SET);
DEFINE_NAME_OBJECT(VkFramebuffer,                   VK_OBJECT_TYPE_FRAMEBUFFER);
DEFINE_NAME_OBJECT(VkCommandPool,                   VK_OBJECT_TYPE_COMMAND_POOL);
DEFINE_NAME_OBJECT(VkSamplerYcbcrConversion,        VK_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION);
DEFINE_NAME_OBJECT(VkDescriptorUpdateTemplate,      VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE);
DEFINE_NAME_OBJECT(VkPrivateDataSlot,               VK_OBJECT_TYPE_PRIVATE_DATA_SLOT);
DEFINE_NAME_OBJECT(VkSurfaceKHR,                    VK_OBJECT_TYPE_SURFACE_KHR);
DEFINE_NAME_OBJECT(VkSwapchainKHR,                  VK_OBJECT_TYPE_SWAPCHAIN_KHR);
DEFINE_NAME_OBJECT(VkDisplayKHR,                    VK_OBJECT_TYPE_DISPLAY_KHR);
DEFINE_NAME_OBJECT(VkDisplayModeKHR,                VK_OBJECT_TYPE_DISPLAY_MODE_KHR);
DEFINE_NAME_OBJECT(VkDebugReportCallbackEXT,        VK_OBJECT_TYPE_DEBUG_REPORT_CALLBACK_EXT);
DEFINE_NAME_OBJECT(VkVideoSessionKHR,               VK_OBJECT_TYPE_VIDEO_SESSION_KHR);
DEFINE_NAME_OBJECT(VkVideoSessionParametersKHR,     VK_OBJECT_TYPE_VIDEO_SESSION_PARAMETERS_KHR);
DEFINE_NAME_OBJECT(VkCuModuleNVX,                   VK_OBJECT_TYPE_CU_MODULE_NVX);
DEFINE_NAME_OBJECT(VkCuFunctionNVX,                 VK_OBJECT_TYPE_CU_FUNCTION_NVX);
DEFINE_NAME_OBJECT(VkDebugUtilsMessengerEXT,        VK_OBJECT_TYPE_DEBUG_UTILS_MESSENGER_EXT);
DEFINE_NAME_OBJECT(VkAccelerationStructureKHR,      VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR);
DEFINE_NAME_OBJECT(VkValidationCacheEXT,            VK_OBJECT_TYPE_VALIDATION_CACHE_EXT);
DEFINE_NAME_OBJECT(VkAccelerationStructureNV,       VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_NV);
DEFINE_NAME_OBJECT(VkPerformanceConfigurationINTEL, VK_OBJECT_TYPE_PERFORMANCE_CONFIGURATION_INTEL);
DEFINE_NAME_OBJECT(VkDeferredOperationKHR,          VK_OBJECT_TYPE_DEFERRED_OPERATION_KHR);
DEFINE_NAME_OBJECT(VkIndirectCommandsLayoutNV,      VK_OBJECT_TYPE_INDIRECT_COMMANDS_LAYOUT_NV);
DEFINE_NAME_OBJECT(VkMicromapEXT,                   VK_OBJECT_TYPE_MICROMAP_EXT);
DEFINE_NAME_OBJECT(VkOpticalFlowSessionNV,          VK_OBJECT_TYPE_OPTICAL_FLOW_SESSION_NV);
DEFINE_NAME_OBJECT(VkShaderEXT,                     VK_OBJECT_TYPE_SHADER_EXT);

#undef DEFINE_NAME_OBJECT