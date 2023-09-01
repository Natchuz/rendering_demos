#include "gfx_context.h"

#include "common.h"
#include "platform.h"
#include "vulkan_utilities.h"

#include <set>
#include <volk.h>

// Private functions
void create_instance  ();
void create_device    ();
void create_allocator ();
void create_swapchain ();
void destroy_instance ();
void destroy_device   ();
void destroy_allocator();
void destroy_swapchain();

void gfx_context_init()
{
	ZoneScopedN("Gfx context creation");

	gfx_context = new Gfx_Context{};

	create_instance();
	create_device();
	create_allocator();
	create_swapchain();
}

void gfx_context_deinit()
{
	ZoneScopedN("Gfx context destruction");

	destroy_swapchain();
	destroy_allocator();
	destroy_device();
	destroy_instance();

	delete gfx_context;
}

// Logger for Vulkan's EXT_debug_utils
// As required in specs, should always return false.
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_utils_messenger_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
																	 VkDebugUtilsMessageTypeFlagsEXT message_type,
																	 const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
																	 void *user_data)
{
#ifndef ENABLE_VULKAN_LOADER_MESSAGES
	if (callback_data->messageIdNumber == 0) // Loader messages usually have id number ==0 0
	{
		return false;
	}
#endif

	spdlog::level::level_enum level;

	// Handle these in this particular order, since if e.g. message has error bit and verbose bit,
	// we should report this as error
	if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
	{
		level = spdlog::level::err;
	}
	else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
	{
		level = spdlog::level::warn;
	}
	else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
	{
		level = spdlog::level::info;
	}
	else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)
	{
		level = spdlog::level::trace;
	} else { assert(false); }

	spdlog::log(level, "[{}]: {}", callback_data->messageIdNumber, callback_data->pMessage);
	return false;
}

const std::vector<std::string> INSTANCE_REQUIRED_EXTENSIONS = {
	VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
};

void create_instance()
{
	ZoneScopedN("Instance creation");

	// Load base pointers
	volkInitialize();

	vkEnumerateInstanceVersion(&gfx_context->instance_version);
	if (gfx_context->instance_version < VK_API_VERSION_1_3)
	{
		throw std::runtime_error("Unsupported vulkan version");
	}

	std::vector<std::string> required_extensions = INSTANCE_REQUIRED_EXTENSIONS;

	// Platform extensions are required
	const auto required_platform_extensions = platform->get_required_extensions();
	required_extensions.insert(required_extensions.end(), required_platform_extensions.begin(),
							   required_platform_extensions.end());

	// Query available extensions
	uint32_t available_extension_count;
	vkEnumerateInstanceExtensionProperties(nullptr, &available_extension_count, nullptr);
	std::vector<VkExtensionProperties> available_extensions(available_extension_count);
	vkEnumerateInstanceExtensionProperties(nullptr, &available_extension_count,
										   available_extensions.data());

	// Build list of enabled extensions.
	// Note that gfx_contextlications like RenderDoc may report VK_EXT_debug_utils twice, thus we need to take care
	// of duplicate extensions
	std::vector<char*> enabled_extensions = {};
	for (auto &required_extension : required_extensions)
	{
		auto predictor = [required_extension](VkExtensionProperties p) -> bool {
			return p.extensionName == required_extension;
		};
		auto it = std::find_if(available_extensions.begin(), available_extensions.end(), predictor);
		if (it != available_extensions.end())
		{
			enabled_extensions.push_back((*it).extensionName);
		}
	}

	if (enabled_extensions.size() != required_extensions.size())
	{
		throw std::runtime_error("Not all required instance extensions are present!");
	}

	const VkDebugUtilsMessengerCreateInfoEXT debug_utils_messenger_create_info = {
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
		.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT
						   | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT
						   | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
						   | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
		.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
					   | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
					   | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT
					   | VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT,
		.pfnUserCallback = debug_utils_messenger_callback,
	};

	const VkApplicationInfo application_info = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName   = "Rendering Demos",
		.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
		.pEngineName        = "Custom Engine",
		.engineVersion      = VK_MAKE_API_VERSION(0, 0, 1, 0),
		.apiVersion         = VK_API_VERSION_1_3,
	};

	const VkInstanceCreateInfo instance_create_info = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pNext                   = &debug_utils_messenger_create_info,
		.pApplicationInfo        = &application_info,
		.enabledLayerCount       = 0,
		.ppEnabledLayerNames     = nullptr,
		.enabledExtensionCount   = static_cast<uint32_t>(enabled_extensions.size()),
		.ppEnabledExtensionNames = enabled_extensions.data(),
	};

	auto vk_err = vkCreateInstance(&instance_create_info, nullptr, &gfx_context->instance);
	switch (vk_err)
	{
		case VK_SUCCESS:
			break;
		case VK_ERROR_LAYER_NOT_PRESENT:
			throw std::runtime_error("Layer not present!");
		case VK_ERROR_EXTENSION_NOT_PRESENT:
			throw std::runtime_error("Extensions not present!");
		case VK_ERROR_INCOMPATIBLE_DRIVER:
			throw std::runtime_error("Incompatible driver!");
		case VK_ERROR_INITIALIZATION_FAILED:
			throw std::runtime_error("Initialization failed!");
		default:
			throw std::runtime_error("Could not create instance!");
	}

	// Load instance and device pointers
	volkLoadInstance(gfx_context->instance);

	vkCreateDebugUtilsMessengerEXT(gfx_context->instance, &debug_utils_messenger_create_info, nullptr,
								   &gfx_context->debug_utils_messenger);
}

