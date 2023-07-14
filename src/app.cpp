#include<iostream>
#include<vector>
#include<algorithm>
#include<limits>
#include<fstream>

#include<vk_mem_alloc.h>

#include <spdlog/spdlog.h>
#include <glm/gtx/transform.hpp>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_vulkan.h>

#include<tiny_obj_loader.h>
#include<tracy/tracy/Tracy.hpp>

#if LIVEPP_ENABLED
#include <LivePP/API/LPP_API_x64_CPP.h>
#endif

#include "app.h"

auto App::entry() -> void
{
	spdlog::info("Rendering demos startup");

#if LIVEPP_ENABLED
	lpp::LppDefaultAgent lppAgent = lpp::LppCreateDefaultAgent(L"sdk/LivePP");

	if (!lpp::LppIsValidDefaultAgent(&lppAgent))
	{
		throw std::runtime_error("Live++ error");
	}
	lppAgent.EnableModule(lpp::LppGetCurrentModulePath(), lpp::LPP_MODULES_OPTION_ALL_IMPORT_MODULES, nullptr, nullptr);

#endif

	platform->window_init(Window_Params{.name = "Rendering demos", .size = {1280, 720}});
	init_vulkan();

	is_running = true;
	frame_index = 0;

	while (!platform->window_requested_to_close())
	{
		platform->poll_events();

		ImGui_ImplVulkan_NewFrame();
		platform->imgui_new_frame();
		ImGui::NewFrame();

		bool yes = true;
		ImGui::ShowDemoWindow(&yes);

		draw(frame_index);

		frame_index += 1;
	}

	is_running = false;

	platform->window_destroy();
#if LIVEPP_ENABLED
	lpp::LppDestroyDefaultAgent(&lppAgent);
#endif
}

auto App::init_vulkan() -> bool
{
	create_instance();
	create_device();
	create_allocator();
	create_surface();
	create_swapchain();
	create_command_buffers();
	create_buffers();
	upload_vertex_data();
	create_depth_buffer();
	create_descriptors();
	create_sync_objects();
	create_shaders();
	create_pipeline();

	init_imgui();

	return true;
}

auto App::create_instance() -> void
{
	vkEnumerateInstanceVersion(&instance_version);
	if (instance_version < VK_API_VERSION_1_3)
	{
		throw std::runtime_error("Unsupported vulkan version");
	}

	const VkApplicationInfo application_info = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "Rendering Demos",
		.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
		.pEngineName = "Custom Engine",
		.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
		.apiVersion = VK_API_VERSION_1_3,
	};

	std::vector<VkValidationFeatureEnableEXT> enabled_validation_features = {
		VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
		VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
	};

	VkValidationFeaturesEXT validation_features = {
		.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
		.enabledValidationFeatureCount = (uint32_t) enabled_validation_features.size(),
		.pEnabledValidationFeatures = enabled_validation_features.data(),
	};

	const std::vector<const char *> enabled_validation_layers = {
		"VK_LAYER_KHRONOS_validation"
	};

	auto required_platform_extensions = platform->get_required_extensions();

	VkInstanceCreateInfo instance_create_info = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pNext = &validation_features,
		.pApplicationInfo = &application_info,
		.enabledLayerCount = (uint32_t) enabled_validation_layers.size(),
		.ppEnabledLayerNames = enabled_validation_layers.data(),
		.enabledExtensionCount = static_cast<uint32_t>(required_platform_extensions.size()),
		.ppEnabledExtensionNames = required_platform_extensions.data(),
	};

	auto vk_err = vkCreateInstance(&instance_create_info, nullptr, &instance);
	switch (vk_err)
	{
		case VK_SUCCESS:
			break;
		case VK_ERROR_LAYER_NOT_PRESENT:
			throw std::runtime_error("Layer not present!");
		case VK_ERROR_EXTENSION_NOT_PRESENT:
			throw std::runtime_error("Extensions not present!");
		case VK_ERROR_INCOMPATIBLE_DRIVER:
			throw std::runtime_error("Incompatibilee driver!");
		case VK_ERROR_INITIALIZATION_FAILED:
			throw std::runtime_error("Initialization failed!");
		default:
			throw std::runtime_error("Could not create instance!");
	}

	std::cout << "Following validation layers loaded (1):\n";
	for (auto &layer: enabled_validation_layers)
	{
		std::cout << "\t" << layer << "\n";
	}
}

