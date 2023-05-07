#include<iostream>
#include<vulkan/vulkan.h>
#include<GLFW/glfw3.h>
#include<vector>
#include<span>
#include<algorithm>
#include<limits>

#pragma warning (disable: 4703)

/* Provides strting names for [VkPhysicalDeviceType] */
const char* deviceTypesNames[5] = {
	"OTHER",
	"INTEGRATED_GPU",
	"DISCRETE_GPU",
	"VIRTUAL_GPU",
	"CPU"
};

int main()
{
	if (!glfwInit())
	{
		return 1;
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	GLFWwindow* window = glfwCreateWindow(640, 480, "My Title", NULL, NULL);

	/*PFN_vkCreateInstance pfnCreateInstance = (PFN_vkCreateInstance)
		glfwGetInstanceProcAddress(NULL, "vkCreateInstance");*/

	VkResult vk_err;

	/*
		GLFW-Vulkan related stuff
	*/
	uint32_t required_glfw_extensions_count;
	const char** glfw_extension = glfwGetRequiredInstanceExtensions(&required_glfw_extensions_count);

	/*
		Instance creation
	*/

	/* TODO try loding this function with vkGetInstanceProcAddr first If it fails,
	it means we're using vulkan 1.0. (as per Vulkan specification, noted in initaliztion section:
	https://registry.khronos.org/vulkan/specs/1.3-extensions/html/vkspec.html#initialization-instances */
	uint32_t instance_version;
	vkEnumerateInstanceVersion(&instance_version); // Error ignored.
	std::cout
		<< "Vulkan instance version: "
		<< VK_API_VERSION_VARIANT(instance_version) << "."
		<< VK_API_VERSION_MAJOR(instance_version) << "."
		<< VK_API_VERSION_MINOR(instance_version) << "."
		<< VK_API_VERSION_PATCH(instance_version) << "\n";

	VkApplicationInfo application_info = {};
	application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	application_info.pApplicationName = "ApplicationName";
	application_info.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
	application_info.pEngineName = "EngineName";
	application_info.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
	application_info.apiVersion = VK_API_VERSION_1_3;

	const VkValidationFeatureEnableEXT enabled_validation_features[2] = {
		VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
		VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT };
	VkValidationFeaturesEXT validation_features = {};

	validation_features.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
	validation_features.enabledValidationFeatureCount = 2;
	validation_features.pEnabledValidationFeatures = enabled_validation_features;

	const char* enabled_validation_layers[1] = { "VK_LAYER_KHRONOS_validation" };
	VkInstanceCreateInfo instance_create_info = {};
	instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance_create_info.pNext = &validation_features;
	instance_create_info.pApplicationInfo = &application_info;
	instance_create_info.enabledLayerCount = 1;
	instance_create_info.ppEnabledLayerNames = enabled_validation_layers;
	instance_create_info.enabledExtensionCount = required_glfw_extensions_count;
	instance_create_info.ppEnabledExtensionNames = glfw_extension;

	VkInstance instance;
	vk_err = vkCreateInstance(&instance_create_info, nullptr, &instance);

	// TODO handle more errors
	switch (vk_err)
	{
	case VK_ERROR_LAYER_NOT_PRESENT:
		std::cerr << "Layers not present! Terminating!\n"; // TODO graceful deinitizalization
		return 1;
	case VK_ERROR_EXTENSION_NOT_PRESENT:
		std::cerr << "Extension not present! Terminating!\n"; // TODO graceful deinitizalization
		return 1;
	default:
		break;
	};

	std::cout << "Following validation layers loaded (1):\n";
	for (auto& layer : enabled_validation_layers)
	{
		std::cout << "\t" << layer << "\n";
	}

	/*
		Physical device initialization
	*/

	VkPhysicalDevice phdevice;
	VkPhysicalDeviceProperties phdevice_props;
	{
	uint32_t pdevice_count, enumerated = 0;
	VkPhysicalDevice pdevice_buffer[10];
	VkResult enum_result;

	std::cout << "Found following physical devices:\n";
	do
	{
		pdevice_count = 10;
		enum_result = vkEnumeratePhysicalDevices(instance, &pdevice_count, pdevice_buffer);

		if ((enumerated == 0 && pdevice_count == 0)
			|| enum_result == VK_ERROR_OUT_OF_HOST_MEMORY
			|| enum_result == VK_ERROR_OUT_OF_HOST_MEMORY
			|| enum_result == VK_ERROR_OUT_OF_HOST_MEMORY)
		{
			break; // TODO: Panic
		}

		for (uint8_t i = 0; i < pdevice_count; i++)
		{
			VkPhysicalDevice candidate = pdevice_buffer[i];
			VkPhysicalDeviceProperties candidate_props;
			vkGetPhysicalDeviceProperties(candidate, &candidate_props);

			/* Compare physical device. For now will select first discrete gpu */
			if (enumerated == 0)
			{
				phdevice = candidate;
				phdevice_props = candidate_props;
			}

			if (candidate_props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU
				&& phdevice_props.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
			{
				phdevice = candidate;
				phdevice_props = candidate_props;
			}

			std::cout << "\t" << enumerated << ":\n"
				<< "\t\tAPI version:         "
				<< VK_API_VERSION_VARIANT(candidate_props.apiVersion) << "."
				<< VK_API_VERSION_MAJOR(candidate_props.apiVersion) << "."
				<< VK_API_VERSION_MINOR(candidate_props.apiVersion) << "."
				<< VK_API_VERSION_PATCH(candidate_props.apiVersion) << "\n"
				<< "\t\tDriver version:      " << candidate_props.driverVersion << "\n"
				<< "\t\tVendor id:           " << candidate_props.vendorID << "\n"
				<< "\t\tDevice id:           " << candidate_props.deviceID << "\n"
				<< "\t\tDevice id:           " << candidate_props.driverVersion << "\n"
				<< "\t\tDevice name:         " << candidate_props.deviceName << "\n"
				<< "\t\tDevice type:         " << deviceTypesNames[candidate_props.deviceType] << "\n";

			enumerated += 1;
		}

	} while (enum_result == VK_INCOMPLETE);
	}
	std::cout << "Selected " << phdevice_props.deviceName << "\n";

	/*
		Queue selection
	*/
	uint32_t queue_family_count;
	vkGetPhysicalDeviceQueueFamilyProperties(phdevice, &queue_family_count, nullptr);
	VkQueueFamilyProperties* queue_family_props = new VkQueueFamilyProperties[queue_family_count];
	vkGetPhysicalDeviceQueueFamilyProperties(phdevice, &queue_family_count, queue_family_props);

	std::cout << "Found following queue families:\n";
	for (uint32_t i = 0; i < queue_family_count; i++)
	{
		std::cout << "\t" << i << ":\n"
			<< "\t\tFlags:" << "\n";
		if (queue_family_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) std::cout << "\t\t\tGRAPHICS\n";
		if (queue_family_props[i].queueFlags & VK_QUEUE_COMPUTE_BIT) std::cout << "\t\t\tCOMPUTE\n";
		if (queue_family_props[i].queueFlags & VK_QUEUE_TRANSFER_BIT) std::cout << "\t\t\tTRANSFER\n";
		if (queue_family_props[i].queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) std::cout << "\t\t\tSPARSE_BINDING\n";
		if (queue_family_props[i].queueFlags & VK_QUEUE_PROTECTED_BIT) std::cout << "\t\t\tPROTECTED_BIT\n";
		std::cout << "\t\tQueue Count : " << queue_family_props[i].queueCount << "\n";
	}

	int32_t graphics_queue_family_index = -1;
	for (uint32_t i = 0; i < queue_family_count; i++)
	{
		if (queue_family_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
		{
			graphics_queue_family_index = i;
			break;
		}
	}
	if (graphics_queue_family_index == -1) {} // TOOD: Panic
	std::cout << "Selected queue family with index: " << graphics_queue_family_index << "\n";

	/*
		Logic device and queue creation
	*/
	float queue_prorities = 1.0;
	VkDeviceQueueCreateInfo queue_create_info = {};
	queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue_create_info.queueFamilyIndex = graphics_queue_family_index;
	queue_create_info.queueCount = 1; // Single queue for now
	queue_create_info.pQueuePriorities = &queue_prorities;

	const std::vector<const char*> device_extensions = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME
	};
	VkDeviceCreateInfo device_create_info = {};
	device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	device_create_info.queueCreateInfoCount = 1;
	device_create_info.pQueueCreateInfos = &queue_create_info;
	device_create_info.enabledExtensionCount = device_extensions.size();
	device_create_info.ppEnabledExtensionNames = device_extensions.data();

	VkDevice device;
	vk_err = vkCreateDevice(phdevice, &device_create_info, nullptr, &device);

	if (vk_err != VK_SUCCESS) {} // TODO: Panic

	VkQueue queue;
	vkGetDeviceQueue(device, graphics_queue_family_index, 0, &queue);

	/*
		Surface and swapchain creation
	*/
	
	
	if (!glfwGetPhysicalDevicePresentationSupport(instance, phdevice, graphics_queue_family_index)) {} // TODO: Panic

	VkSurfaceKHR surface;
	vk_err = glfwCreateWindowSurface(instance, window, nullptr, &surface);

	uint32_t format_count;
	vkGetPhysicalDeviceSurfaceFormatsKHR(phdevice, surface, &format_count, nullptr);
	std::vector<VkSurfaceFormatKHR> formats(format_count);
	vkGetPhysicalDeviceSurfaceFormatsKHR(phdevice, surface, &format_count, formats.data());

	uint32_t present_mode_count;
	vkGetPhysicalDeviceSurfacePresentModesKHR(phdevice, surface, &present_mode_count, nullptr);
	std::vector<VkPresentModeKHR> present_modes(present_mode_count);
	vkGetPhysicalDeviceSurfacePresentModesKHR(phdevice, surface, &present_mode_count, present_modes.data());

	VkSurfaceCapabilitiesKHR surface_capabilities;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phdevice, surface, &surface_capabilities);

	
	VkSurfaceFormatKHR surface_format = formats[0];
	for (const auto& format : formats) {
		if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
			surface_format = format;
			break;
		}
	}

	VkPresentModeKHR selected_present_mode = VK_PRESENT_MODE_FIFO_KHR;
	for (const auto& present_mode : present_modes) {
		if (present_mode == VK_PRESENT_MODE_MAILBOX_KHR) {
			selected_present_mode = present_mode;
			break;
		}
	}

	VkExtent2D swap_extent = surface_capabilities.currentExtent;
	if (surface_capabilities.currentExtent.width == std::numeric_limits<uint32_t>::max()) {
		int32_t width, height;
		glfwGetFramebufferSize(window, &width, &height);

		swap_extent = {
			static_cast<uint32_t>(width),
			static_cast<uint32_t>(height)
		};

		swap_extent.width = std::clamp(
			swap_extent.width,
			surface_capabilities.minImageExtent.width, 
			surface_capabilities.maxImageExtent.width);
		swap_extent.height = std::clamp(
			swap_extent.height,
			surface_capabilities.minImageExtent.height, 
			surface_capabilities.maxImageExtent.height);
	}

	uint32_t image_count = surface_capabilities.minImageCount + 1;
	if (surface_capabilities.maxImageCount > 0 && image_count > surface_capabilities.maxImageCount) {
		image_count = surface_capabilities.maxImageCount;
	}
	
	VkSwapchainCreateInfoKHR swapchain_create_info = {};
	swapchain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapchain_create_info.surface = surface;
	swapchain_create_info.minImageCount = image_count;
	swapchain_create_info.imageFormat = surface_format.format;
	swapchain_create_info.imageColorSpace = surface_format.colorSpace;
	swapchain_create_info.imageExtent = swap_extent;
	swapchain_create_info.imageArrayLayers = 1;
	swapchain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapchain_create_info.preTransform = surface_capabilities.currentTransform;
	swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapchain_create_info.presentMode = selected_present_mode;
	swapchain_create_info.clipped = true;
	swapchain_create_info.oldSwapchain = VK_NULL_HANDLE;

	VkSwapchainKHR swapchain;
	vk_err = vkCreateSwapchainKHR(device, &swapchain_create_info, nullptr, &swapchain);

	uint32_t swapchain_image_count;
	vkGetSwapchainImagesKHR(device, swapchain, &swapchain_image_count, nullptr);
	std::vector<VkImage> swapchain_images(swapchain_image_count);
	vkGetSwapchainImagesKHR(device, swapchain, &swapchain_image_count, swapchain_images.data());


	/*
		Command buffer and command pool creation
	*/

	VkCommandPoolCreateInfo command_pool_create_info = {};
	command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	command_pool_create_info.queueFamilyIndex = graphics_queue_family_index;

	VkCommandPool command_pool;
	vk_err = vkCreateCommandPool(device, &command_pool_create_info, nullptr,  &command_pool);

	if (vk_err != VK_SUCCESS) {} // TODO: Panic

	VkCommandBufferAllocateInfo command_buffer_allocate_info = {};
	command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	command_buffer_allocate_info.commandPool = command_pool;
	command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	command_buffer_allocate_info.commandBufferCount = 2;

	VkCommandBuffer command_buffers[2];
	vkAllocateCommandBuffers(device, &command_buffer_allocate_info, command_buffers);

	/*
		Some barriers and fences
	*/
	VkSemaphore semaphore_render_finish[2], semaphore_image_ready[2];
	VkSemaphoreCreateInfo semaphore_create_info = {};
	semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	vkCreateSemaphore(device, &semaphore_create_info, nullptr, &semaphore_render_finish[0]);
	vkCreateSemaphore(device, &semaphore_create_info, nullptr, &semaphore_render_finish[1]);
	vkCreateSemaphore(device, &semaphore_create_info, nullptr, &semaphore_image_ready[0]);
	vkCreateSemaphore(device, &semaphore_create_info, nullptr, &semaphore_image_ready[1]);

	VkFence fence_render_finish[2];
	VkFenceCreateInfo fence_create_info = {};
	fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	vkCreateFence(device, &fence_create_info, nullptr, &fence_render_finish[0]);
	vkCreateFence(device, &fence_create_info, nullptr, &fence_render_finish[1]);

	/*
		Loop
	*/

	uint32_t frame = 0;
	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		uint32_t index = frame % 2;

		vkWaitForFences(device, 1, &fence_render_finish[index], true, UINT64_MAX);
		vkResetFences(device, 1, &fence_render_finish[index]);

		uint32_t image_index;
		vkAcquireNextImageKHR(device, swapchain, std::numeric_limits<uint64_t>::max(), 
			semaphore_image_ready[index], VK_NULL_HANDLE, &image_index);
		
		VkCommandBufferBeginInfo begin_info = {};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vk_err = vkBeginCommandBuffer(command_buffers[index], &begin_info);

		VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.srcQueueFamilyIndex = graphics_queue_family_index;
		barrier.dstQueueFamilyIndex = graphics_queue_family_index;
		barrier.image = swapchain_images[image_index];
		barrier.subresourceRange = range;

		vkCmdPipelineBarrier(
			command_buffers[index],
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &barrier);

		VkClearColorValue clear_color = { .float32 = {1, 0, 0, 1} };
		vkCmdClearColorImage(command_buffers[index],
			swapchain_images[image_index],
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_color, 
			1, &range);

		VkImageMemoryBarrier finalBarrier{};
		finalBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		finalBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		finalBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		finalBarrier.srcQueueFamilyIndex = graphics_queue_family_index;
		finalBarrier.dstQueueFamilyIndex = graphics_queue_family_index;
		finalBarrier.image = swapchain_images[image_index];
		finalBarrier.subresourceRange = range;

		vkCmdPipelineBarrier(
			command_buffers[index],
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &finalBarrier);

		vkEndCommandBuffer(command_buffers[index]);

		VkPipelineStageFlags dst_stage_mask[1] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		VkSubmitInfo submit_info = {};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.waitSemaphoreCount = 1;
		submit_info.pWaitSemaphores = &semaphore_image_ready[index];
		submit_info.pWaitDstStageMask = dst_stage_mask;
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &command_buffers[index];
		submit_info.signalSemaphoreCount = 1;
		submit_info.pSignalSemaphores = &semaphore_render_finish[index];
		vkQueueSubmit(queue, 1, &submit_info, fence_render_finish[index]);
		
		VkPresentInfoKHR present_info = {};
		present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		present_info.waitSemaphoreCount = 1;
		present_info.pWaitSemaphores = &semaphore_render_finish[index];
		present_info.swapchainCount = 1;
		present_info.pSwapchains = &swapchain;
		present_info.pImageIndices = &image_index;
		vkQueuePresentKHR(queue, &present_info);

		frame += 1;
	}

	/*
		Cleanup
	*/
	vkFreeCommandBuffers(device, command_pool, 1, command_buffers);
	vkDestroyCommandPool(device, command_pool, nullptr);
	//vkDestroySwapchainKHR(device, swapchain, nullptr);
	//vkDestroySurfaceKHR(instance, surface, nullptr);
	vkDestroyDevice(device, nullptr);
	vkDestroyInstance(instance, nullptr);
	glfwTerminate();

	return 0;
}