void destroy_instance()
{
}

const std::set<std::string> DEVICE_REQUIRED_EXTENSIONS = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

void create_device()
{
	ZoneScopedN("Device selection and creation");

	// Query available physical devices
	uint32_t available_physical_devices_count;
	vkEnumeratePhysicalDevices(gfx_context->instance, &available_physical_devices_count, nullptr);
	std::vector<VkPhysicalDevice> available_physical_devices(available_physical_devices_count);
	vkEnumeratePhysicalDevices(gfx_context->instance, &available_physical_devices_count, available_physical_devices.data());

	// Device selection algorithm presented here is completely over the top and unnecessary, since a simple
	// heuristic of "just pick the first discrete GPU" would be more than fine, but I kinda felt fancy
	// and did this monstrosity, which is probably the most specs-certified way there is.

	struct Device_Candidate
	{
		VkPhysicalDevice physical_device;
		Physical_Device_Properties device_properties;
		uint32_t gfx_family_queue_index;
		VkPhysicalDeviceFeatures device_features;
		VkPhysicalDeviceVulkan11Features device_features11;
		VkPhysicalDeviceVulkan12Features device_features12;
		VkPhysicalDeviceVulkan13Features device_features13;
		Renderer_Capabilities renderer_features;
		// We need to carry entire VkExtensionProperties, to avoid invalid pointers to extensions' names
		std::vector<VkExtensionProperties> interested_extensions;
	};

	std::vector<Device_Candidate> candidates = {};

	// Build candidate list by eliminating devices that do not support required extensions, properties, queues etc.
	for (auto &candidate_device : available_physical_devices)
	{
		Device_Candidate candidate = {candidate_device };

		// Query device properties, by linking all required structs
		candidate.device_properties.properties11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES;
		candidate.device_properties.properties11.pNext = &candidate.device_properties.properties12;
		candidate.device_properties.properties12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES;
		candidate.device_properties.properties12.pNext = &candidate.device_properties.properties13;
		candidate.device_properties.properties13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES;

		VkPhysicalDeviceProperties2 properties2 = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
			.pNext = &candidate.device_properties.properties11,
		};

		vkGetPhysicalDeviceProperties2(candidate_device, &properties2);
		candidate.device_properties.properties = properties2.properties;

		// Check if required properties2 are supported
	auto support_vulkan_13 = candidate.device_properties.properties.apiVersion >= VK_API_VERSION_1_3;
		if (!support_vulkan_13)
		{
			continue;
		}

		// Query device features, by linking all required structs
		candidate.device_features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
		candidate.device_features11.pNext = &candidate.device_features12;
		candidate.device_features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
		candidate.device_features12.pNext = &candidate.device_features13;
		candidate.device_features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

		VkPhysicalDeviceFeatures2 features2 = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
			.pNext = &candidate.device_features11,
		};
		vkGetPhysicalDeviceFeatures2(candidate_device, &features2);
		candidate.device_features = features2.features;

		// Check if required features are supported
		auto dynamic_rendering = candidate.device_features13.dynamicRendering;
		auto synchronization2 = candidate.device_features13.synchronization2;
		auto anisotropy = candidate.device_features.samplerAnisotropy;
		auto variable_descriptor = candidate.device_features12.descriptorBindingVariableDescriptorCount;
		if (!dynamic_rendering || !synchronization2 || !anisotropy || !variable_descriptor)
		{
			continue;
		}

		// Query for queue families
		uint32_t queue_families_count;
		vkGetPhysicalDeviceQueueFamilyProperties(candidate_device, &queue_families_count, nullptr);
		std::vector<VkQueueFamilyProperties> queue_families(queue_families_count);
		vkGetPhysicalDeviceQueueFamilyProperties(candidate_device, &queue_families_count, queue_families.data());

		// Find of family queue that support our required operations
		int32_t best_queue_find = -1;
		for (int32_t queue_index = 0; queue_index < queue_families_count; queue_index++)
		{
			auto queue = queue_families[queue_index];
			auto support_required_ops = queue.queueFlags
										& (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT | VK_QUEUE_TRANSFER_BIT);
			auto support_presentation = platform->check_presentation_support(gfx_context->instance,
																			 candidate_device,
																			 queue_index);
			if (support_required_ops && support_presentation)
			{
				best_queue_find = queue_index;
				break;
			}
		}
		candidate.gfx_family_queue_index = best_queue_find;

		if (best_queue_find == -1)
		{
			continue; // This device doesn't support any queue with required flags
		}

		// The code here is simplified for a moment as it doesn't look up optional extensions that certain
		// renderer features might be interested in, so it will need to be changed once I introduce these features.

		// Query extensions
		uint32_t available_extension_count;
		vkEnumerateDeviceExtensionProperties(candidate_device, nullptr, &available_extension_count, nullptr);
		std::vector<VkExtensionProperties> available_extensions(available_extension_count);
		vkEnumerateDeviceExtensionProperties(candidate_device, nullptr, &available_extension_count,
											 available_extensions.data());

		candidate.interested_extensions = {};
		for (auto &available_extension : available_extensions)
		{
			// A lot of allocations again :/
			if (DEVICE_REQUIRED_EXTENSIONS.contains(std::string(available_extension.extensionName)))
			{
				candidate.interested_extensions.push_back(available_extension);
			}
		}

		if (candidate.interested_extensions.size() != DEVICE_REQUIRED_EXTENSIONS.size())
		{
			continue;
		}

		// Knowing all of these, let's build set of optionals features that may be enabled in our renderer
		// if proper features, extensions and limits are present.
		candidate.renderer_features = {};

		candidates.push_back(candidate);
	}

	if (candidates.empty())
	{
		throw std::runtime_error("No suitable device found!");
	}

	// Sort candidates by number of functions they provide. We'll sort from best to worse, so compare function
	// will return true if device "b" is worse than "a"
	auto compare_function = [](const Device_Candidate& a, const Device_Candidate& b) -> bool
	{
		// We don't have sophisticated renderer features for now, so let's just compare based on device type

		// That's some complicated criteria... is this possible to simplify?
		const auto is_a_discr = a.device_properties.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
		const auto is_a_vi_in = a.device_properties.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU
								|| a.device_properties.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
		const auto is_a_other = a.device_properties.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_OTHER
								|| a.device_properties.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;

		const auto is_b_discr = b.device_properties.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
		const auto is_b_vi_in = b.device_properties.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU
								|| b.device_properties.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
		const auto is_b_other = b.device_properties.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_OTHER
								|| b.device_properties.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;

		auto are_the_same = (is_a_discr && is_b_discr) || (is_a_vi_in && is_b_vi_in) || (is_a_other && is_b_other);

		// Selecting order: Discrete > Virtual | Integrated > Other
		if (!are_the_same)
		{
			// I guess that covers everything?
			return ((is_b_other && is_a_vi_in) || (is_b_other && is_a_discr) || (is_b_vi_in && is_a_discr));
		}

		return true;
	};
	std::stable_sort(candidates.begin(), candidates.end(), compare_function);

	auto selected_candidate = candidates[0];
	gfx_context->physical_device = selected_candidate.physical_device;
	gfx_context->physical_device_properties = selected_candidate.device_properties;
	gfx_context->gfx_queue_family_index = selected_candidate.gfx_family_queue_index;

	spdlog::info("Selected device: {}", selected_candidate.device_properties.properties.deviceName);
	spdlog::info("Driver: {}, id {}", selected_candidate.device_properties.properties12.driverName,
				 selected_candidate.device_properties.properties.driverVersion);

	// Create device
	float queue_priorities = 1.0;
	VkDeviceQueueCreateInfo queue_create_info = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = selected_candidate.gfx_family_queue_index,
		.queueCount = 1,
		.pQueuePriorities = &queue_priorities,
	};

	VkPhysicalDeviceVulkan13Features device_13_features = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
		.synchronization2 = true,
		.dynamicRendering = true,
	};

	VkPhysicalDeviceVulkan12Features device_12_features = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
		.pNext = &device_13_features,
		.descriptorBindingVariableDescriptorCount = true,
	};

	VkPhysicalDeviceFeatures device_core_features = {
		.samplerAnisotropy = true,
	};

	std::vector<char*> enabled_extensions(selected_candidate.interested_extensions.size());
	std::transform(selected_candidate.interested_extensions.begin(), selected_candidate.interested_extensions.end(),
				   enabled_extensions.begin(), [](auto &it) { return it.extensionName; });

	VkDeviceCreateInfo device_create_info = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = &device_12_features,
		.queueCreateInfoCount = 1,
		.pQueueCreateInfos = &queue_create_info,
		.enabledExtensionCount = static_cast<uint32_t>(enabled_extensions.size()),
		.ppEnabledExtensionNames = enabled_extensions.data(),
		.pEnabledFeatures = &device_core_features,
	};

	auto vk_err = vkCreateDevice(gfx_context->physical_device, &device_create_info, nullptr, &gfx_context->device);
	switch (vk_err)
	{
		case VK_SUCCESS:
			break;
		case VK_ERROR_FEATURE_NOT_PRESENT:
			throw std::runtime_error("Feature not present!"); // If this hgfx_contextens, we have a bug!
		case VK_ERROR_EXTENSION_NOT_PRESENT:
			throw std::runtime_error("Extensions not present!"); // If this hgfx_contextens, we have a bug!
		case VK_ERROR_INITIALIZATION_FAILED:
			throw std::runtime_error("Initialization failed!");
		default:
			throw std::runtime_error("Could not create device!");
	}
	volkLoadDevice(gfx_context->device);
	vkGetDeviceQueue(gfx_context->device, gfx_context->gfx_queue_family_index, 0, &gfx_context->gfx_queue);

	// Set debug names for objects
	name_object(gfx_context->physical_device, "Main physical device");
	name_object(gfx_context->device, "Main device");
	name_object( gfx_context->gfx_queue, "Main graphics queue");
}