/* Provides string names for [VkPhysicalDeviceType] */
const char *deviceTypesNames[5] = {
	"OTHER",
	"INTEGRATED_GPU",
	"DISCRETE_GPU",
	"VIRTUAL_GPU",
	"CPU"
};

auto App::create_device() -> void
{
	VkPhysicalDeviceProperties physical_device_properties;
	uint32_t physical_device_count, enumerated = 0;
	VkPhysicalDevice pdevice_buffer[10];
	VkResult enum_result;

	std::cout << "Found following physical devices:\n";
	do
	{
		physical_device_count = 10;
		enum_result = vkEnumeratePhysicalDevices(instance, &physical_device_count, pdevice_buffer);

		if ((enumerated == 0 && physical_device_count == 0)
			|| enum_result == VK_ERROR_OUT_OF_HOST_MEMORY)
		{
			break; // TODO: Panic
		}

		for (uint8_t i = 0; i < physical_device_count; i++)
		{
			VkPhysicalDevice candidate = pdevice_buffer[i];
			VkPhysicalDeviceProperties candidate_props;
			vkGetPhysicalDeviceProperties(candidate, &candidate_props);

			/* Compare physical device. For now will select first discrete gpu */
			if (enumerated == 0)
			{
				physical_device = candidate;
				physical_device_properties = candidate_props;
			}

			if (candidate_props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU
				&& physical_device_properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
			{
				physical_device = candidate;
				physical_device_properties = candidate_props;
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

	std::cout << "Selected " << physical_device_properties.deviceName << "\n";

	physical_device_limits = physical_device_properties.limits;

	/*
		Queue selection
	*/
	uint32_t queue_family_count;
	vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, nullptr);
	auto *queue_family_props = new VkQueueFamilyProperties[queue_family_count];
	vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_family_props);

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

	gfx_queue_family_index = -1;
	for (uint32_t i = 0; i < queue_family_count; i++)
	{
		if (queue_family_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
		{
			gfx_queue_family_index = i;
			break;
		}
	}
	if (gfx_queue_family_index == -1) {} // TOOD: Panic
	std::cout << "Selected queue family with index: " << gfx_queue_family_index << "\n";

	/*
		Logic device and queue creation
	*/
	float queue_prorities = 1.0;
	VkDeviceQueueCreateInfo queue_create_info = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = gfx_queue_family_index,
		.queueCount = 1,
		.pQueuePriorities = &queue_prorities,
	};

	const std::vector<const char *> device_extensions = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME
	};

	VkPhysicalDeviceVulkan13Features device_13_features = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
		.synchronization2 = true,
		.dynamicRendering = true,
	};

	VkDeviceCreateInfo device_create_info = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = &device_13_features,
		.queueCreateInfoCount = 1,
		.pQueueCreateInfos = &queue_create_info,
		.enabledExtensionCount = (uint32_t) device_extensions.size(),
		.ppEnabledExtensionNames = device_extensions.data(),
	};

	vkCreateDevice(physical_device, &device_create_info, nullptr, &device);

	//	if (vk_err != VK_SUCCESS) {} // TODO: Panic

	vkGetDeviceQueue(device, gfx_queue_family_index, 0, &gfx_queue);
}

auto App::create_allocator() -> void {
	VmaAllocatorCreateInfo create_info = {
		.flags = VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT, // TODO: add device_address support
		.physicalDevice = physical_device,
		.device = device,
		.instance = instance,
		.vulkanApiVersion = VK_API_VERSION_1_3,
	};
	vmaCreateAllocator(&create_info, &vma_allocator);
}