void destroy_device()
{
}

void create_allocator()
{
	ZoneScopedN("VMA creation");

	VmaVulkanFunctions vulkan_functions = {
		.vkGetInstanceProcAddr                   = vkGetInstanceProcAddr,
		.vkGetDeviceProcAddr                     = vkGetDeviceProcAddr,
		.vkGetPhysicalDeviceProperties           = vkGetPhysicalDeviceProperties,
		.vkGetPhysicalDeviceMemoryProperties     = vkGetPhysicalDeviceMemoryProperties,
		.vkAllocateMemory                        = vkAllocateMemory,
		.vkFreeMemory                            = vkFreeMemory,
		.vkMapMemory                             = vkMapMemory,
		.vkUnmapMemory                           = vkUnmapMemory,
		.vkFlushMappedMemoryRanges               = vkFlushMappedMemoryRanges,
		.vkInvalidateMappedMemoryRanges          = vkInvalidateMappedMemoryRanges,
		.vkBindBufferMemory                      = vkBindBufferMemory,
		.vkBindImageMemory                       = vkBindImageMemory,
		.vkGetBufferMemoryRequirements           = vkGetBufferMemoryRequirements,
		.vkGetImageMemoryRequirements            = vkGetImageMemoryRequirements,
		.vkCreateBuffer                          = vkCreateBuffer,
		.vkDestroyBuffer                         = vkDestroyBuffer,
		.vkCreateImage                           = vkCreateImage,
		.vkDestroyImage                          = vkDestroyImage,
		.vkCmdCopyBuffer                         = vkCmdCopyBuffer,
		.vkGetBufferMemoryRequirements2KHR       = vkGetBufferMemoryRequirements2KHR,
		.vkGetImageMemoryRequirements2KHR        = vkGetImageMemoryRequirements2KHR,
		.vkBindBufferMemory2KHR                  = vkBindBufferMemory2KHR,
		.vkBindImageMemory2KHR                   = vkBindImageMemory2KHR,
		.vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2KHR,
		.vkGetDeviceBufferMemoryRequirements     = vkGetDeviceBufferMemoryRequirements,
		.vkGetDeviceImageMemoryRequirements      = vkGetDeviceImageMemoryRequirements,
	};

	VmaAllocatorCreateInfo create_info = {
		.flags            = VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT,
		.physicalDevice   = gfx_context->physical_device,
		.device           = gfx_context->device,
		.pVulkanFunctions = &vulkan_functions,
		.instance         = gfx_context->instance,
		.vulkanApiVersion = VK_API_VERSION_1_3,
	};

	vmaCreateAllocator(&create_info, &gfx_context->vma_allocator);
}