auto App::create_surface() -> void
{
	if (!platform->check_presentation_support(instance, physical_device, gfx_queue_family_index))
	{
		throw std::runtime_error("Platform does not support presentation!");
	}

	platform->create_surface(instance, &surface);
}

auto App::create_swapchain(bool recreate) -> void
{
	uint32_t present_modes_count;
	vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &present_modes_count, nullptr);
	std::vector<VkPresentModeKHR> present_modes(present_modes_count);
	vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &present_modes_count, present_modes.data());
	VkPresentModeKHR selected_present_mode = VK_PRESENT_MODE_FIFO_KHR; // Always supported
	for (const auto &present_mode: present_modes)
	{
		if (present_mode == VK_PRESENT_MODE_MAILBOX_KHR)
		{
			selected_present_mode = present_mode;
			break;
		}
	}

	uint32_t formats_count;
	vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &formats_count, nullptr);
	std::vector<VkSurfaceFormatKHR> formats(formats_count);
	vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &formats_count, formats.data());
	VkSurfaceFormatKHR selected_surface_format = formats[0];
	for (const auto &format: formats)
	{
		if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
		{
			selected_surface_format = format;
			break;
		}
	}
	swapchain_image_format = selected_surface_format.format;

	// Technically, we should query for capabilities for selected present mode, but we're using swapchain
	// images only as color attachment, so it doesn't matter since color attachment usage is required anyway.
	VkSurfaceCapabilitiesKHR surface_capabilities;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &surface_capabilities);

	window_extent = surface_capabilities.currentExtent;

	// Special value indicating that surface size will be determined by swapchain
	if (window_extent.width == 0xFFFFFFFF && window_extent.width == 0xFFFFFFFF) {
		auto size = platform->window_get_size();

		window_extent = {size.width, size.height};
		window_extent.width = std::clamp(
			window_extent.width,
			surface_capabilities.minImageExtent.width,
			surface_capabilities.maxImageExtent.width);
		window_extent.height = std::clamp(
			window_extent.height,
			surface_capabilities.minImageExtent.height,
			surface_capabilities.maxImageExtent.height);
	}

	uint32_t image_count = surface_capabilities.minImageCount + 1;
	if (surface_capabilities.maxImageCount > 0 && image_count > surface_capabilities.maxImageCount)
	{
		image_count = surface_capabilities.maxImageCount;
	}

	VkSwapchainCreateInfoKHR swapchain_create_info = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = surface,
		.minImageCount = image_count,
		.imageFormat = selected_surface_format.format,
		.imageColorSpace = selected_surface_format.colorSpace,
		.imageExtent = window_extent,
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.preTransform = surface_capabilities.currentTransform,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = selected_present_mode,
		.clipped = true,
		.oldSwapchain = recreate ? swapchain : VK_NULL_HANDLE, // Is returning to the same variable fine?
	};
	vkCreateSwapchainKHR(device, &swapchain_create_info, nullptr, &swapchain);

	vkGetSwapchainImagesKHR(device, swapchain, &swapchain_images_count, nullptr);
	swapchain_images = std::vector<VkImage>(swapchain_images_count);
	vkGetSwapchainImagesKHR(device, swapchain, &swapchain_images_count, swapchain_images.data());

	swapchain_image_views = std::vector<VkImageView>(swapchain_images_count);
	for (uint32_t image_index = 0; image_index < swapchain_images_count; image_index++)
	{
		VkImageViewCreateInfo image_view_create_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = swapchain_images[image_index],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = swapchain_image_format,
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

		vkCreateImageView(device, &image_view_create_info, nullptr, &swapchain_image_views[image_index]);
	}
}

auto App::create_command_buffers() -> void
{
	VkCommandPoolCreateInfo command_pool_create_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = gfx_queue_family_index,
	};
	vkCreateCommandPool(device, &command_pool_create_info, nullptr, &command_pool);

	command_buffers = std::vector<VkCommandBuffer>(swapchain_images_count);
	VkCommandBufferAllocateInfo command_buffer_allocate_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = command_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = swapchain_images_count,
	};
	vkAllocateCommandBuffers(device, &command_buffer_allocate_info, command_buffers.data());
}