void destroy_allocator()
{
}

void create_swapchain()
{
	ZoneScopedN("Swapchain initialization");

	Swapchain* swapchain = &gfx_context->swapchain; // Shortcut

	platform->create_surface(gfx_context->instance, &swapchain->surface);

	uint32_t present_modes_count;
	vkGetPhysicalDeviceSurfacePresentModesKHR(gfx_context->physical_device, swapchain->surface, &present_modes_count, nullptr);
	swapchain->present_modes = std::vector<VkPresentModeKHR>(present_modes_count);
	vkGetPhysicalDeviceSurfacePresentModesKHR(gfx_context->physical_device, swapchain->surface, &present_modes_count,
											  swapchain->present_modes.data());

	// Automatically select best present MODE if available
	swapchain->selected_present_mode = VK_PRESENT_MODE_FIFO_KHR; // Required to be always supported.
	for (const auto &present_mode: swapchain->present_modes)
	{
		if (present_mode == VK_PRESENT_MODE_MAILBOX_KHR)
		{
			swapchain->selected_present_mode = present_mode;
			break;
		}
	}

	uint32_t formats_count;
	vkGetPhysicalDeviceSurfaceFormatsKHR(gfx_context->physical_device, swapchain->surface, &formats_count, nullptr);
	swapchain->formats = std::vector<VkSurfaceFormatKHR>(formats_count);
	vkGetPhysicalDeviceSurfaceFormatsKHR(gfx_context->physical_device, swapchain->surface, &formats_count,
										 swapchain->formats.data());

	swapchain->selected_format = swapchain->formats[0];
	for (const auto &format : swapchain->formats)
	{
		if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
		{
			swapchain->selected_format = format;
			break;
		}
	}

	// Technically, we should query for capabilities for selected present mode, but we're using swapchain
	// images only as color attachment, so it doesn't matter since color attachment usage is required anyway.
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gfx_context->physical_device, swapchain->surface,
											  &swapchain->surface_capabilities);

	auto surface_extent = swapchain->surface_capabilities.currentExtent;
	// Special value indicating that surface size will be determined by swapchain
	if (surface_extent.width == 0xFFFFFFFF && surface_extent.height == 0xFFFFFFFF) {
		auto size = platform->window_get_size();
		surface_extent = {size.width, size.height };
		surface_extent.width = std::clamp(
			surface_extent.width,
			swapchain->surface_capabilities.minImageExtent.width,
			swapchain->surface_capabilities.maxImageExtent.width);
		surface_extent.height = std::clamp(
			surface_extent.height,
			swapchain->surface_capabilities.minImageExtent.height,
			swapchain->surface_capabilities.maxImageExtent.height);
	}
	swapchain->extent = surface_extent;

	uint32_t requested_image_count = swapchain->surface_capabilities.minImageCount + 1;
	if (swapchain->surface_capabilities.maxImageCount > 0 && // When zero, threre is no max value
		swapchain->surface_capabilities.maxImageCount < requested_image_count)
	{
		requested_image_count = swapchain->surface_capabilities.maxImageCount;
	}

	VkSwapchainCreateInfoKHR swapchain_create_info = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface          = swapchain->surface,
		.minImageCount    = 4,
		.imageFormat      = swapchain->selected_format.format,
		.imageColorSpace  = swapchain->selected_format.colorSpace,
		.imageExtent      = surface_extent,
		.imageArrayLayers = 1,
		.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.preTransform     = swapchain->surface_capabilities.currentTransform,
		.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode      = swapchain->selected_present_mode,
		.clipped          = true,
		.oldSwapchain     = VK_NULL_HANDLE,
	};
	vkCreateSwapchainKHR(gfx_context->device, &swapchain_create_info, nullptr, &swapchain->handle);

	uint32_t image_count;
	vkGetSwapchainImagesKHR(gfx_context->device, swapchain->handle, &image_count, nullptr);
	std::vector<VkImage> images(image_count);
	vkGetSwapchainImagesKHR(gfx_context->device, swapchain->handle, &image_count, images.data());

	swapchain->images       = std::vector<Combined_View_Image>(image_count);
	swapchain->images_count = image_count;

	for (uint32_t image_index = 0; image_index < image_count; image_index++)
	{
		VkImageViewCreateInfo image_view_create_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image    = images[image_index],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format   = swapchain->selected_format.format,
			.components = {
				.r = VK_COMPONENT_SWIZZLE_IDENTITY,
				.g = VK_COMPONENT_SWIZZLE_IDENTITY,
				.b = VK_COMPONENT_SWIZZLE_IDENTITY,
				.a = VK_COMPONENT_SWIZZLE_IDENTITY,
			},
			.subresourceRange = {
				.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel   = 0,
				.levelCount     = 1,
				.baseArrayLayer = 0,
				.layerCount     = 1,
			},
		};

		vkCreateImageView(gfx_context->device, &image_view_create_info, nullptr, &swapchain->images[image_index].view);
		swapchain->images[image_index].image = images[image_index];
	}
}