auto App::create_buffers() -> void {

	// Vertex buffer
	{
		VkBufferCreateInfo vertex_buffer_create_info = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = 70000,
			.usage = VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		};

		VmaAllocationCreateInfo vma_vertex_buffer_create_info = {
			.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
			.usage = VMA_MEMORY_USAGE_AUTO,
		};

		vmaCreateBuffer(vma_allocator,
						&vertex_buffer_create_info,
						&vma_vertex_buffer_create_info,
						&vertex_buffer.buffer,
						&vertex_buffer.allocation,
						nullptr);
		vmaMapMemory(vma_allocator, vertex_buffer.allocation, &vertex_buffer_ptr);
	}

	// Uniform buffer
	{
		VkBufferCreateInfo buffer_create_info = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = 1000,
			.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		};

		VmaAllocationCreateInfo vma_buffer_create_info = {
			.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
			.usage = VMA_MEMORY_USAGE_AUTO,
		};

		vmaCreateBuffer(vma_allocator,
						&buffer_create_info,
						&vma_buffer_create_info,
						&frame_data_buffer.buffer,
						&frame_data_buffer.allocation,
						nullptr);
		vmaMapMemory(vma_allocator, frame_data_buffer.allocation, &frame_data_buffer_ptr);
	}
}

const std::vector<float> VERTEX_DATA = {
	0.5, 0.5, 0.0, 0.0, 0.0, 1.0,
	-0.5, 0.5, 0.0, 1.0, 0.0, 0.0,
	0.0, -0.5, 0.0, 0.0, 1.0, 0.0,
};

auto App::upload_vertex_data() -> void {
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn, err;
	auto filename = "assets/suzanne/suzanne.obj";

	tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filename);

	if (!warn.empty())
	{
		std::cout << "WARN: " << warn << std::endl;
	}

	if (!err.empty())
	{
		throw std::runtime_error(err);
	}

	std::vector<float> vertex_data = {};
	vertices_count = 0;

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

				vertex_data.insert(vertex_data.end(), {vx, vy, vz, nx, ny, nz});
				vertices_count += 1;
			}

			index_offset += 3;
		}
	}

	memcpy(vertex_buffer_ptr, vertex_data.data(), vertex_data.size() * sizeof(float));
	vmaFlushAllocation(vma_allocator, vertex_buffer.allocation, 0, vertex_data.size() * sizeof(float));
}


auto App::create_depth_buffer() -> void {
	VkImageCreateInfo image_create_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.flags = 0,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = VK_FORMAT_D32_SFLOAT,
		.extent = { window_extent.width, window_extent.height, 1 },
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	VmaAllocationCreateInfo vma_allocation_info = {
		.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
	};

	vmaCreateImage(vma_allocator, &image_create_info, &vma_allocation_info,
				   &depth_buffer.image, &depth_buffer.allocation,
				   nullptr);

	VkImageViewCreateInfo image_view_create_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = depth_buffer.image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = VK_FORMAT_D32_SFLOAT,
		.components = {
			.r = VK_COMPONENT_SWIZZLE_IDENTITY,
			.g = VK_COMPONENT_SWIZZLE_IDENTITY,
			.b = VK_COMPONENT_SWIZZLE_IDENTITY,
			.a = VK_COMPONENT_SWIZZLE_IDENTITY,
		},
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
	};

	vkCreateImageView(device, &image_view_create_info, nullptr, &depth_buffer_view);
}