void destroy_swapchain()
{
	ZoneScopedN("Swapchain destruction");

	Swapchain* swapchain = &gfx_context->swapchain; // Shortcut

	for (const auto &image : gfx_context->swapchain.images)
	{
		vkDestroyImageView(gfx_context->device, image.view, nullptr);
	}

	vkDestroySwapchainKHR(gfx_context->device, swapchain->handle, nullptr);
	vkDestroySurfaceKHR(gfx_context->instance, swapchain->surface, nullptr);
}

bool recreate_swapchain()
{
	ZoneScopedN("Swapchain recreation");

	Swapchain* swapchain = &gfx_context->swapchain; // Shortcut

	// Requery surface capabilities
	VkSurfaceCapabilitiesKHR surface_capabilities;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gfx_context->physical_device, swapchain->surface, &surface_capabilities);

	auto surface_extent = surface_capabilities.currentExtent;
	// Special value indicating that surface size will be determined by swapchain
	if (surface_extent.width == 0xFFFFFFFF && surface_extent.height == 0xFFFFFFFF) {
		auto size = platform->window_get_size();
		surface_extent = {size.width, size.height };
		surface_extent.width = std::clamp(
			surface_extent.width,
			surface_capabilities.minImageExtent.width,
			surface_capabilities.maxImageExtent.width);
		surface_extent.height = std::clamp(
			surface_extent.height,
			surface_capabilities.minImageExtent.height,
			surface_capabilities.maxImageExtent.height);
	}

	// Swapchain is (0, 0), thus window is probably minimized, don't recreate swapchain then.
	if (surface_extent.width == 0 && surface_extent.height == 0)
	{
		return false;
	}

	// Destroy associated resources
	for (const auto &image : gfx_context->swapchain.images)
	{
		vkDestroyImageView(gfx_context->device, image.view, nullptr);
	}
	vkDestroySwapchainKHR(gfx_context->device, swapchain->handle, nullptr);

	swapchain->surface_capabilities = surface_capabilities;
	swapchain->extent               = surface_extent;

	uint32_t requested_image_count = swapchain->surface_capabilities.minImageCount + 1;
	if (swapchain->surface_capabilities.maxImageCount > 0 && // When zero, threre is no max value
		swapchain->surface_capabilities.maxImageCount < requested_image_count)
	{
		requested_image_count = swapchain->surface_capabilities.maxImageCount;
	}

	VkSwapchainCreateInfoKHR swapchain_create_info = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface          = swapchain->surface,
		.minImageCount    = requested_image_count,
		.imageFormat      = swapchain->selected_format.format,
		.imageColorSpace  = swapchain->selected_format.colorSpace,
		.imageExtent      = surface_extent,
		.imageArrayLayers = 1,
		.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.preTransform     = swapchain->surface_capabilities.currentTransform,
		.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode      = swapchain->selected_present_mode,
		.clipped          = true,
		.oldSwapchain     = VK_NULL_HANDLE,
	};
	vkCreateSwapchainKHR(gfx_context->device, &swapchain_create_info, nullptr, &swapchain->handle);

	uint32_t image_count;
	vkGetSwapchainImagesKHR(gfx_context->device, swapchain->handle, &image_count, nullptr);
	std::vector<VkImage> images(image_count);
	vkGetSwapchainImagesKHR(gfx_context->device, swapchain->handle, &image_count, images.data());

	swapchain->images       = std::vector<Combined_View_Image>(image_count);
	swapchain->images_count = image_count;

	for (uint32_t image_index = 0; image_index < image_count; image_index++)
	{
		VkImageViewCreateInfo image_view_create_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image    = images[image_index],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format   = swapchain->selected_format.format,
			.components = {
				.r = VK_COMPONENT_SWIZZLE_IDENTITY,
				.g = VK_COMPONENT_SWIZZLE_IDENTITY,
				.b = VK_COMPONENT_SWIZZLE_IDENTITY,
				.a = VK_COMPONENT_SWIZZLE_IDENTITY,
			},
			.subresourceRange = {
				.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel   = 0,
				.levelCount     = 1,
				.baseArrayLayer = 0,
				.layerCount     = 1,
			},
		};

		vkCreateImageView(gfx_context->device, &image_view_create_info, nullptr, &swapchain->images[image_index].view);
		swapchain->images[image_index].image = images[image_index];
	}

	return true;
}