auto App::create_descriptors() -> void {

	{
		VkDescriptorSetLayoutBinding set_layout_bindings[1] = {
			{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
			}
		};

		VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.flags = 0,
			.bindingCount = 1,
			.pBindings = set_layout_bindings,
		};

		vkCreateDescriptorSetLayout(device, &descriptor_set_layout_create_info, nullptr, &per_frame_descriptor_set_layout);
	}

	{
		VkDescriptorPoolSize pool_sizes[1] = {
			{
				.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				.descriptorCount = 10,
			},
		};

		VkDescriptorPoolCreateInfo descriptor_pool_create_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.flags = 0,
			.maxSets = 10,
			.poolSizeCount = 1,
			.pPoolSizes = pool_sizes,
		};

		vkCreateDescriptorPool(device, &descriptor_pool_create_info, nullptr, &descriptor_pool);
	}

	{
		VkDescriptorSetAllocateInfo descriptor_set_allocate_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = descriptor_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &per_frame_descriptor_set_layout,
		};

		vkAllocateDescriptorSets(device, &descriptor_set_allocate_info, &per_frame_descriptor_set);

		VkDescriptorBufferInfo descriptor_buffer_info = {
			.buffer = frame_data_buffer.buffer,
			.offset = 0,
			.range = 4,
		};

		VkWriteDescriptorSet descriptor_set_write = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = per_frame_descriptor_set,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.pBufferInfo = &descriptor_buffer_info,
		};

		vkUpdateDescriptorSets(device, 1, &descriptor_set_write, 0, nullptr);
	}
}

auto App::create_sync_objects() -> void
{
	VkSemaphoreCreateInfo semaphore_create_info = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	};

	vkCreateSemaphore(device, &semaphore_create_info, nullptr, &render_semaphore[0]);
	vkCreateSemaphore(device, &semaphore_create_info, nullptr, &render_semaphore[1]);
	vkCreateSemaphore(device, &semaphore_create_info, nullptr, &present_semaphore[0]);
	vkCreateSemaphore(device, &semaphore_create_info, nullptr, &present_semaphore[1]);

	VkFenceCreateInfo fence_create_info = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT,
	};
	vkCreateFence(device, &fence_create_info, nullptr, &render_fence[0]);
	vkCreateFence(device, &fence_create_info, nullptr, &render_fence[1]);
}

auto load_file(const char* file_path) -> std::vector<uint32_t>
{
	std::ifstream file(file_path, std::ios::ate | std::ios::binary);

	if (!file.is_open())
	{
		throw std::runtime_error("Shader file open!");
	}

	size_t file_size = (size_t) file.tellg();
	std::vector<uint32_t> buffer(file_size / sizeof(uint32_t));

	file.seekg(0);
	file.read((char*) buffer.data(), file_size);
	file.close();

	return buffer;
}

auto App::create_shaders() -> void
{
	auto vert_shader_code = load_file("data/shaders/triangle_vert.spv");
	VkShaderModuleCreateInfo vert_shader_create_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = vert_shader_code.size() * sizeof(uint32_t),
		.pCode = vert_shader_code.data(),
	};
	vkCreateShaderModule(device, &vert_shader_create_info, nullptr, &vertex_shader);

	auto frag_shader_code = load_file("data/shaders/triangle_frag.spv");
	VkShaderModuleCreateInfo frag_shader_create_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = frag_shader_code.size() * sizeof(uint32_t),
		.pCode = frag_shader_code.data(),
	};
	vkCreateShaderModule(device, &frag_shader_create_info, nullptr, &fragment_shader);
}

auto App::create_pipeline() -> void
{
	/*
	 * Pipeline layout
	 */
	VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.flags = 0,
		.setLayoutCount = 1,
		.pSetLayouts = &per_frame_descriptor_set_layout,
	};
	vkCreatePipelineLayout(device, &pipeline_layout_create_info, nullptr, &pipeline_layout);

	/*
	 * Pipeline
	 */

	VkPipelineShaderStageCreateInfo vert_stage = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_VERTEX_BIT,
		.module = vertex_shader,
		.pName = "main",
	};
	VkPipelineShaderStageCreateInfo frag_stage = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
		.module = fragment_shader,
		.pName = "main",
	};
	VkPipelineShaderStageCreateInfo stages[2] = {vert_stage, frag_stage};

	VkVertexInputBindingDescription binding_description = {
		.binding = 0,
		.stride = 6 * sizeof(float),
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
	};

	VkVertexInputAttributeDescription position_attribute_description = {
		.location = 0,
		.binding = 0,
		.format = VK_FORMAT_R32G32B32_SFLOAT,
		.offset = 0,
	};

	VkVertexInputAttributeDescription color_attribute_description = {
		.location = 1,
		.binding = 0,
		.format = VK_FORMAT_R32G32B32_SFLOAT,
		.offset = sizeof(float) * 3,
	};

	VkVertexInputAttributeDescription attributes[2] = {position_attribute_description, color_attribute_description};

	VkPipelineVertexInputStateCreateInfo vertex_input_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &binding_description,
		.vertexAttributeDescriptionCount = 2,
		.pVertexAttributeDescriptions = attributes,
	};

	VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	};

	VkPipelineRasterizationStateCreateInfo rasterization_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.depthClampEnable = VK_FALSE,
		.rasterizerDiscardEnable = VK_FALSE,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_NONE,
		.frontFace = VK_FRONT_FACE_CLOCKWISE,
		.depthBiasEnable = VK_FALSE,
		.depthBiasConstantFactor = 0.0f,
		.depthBiasClamp = 0.0f,
		.depthBiasSlopeFactor = 0.0f,
		.lineWidth = 1.0f,
	};

	VkPipelineMultisampleStateCreateInfo multisample_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
		.sampleShadingEnable = VK_FALSE,
		.minSampleShading = 1.0f,
		.pSampleMask = nullptr,
		.alphaToCoverageEnable = VK_FALSE,
		.alphaToOneEnable = VK_FALSE,
	};

	VkPipelineViewportStateCreateInfo viewport_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};

	VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dynamic_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = 2,
		.pDynamicStates = dynamic_states,
	};

	VkPipelineColorBlendAttachmentState color_blend_attachment_state = {
		.blendEnable = VK_FALSE,
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
	};

	VkPipelineColorBlendStateCreateInfo color_blend_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.logicOpEnable = VK_FALSE,
		.logicOp = VK_LOGIC_OP_COPY,
		.attachmentCount = 1,
		.pAttachments = &color_blend_attachment_state,
	};

	VkPipelineRenderingCreateInfo pipeline_rendering_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.colorAttachmentCount = 1,
		.pColorAttachmentFormats = &swapchain_image_format,
		.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT,
		.stencilAttachmentFormat = VK_FORMAT_UNDEFINED,
	};

	VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = true,
		.depthWriteEnable = true,
		.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
		.depthBoundsTestEnable = false,
		.stencilTestEnable = false,
	};

	VkGraphicsPipelineCreateInfo pipeline_create_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = &pipeline_rendering_create_info,
		.flags = 0,
		.stageCount = 2,
		.pStages = stages,
		.pVertexInputState = &vertex_input_state,
		.pInputAssemblyState = &input_assembly_state,
		.pViewportState = &viewport_state,
		.pRasterizationState = &rasterization_state,
		.pMultisampleState = &multisample_state,
		.pDepthStencilState = &depth_stencil_state,
		.pColorBlendState = &color_blend_state,
		.pDynamicState = &dynamic_state,
		.layout = pipeline_layout,
		.renderPass = VK_NULL_HANDLE,
		.subpass = 0,
		.basePipelineHandle = VK_NULL_HANDLE,
	};

	vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &pipeline);
}

auto App::init_imgui() -> void {
	{
		VkDescriptorPoolSize pool_sizes[] = {
			{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
			{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
			{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
		};

		VkDescriptorPoolCreateInfo pool_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
			.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes),
			.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes),
			.pPoolSizes = pool_sizes,
		};

		vkCreateDescriptorPool(device, &pool_info, nullptr, &imgui_descriptor_pool);
	}

	ImGui::CreateContext();
	ImGui::StyleColorsDark();

	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	io.Fonts->AddFontDefault();

	platform->imgui_init();

	ImGui_ImplVulkan_InitInfo init_info = {
		.Instance = instance,
		.PhysicalDevice = physical_device,
		.Device = device,
		.QueueFamily = gfx_queue_family_index,
		.Queue = gfx_queue,
		.PipelineCache = VK_NULL_HANDLE,
		.DescriptorPool = imgui_descriptor_pool,
		.MinImageCount = swapchain_images_count,
		.ImageCount = swapchain_images_count,
		.MSAASamples = VK_SAMPLE_COUNT_1_BIT,
		.UseDynamicRendering = true,
		.ColorAttachmentFormat = swapchain_image_format,
	};
	ImGui_ImplVulkan_Init(&init_info, VK_NULL_HANDLE);

	{
		VkCommandBuffer command_buffer = command_buffers[0];

		VkCommandBufferBeginInfo begin_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		};
		vkBeginCommandBuffer(command_buffer, &begin_info);

		ImGui_ImplVulkan_CreateFontsTexture(command_buffer);

		VkSubmitInfo end_info = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.commandBufferCount = 1,
			.pCommandBuffers = &command_buffer,
		};
		vkEndCommandBuffer(command_buffer);

		vkQueueSubmit(gfx_queue, 1, &end_info, VK_NULL_HANDLE);
		vkDeviceWaitIdle(device);

		ImGui_ImplVulkan_DestroyFontUploadObjects();
	}
}

auto App::draw(uint32_t frame) -> void
{
	ZoneScopedN("Draw");

	uint32_t index = frame % 2;

	vkWaitForFences(device, 1, &render_fence[index], true, UINT64_MAX);
	vkResetFences(device, 1, &render_fence[index]);

	uint32_t image_index;
	auto acquire_result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
						  present_semaphore[index], VK_NULL_HANDLE, &image_index);

	if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR || acquire_result == VK_SUBOPTIMAL_KHR)
	{
		// Recreate swapchain
		// Sync for image views in-flight:
		vkWaitForFences(device, 1, &render_fence[(frame + 1) % 2], true, UINT64_MAX);
		for (const auto &image_view : swapchain_image_views)
		{
			vkDestroyImageView(device, image_view, nullptr);
		}
		create_swapchain(true);
		vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,present_semaphore[index], VK_NULL_HANDLE, &image_index);
	}

	uint64_t current_per_frame_data_bytes_offset =
		(sizeof(FrameData) + physical_device_limits.minUniformBufferOffsetAlignment
		- (sizeof(FrameData) % physical_device_limits.minUniformBufferOffsetAlignment)) * index;

	// Upload per-frame data
	{
		glm::vec3 cam_pos = { 0.f, 0.f, -2.f };
		glm::mat4 view = glm::translate(glm::mat4(1.f), cam_pos);
		glm::mat4 projection = glm::perspective(
			glm::radians(70.f),
			1280.f / 720.f,
			0.1f,
			200.0f);
		projection[1][1] *= -1;
		glm::mat4 model = glm::rotate(
			glm::mat4{ 1.0f },
			glm::radians(static_cast<float>(frame) * 0.4f),
			glm::vec3(0, 1, 0));
		glm::mat4 render_matrix = projection * view * model;

		FrameData frame_data = {
			.render_matrix = render_matrix,
		};

		void* dst_pointer = reinterpret_cast<char*>(frame_data_buffer_ptr) + current_per_frame_data_bytes_offset;

		memcpy(dst_pointer, &frame_data, sizeof(FrameData));
		vmaFlushAllocation(vma_allocator, frame_data_buffer.allocation,
						   current_per_frame_data_bytes_offset, sizeof(FrameData));
	}

	VkCommandBufferBeginInfo begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	vkBeginCommandBuffer(command_buffers[index], &begin_info);

	VkImageSubresourceRange range = {
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel = 0,
		.levelCount = 1,
		.baseArrayLayer = 0,
		.layerCount = 1,
	};

	VkImageMemoryBarrier render_transition_barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, // Legal, since we're clearing this image on render load anyway
		.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = swapchain_images[image_index],
		.subresourceRange = range,
	};

	vkCmdPipelineBarrier(
		command_buffers[index],
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		0,
		0,nullptr,
		0,nullptr,
		1, &render_transition_barrier);


	VkClearValue color_clear_value = { .color = { .float32 = {0, 0, 0.2, 1} } };

	VkRenderingAttachmentInfo swapchain_attachment_info = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = swapchain_image_views[image_index],
		.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.resolveMode = VK_RESOLVE_MODE_NONE,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.clearValue = color_clear_value,
	};

	VkClearValue depth_clear_value = { .depthStencil = { .depth = 1 } };

	VkRenderingAttachmentInfo depth_attachment_info = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = depth_buffer_view,
		.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
		.resolveMode = VK_RESOLVE_MODE_NONE,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.clearValue = depth_clear_value,
	};

	VkRenderingInfo rendering_info = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.flags = 0,
		.renderArea = {
			.offset = {}, // Zero
			.extent = window_extent,
		},
		.layerCount = 1,
		.viewMask = 0,
		.colorAttachmentCount = 1,
		.pColorAttachments = &swapchain_attachment_info,
		.pDepthAttachment = &depth_attachment_info,
	};

	VkViewport viewport = {
		.x = 0,
		.y = 0,
		.width = (float) window_extent.width,
		.height = (float) window_extent.height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};
	vkCmdSetViewport(command_buffers[index], 0, 1, &viewport);

	VkRect2D scissor = { .offset = {}, .extent = window_extent, };
	vkCmdSetScissor(command_buffers[index], 0, 1, &scissor);

	uint32_t offset = current_per_frame_data_bytes_offset;
	vkCmdBindDescriptorSets(command_buffers[index],
							VK_PIPELINE_BIND_POINT_GRAPHICS,
							pipeline_layout,
							0,
							1, &per_frame_descriptor_set,
							1, &offset);

	vkCmdBeginRendering(command_buffers[index], &rendering_info);
	vkCmdBindPipeline(command_buffers[index], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	VkDeviceSize offsets = 0;
	vkCmdBindVertexBuffers(command_buffers[index], 0, 1, &vertex_buffer.buffer, &offsets);

	vkCmdDraw(command_buffers[index], vertices_count, 1, 0, 0);
	vkCmdEndRendering(command_buffers[index]);

	{
		VkRenderingAttachmentInfo imgui_pass_swapchain_attachment_info = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = swapchain_image_views[image_index],
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
				.extent = window_extent,
			},
			.layerCount = 1,
			.viewMask = 0,
			.colorAttachmentCount = 1,
			.pColorAttachments = &imgui_pass_swapchain_attachment_info,
		};
		vkCmdBeginRendering(command_buffers[index], &imgui_pass_rendering_info);
		ImGui::Render();
		ImDrawData *draw_data = ImGui::GetDrawData();
		ImGui_ImplVulkan_RenderDrawData(draw_data, command_buffers[index]);
		vkCmdEndRendering(command_buffers[index]);
	}

	VkImageMemoryBarrier present_transition_barrier {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = swapchain_images[image_index],
		.subresourceRange = range,
	};

	vkCmdPipelineBarrier(
		command_buffers[index],
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		0,
		0, nullptr,
		0, nullptr,
		1, &present_transition_barrier);

	vkEndCommandBuffer(command_buffers[index]);

	VkPipelineStageFlags dst_stage_mask[1] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
	VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &present_semaphore[index],
		.pWaitDstStageMask = dst_stage_mask,
		.commandBufferCount = 1,
		.pCommandBuffers = &command_buffers[index],
		.signalSemaphoreCount = 1,
		.pSignalSemaphores = &render_semaphore[index],
	};
	vkQueueSubmit(gfx_queue, 1, &submit_info, render_fence[index]);

	VkPresentInfoKHR present_info = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &render_semaphore[index],
		.swapchainCount = 1,
		.pSwapchains = &swapchain,
		.pImageIndices = &image_index,
	};
	vkQueuePresentKHR(gfx_queue, &present_info);
}