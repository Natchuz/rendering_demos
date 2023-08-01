#include<iostream>
#include<vector>
#include<algorithm>
#include<limits>
#include<fstream>
#include<numbers>

#include <spdlog/spdlog.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/transform.hpp>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_vulkan.h>

#include<tiny_obj_loader.h>
#include<tracy/tracy/Tracy.hpp>
#include <set>

#include <stb_image.h>

#include "app.h"

auto Camera::init() -> void
{
	position = {0.f, 0.f, -2.f};
	front = {0, 0, 1};
	up = {0, 1, 0};
	right = {1, 0, 0};
	yaw = 90;
	pitch = 0;
	velocity = 20;
	speed_multiplier = 5;
	mouse_sensitivity = 0.05;
}

auto Camera::update(Input* input, float delta_time) -> void
{
	// FIXME: this camera works... but there are many inconsistencies with system handedness
	// that should be fixed ASAP as I have a feeling that bumping into them will be very common

	ImGui::Begin("Camera");
	ImGui::Text("Yaw: %f", yaw);
	ImGui::Text("Pitch: %f", pitch);
	ImGui::Text("Front: %f %f %f", front.x, front.y, front.z);
	ImGui::Text("Up: %f %f %f", up.x, up.y, up.z);
	ImGui::Text("Right: %f %f %f", right.x, right.y, right.z);
	ImGui::DragFloat3("Position:", glm::value_ptr(position), 0.1, -100, 100);
	ImGui::DragFloat("Mouse sens:", &mouse_sensitivity, 0.005, 0.001, 1);
	ImGui::DragFloat("Camera speed:", &velocity, 0.005, 0.001, 50);
	ImGui::DragFloat("Fast multiplier:", &speed_multiplier, 0.005, 1, 10);
	ImGui::Text("dx: %f  dy: %f", input->mouse_x_delta, input->mouse_y_delta);
	if (input->buttons_states[Input::Button::KEYBOARD_BUTTON_LEFT_SHIFT])
	{
		ImGui::Text("SHIFT");
	}
	ImGui::End();

	// Focus window
	if (input->buttons_states[Input::Button::MOUSE_BUTTON_RIGHT] == Input::Button_State::PRESSED && !ImGui::GetIO().WantCaptureMouse)
	{
		input->inhibit_cursor = true;
	}
	else
	{
		input->inhibit_cursor = false;
		return;
	}

	const glm::vec3 WORLD_UP = {0, 1, 0};

	// Camera rotation
	yaw += static_cast<float>(input->mouse_x_delta) * mouse_sensitivity;
	pitch += static_cast<float>(input->mouse_y_delta) * mouse_sensitivity;
	if (pitch > 89.0)
	{
		pitch = 89.0;
	}
	if (pitch < -89.0)
	{
		pitch = -89.0;
	}
	front = glm::normalize(glm::vec3{
		cos(glm::radians(yaw)) * cos(glm::radians(pitch)),
		sin(glm::radians(pitch)),
		sin(glm::radians(yaw)) * cos(glm::radians(pitch)),
	});
	right = glm::normalize(glm::cross(WORLD_UP, front));
	up = glm::normalize(glm::cross(front, right));

	// Movement
	glm::vec3 delta_position = {};
	if (input->buttons_states[Input::Button::KEYBOARD_BUTTON_W]) delta_position += front;
	if (input->buttons_states[Input::Button::KEYBOARD_BUTTON_S]) delta_position -= front;
	if (input->buttons_states[Input::Button::KEYBOARD_BUTTON_A]) delta_position += right; // Handedness whatever
	if (input->buttons_states[Input::Button::KEYBOARD_BUTTON_D]) delta_position -= right;
	if (input->buttons_states[Input::Button::KEYBOARD_BUTTON_E]) delta_position += WORLD_UP;
	if (input->buttons_states[Input::Button::KEYBOARD_BUTTON_Q]) delta_position -= WORLD_UP;

	if (delta_position != glm::zero<glm::vec3>()) // TODO swap for epsilon compare
	{
		position += glm::normalize(delta_position) * (velocity * delta_time *
			(input->buttons_states[Input::Button::KEYBOARD_BUTTON_LEFT_SHIFT] ? speed_multiplier : 1));
	}
}

auto Camera::get_view_matrix() const -> glm::mat4x4
{
	return glm::lookAt(position, position + front, up);
}

auto Timings::update_timings() -> void
{
	// When called at start of a frame, frame_time_stamp indicated previous frame
	//int64_t now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
	//delta_time = static_cast<float>(now - frame_time_stamp) / 1000000000.f;
	//frame_time_stamp = now;
	auto now = std::chrono::high_resolution_clock::now();
	auto time_span = std::chrono::duration_cast<std::chrono::duration<float>>(now - frame_time_stamp);
	delta_time = time_span.count();
	delta_time = std::clamp(delta_time, 0.0f, 0.5f); // Temporary fix
	frame_time_stamp = now;
}

size_t clamp_size_to_alignment(size_t block_size, size_t alignment)
{
	if (alignment > 0) {
		return (block_size + alignment - 1) & ~(alignment - 1);
	}
	return block_size;
}

template <class... Args>
auto command_buffer_region_begin(VkCommandBuffer buffer, std::format_string<Args...> fmt, Args&&... args) -> void
{
	const auto formatted = std::vformat(fmt.get(), std::make_format_args(args...));
	VkDebugUtilsLabelEXT label_info = {
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
		.pLabelName = formatted.c_str(),
	};
	vkCmdBeginDebugUtilsLabelEXT(buffer, &label_info);
}

auto command_buffer_region_end(VkCommandBuffer buffer) -> void
{
	vkCmdEndDebugUtilsLabelEXT(buffer);
}

template <class... Args>
auto command_buffer_insert_marker(VkCommandBuffer buffer, std::format_string<Args...> fmt, Args&&... args) -> void
{
	const auto formatted = std::vformat(fmt.get(), std::make_format_args(args...));
	VkDebugUtilsLabelEXT label_info = {
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
		.pLabelName = formatted.c_str(),
	};
	vkCmdInsertDebugUtilsLabelEXT(buffer, &label_info);
}

template <class... Args>
auto queue_region_begin(VkQueue queue, std::format_string<Args...> fmt, Args&&... args) -> void
{
	const auto formatted = std::vformat(fmt.get(), std::make_format_args(args...));
	VkDebugUtilsLabelEXT label_info = {
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
		.pLabelName = formatted.c_str(),
	};
	vkQueueBeginDebugUtilsLabelEXT(queue, &label_info);
}

auto queue_region_end(VkQueue queue) -> void
{
	vkQueueEndDebugUtilsLabelEXT(queue);
}

template <class... Args>
auto queue_insert_marker(VkQueue queue, std::format_string<Args...> fmt, Args&&... args) -> void
{
	const auto formatted = std::vformat(fmt.get(), std::make_format_args(args...));
	VkDebugUtilsLabelEXT label_info = {
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
		.pLabelName = formatted.c_str(),
	};
	vkQueueInsertDebugUtilsLabelEXT(queue, &label_info);
}

auto Hot_Reload::init() -> void
{
#if LIVEPP_ENABLED
	lpp_agent = lpp::LppCreateSynchronizedAgent(L"sdk/LivePP");

	if (!lpp::LppIsValidSynchronizedAgent(&lpp_agent))
	{
		throw std::runtime_error("Live++ error: invalid agent");
	}
	lpp_agent.EnableModule(lpp::LppGetCurrentModulePath(), lpp::LPP_MODULES_OPTION_ALL_IMPORT_MODULES,
						   nullptr, nullptr);

	spdlog::info("Enabled Live++ agent");
#endif
}

auto Hot_Reload::close() -> void
{
#if LIVEPP_ENABLED
	lpp::LppDestroySynchronizedAgent(&lpp_agent);

	spdlog::info("Destroyed Live++ agent");
#endif
}

auto Hot_Reload::reload_if_needed(App* app) -> void
{
#if LIVEPP_ENABLED
	if (lpp_agent.WantsReload())
	{
		auto start_time = std::chrono::high_resolution_clock::now();
		spdlog::info("-----[ Beginning hot reload (frame {}) ]-----", app->frame_number);
		spdlog::info("Cleanup...");

		clean_up(app);

		lpp_agent.CompileAndReloadChanges(lpp::LPP_RELOAD_BEHAVIOUR_WAIT_UNTIL_CHANGES_ARE_APPLIED);
		spdlog::info("Live++ reloaded. Reinitializing...");

		reinitialize(app);

		auto finish_time = std::chrono::high_resolution_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::duration<float>>(finish_time - start_time);
		spdlog::info("-----[ Hot reload done! ({} s) ]-----", duration.count());
	}
#endif
}

auto Hot_Reload::build_hot_reload_window() -> void
{
	ImGui::Begin("Hot reloading");
#if LIVEPP_ENABLED

	ImGui::Text("Live++ version %s", LPP_VERSION);
	ImGui::SeparatorText("On reload:");
	ImGui::Checkbox("Rebuild frame data", &rebuild_frame_data);

	ImGui::Separator();
	if (ImGui::Button("Schedule hot reload"))
	{
		lpp_agent.ScheduleReload();
	}
#else
	ImGui::Text("Live++ disabled!");
#endif
	ImGui::End();
}

auto Hot_Reload::clean_up(App* app) -> void
{
#if LIVEPP_ENABLED
	vkDeviceWaitIdle(app->device);

	if (rebuild_frame_data)
	{
		app->destroy_frame_data();
	}
#endif
}

auto Hot_Reload::reinitialize(App* app) -> void
{
#if LIVEPP_ENABLED
	if (rebuild_frame_data)
	{
		app->create_frame_data();
	}
#endif
}

auto App::entry() -> void
{
	spdlog::info("Rendering demos startup");
	hot_reload.init();

	platform->window_init(Window_Params{.name = "Rendering demos", .size = {1280, 720}});

	frames_in_flight = 2;

	create_instance();
	create_device();
	create_allocator();
	init_swapchain(this);

	create_frame_data();

	create_buffers();
	depth_buffer_create(this);
	create_shaders();

	init_imgui();

	load_scene_data();
	create_descriptors();
	create_pipeline();

	camera.init();

	is_running = true;
	frame_number = 0;

	rotation = 0;

	spdlog::info("Initialization done, running");

	while (!platform->window_requested_to_close())
	{
		hot_reload.reload_if_needed(this);

		// Timings
		timings.update_timings();

		frame_id = frame_number % frames_in_flight;

		platform->poll_events();
		platform->fill_input(&input);

		ImGui_ImplVulkan_NewFrame();
		platform->imgui_new_frame();
		ImGui::NewFrame();

		hot_reload.build_hot_reload_window();

		rotation += static_cast<float>(2 * 3.14 / 3 * timings.delta_time);

		camera.update(&input, timings.delta_time);

		draw();

		frame_number += 1;
	}

	is_running = false;

	deinit_swapchain(this);
	platform->window_destroy();

	hot_reload.close();
}

template <typename Handle, class... Args>
auto App::name_object(VkObjectType object_type, Handle handle, std::format_string<Args...> fmt, Args&&... args) -> void
{
	assert(device != 0);

	const auto formatted = std::vformat(fmt.get(), std::make_format_args(args...));

	VkDebugUtilsObjectNameInfoEXT name_info = {
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
		.objectType = object_type,
		.objectHandle = reinterpret_cast<uint64_t>(handle),
		.pObjectName = formatted.c_str(),
	};
	vkSetDebugUtilsObjectNameEXT(device, &name_info);
}


// Logger for Vulkan's EXT_debug_utils
// As written in specs, will always return false.
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_utils_messenger_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
															  VkDebugUtilsMessageTypeFlagsEXT message_type,
															  const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
															  void *user_data)
{
#ifndef ENABLE_VULKAN_LOADER_MESSAGES
	if (callback_data->messageIdNumber == 0) // Loader messages usually have id number = 0
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

auto App::create_instance() -> void
{
	ZoneScopedN("Instance creation");

	// Load base pointers
	volkInitialize();

	vkEnumerateInstanceVersion(&instance_version);
	if (instance_version < VK_API_VERSION_1_3)
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
	// Note that applications like RenderDoc may report VK_EXT_debug_utils twice, thus we need to take care
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
		.pApplicationName = "Rendering Demos",
		.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
		.pEngineName = "Custom Engine",
		.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
		.apiVersion = VK_API_VERSION_1_3,
	};

	const VkInstanceCreateInfo instance_create_info = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pNext = &debug_utils_messenger_create_info,
		.pApplicationInfo = &application_info,
		.enabledLayerCount = 0,
		.ppEnabledLayerNames = nullptr,
		.enabledExtensionCount = static_cast<uint32_t>(enabled_extensions.size()),
		.ppEnabledExtensionNames = enabled_extensions.data(),
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
			throw std::runtime_error("Incompatible driver!");
		case VK_ERROR_INITIALIZATION_FAILED:
			throw std::runtime_error("Initialization failed!");
		default:
			throw std::runtime_error("Could not create instance!");
	}

	// Load instance and device pointers
	volkLoadInstance(instance);

	vkCreateDebugUtilsMessengerEXT(instance, &debug_utils_messenger_create_info, nullptr, &debug_util_messenger);
}

const std::set<std::string> DEVICE_REQUIRED_EXTENSIONS = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

auto App::create_device() -> void
{
	ZoneScopedN("Device selection and creation");

	// Query available physical devices
	uint32_t available_physical_devices_count;
	vkEnumeratePhysicalDevices(instance, &available_physical_devices_count, nullptr);
	std::vector<VkPhysicalDevice> available_physical_devices(available_physical_devices_count);
	vkEnumeratePhysicalDevices(instance, &available_physical_devices_count, available_physical_devices.data());

	// Device selection algorithm presented here is completely over the top and unnecessary, since a simple
	// heuristic of "just pick the first discrete GPU" would be more than fine, but I kinda felt fancy
	// and did this monstrosity, which is probably the most specs-certified way there is.

	struct Device_Candidate
	{
		VkPhysicalDevice physical_device;
		Device_Properties device_properties;
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
			auto support_presentation = platform->check_presentation_support(instance,
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
	physical_device = selected_candidate.physical_device;
	device_properties = selected_candidate.device_properties;
	gfx_queue_family_index = selected_candidate.gfx_family_queue_index;

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

	auto vk_err = vkCreateDevice(physical_device, &device_create_info, nullptr, &device);
	switch (vk_err)
	{
		case VK_SUCCESS:
			break;
		case VK_ERROR_FEATURE_NOT_PRESENT:
			throw std::runtime_error("Feature not present!"); // If this happens, we have a bug!
		case VK_ERROR_EXTENSION_NOT_PRESENT:
			throw std::runtime_error("Extensions not present!"); // If this happens, we have a bug!
		case VK_ERROR_INITIALIZATION_FAILED:
			throw std::runtime_error("Initialization failed!");
		default:
			throw std::runtime_error("Could not create device!");
	}
	volkLoadDevice(device);
	vkGetDeviceQueue(device, gfx_queue_family_index, 0, &gfx_queue);

	// Set debug names for objects
	name_object(VK_OBJECT_TYPE_PHYSICAL_DEVICE, physical_device, "Main physical device");
	name_object(VK_OBJECT_TYPE_DEVICE, device, "Main device");
	name_object(VK_OBJECT_TYPE_QUEUE, gfx_queue, "Main graphics queue");
}

auto App::create_allocator() -> void {
	VmaVulkanFunctions vulkan_functions = {
		.vkGetInstanceProcAddr = vkGetInstanceProcAddr,
		.vkGetDeviceProcAddr = vkGetDeviceProcAddr,
		.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties,
		.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties,
		.vkAllocateMemory = vkAllocateMemory,
		.vkFreeMemory = vkFreeMemory,
		.vkMapMemory = vkMapMemory,
		.vkUnmapMemory = vkUnmapMemory,
		.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges,
		.vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges,
		.vkBindBufferMemory = vkBindBufferMemory,
		.vkBindImageMemory = vkBindImageMemory,
		.vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements,
		.vkGetImageMemoryRequirements = vkGetImageMemoryRequirements,
		.vkCreateBuffer = vkCreateBuffer,
		.vkDestroyBuffer = vkDestroyBuffer,
		.vkCreateImage = vkCreateImage,
		.vkDestroyImage = vkDestroyImage,
		.vkCmdCopyBuffer = vkCmdCopyBuffer,
		.vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2KHR,
		.vkGetImageMemoryRequirements2KHR = vkGetImageMemoryRequirements2KHR,
		.vkBindBufferMemory2KHR = vkBindBufferMemory2KHR,
		.vkBindImageMemory2KHR = vkBindImageMemory2KHR,
		.vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2KHR,
		.vkGetDeviceBufferMemoryRequirements = vkGetDeviceBufferMemoryRequirements,
		.vkGetDeviceImageMemoryRequirements = vkGetDeviceImageMemoryRequirements,
	};

	VmaAllocatorCreateInfo create_info = {
		.flags = VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT, // TODO: add device_address support
		.physicalDevice = physical_device,
		.device = device,
		.pVulkanFunctions = &vulkan_functions,
		.instance = instance,
		.vulkanApiVersion = VK_API_VERSION_1_3,
	};
	vmaCreateAllocator(&create_info, &vma_allocator);
}

void init_swapchain(App* app)
{
	ZoneScopedN("Swapchain initialization");

	Swapchain* swapchain = &app->swapchain; // Shortcut

	app->platform->create_surface(app->instance, &swapchain->surface);

	uint32_t present_modes_count;
	vkGetPhysicalDeviceSurfacePresentModesKHR(app->physical_device, swapchain->surface, &present_modes_count, nullptr);
	swapchain->present_modes = std::vector<VkPresentModeKHR>(present_modes_count);
	vkGetPhysicalDeviceSurfacePresentModesKHR(app->physical_device, swapchain->surface, &present_modes_count,
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
	vkGetPhysicalDeviceSurfaceFormatsKHR(app->physical_device, swapchain->surface, &formats_count, nullptr);
	swapchain->formats = std::vector<VkSurfaceFormatKHR>(formats_count);
	vkGetPhysicalDeviceSurfaceFormatsKHR(app->physical_device, swapchain->surface, &formats_count,
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
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(app->physical_device, swapchain->surface,
											  &swapchain->surface_capabilities);

	auto surface_extent = swapchain->surface_capabilities.currentExtent;
	// Special value indicating that surface size will be determined by swapchain
	if (surface_extent.width == 0xFFFFFFFF && surface_extent.height == 0xFFFFFFFF) {
		auto size = app->platform->window_get_size();
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
	vkCreateSwapchainKHR(app->device, &swapchain_create_info, nullptr, &swapchain->handle);

	uint32_t image_count;
	vkGetSwapchainImagesKHR(app->device, swapchain->handle, &image_count, nullptr);
	std::vector<VkImage> images(image_count);
	vkGetSwapchainImagesKHR(app->device, swapchain->handle, &image_count, images.data());

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

		vkCreateImageView(app->device, &image_view_create_info, nullptr, &swapchain->images[image_index].view);
		swapchain->images[image_index].image = images[image_index];
	}
}

void deinit_swapchain(App* app)
{
	ZoneScopedN("Swapchain destruction");

	Swapchain* swapchain = &app->swapchain; // Shortcut

	for (const auto &image : app->swapchain.images)
	{
		vkDestroyImageView(app->device, image.view, nullptr);
	}

	vkDestroySwapchainKHR(app->device, swapchain->handle, nullptr);
	vkDestroySurfaceKHR(app->instance, swapchain->surface, nullptr);
}

bool recreate_swapchain(App* app)
{
	ZoneScopedN("Swapchain recreation");

	Swapchain* swapchain = &app->swapchain; // Shortcut

	// Requery surface capabilities
	VkSurfaceCapabilitiesKHR surface_capabilities;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(app->physical_device, swapchain->surface, &surface_capabilities);

	auto surface_extent = surface_capabilities.currentExtent;
	// Special value indicating that surface size will be determined by swapchain
	if (surface_extent.width == 0xFFFFFFFF && surface_extent.height == 0xFFFFFFFF) {
		auto size = app->platform->window_get_size();
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
	for (const auto &image : app->swapchain.images)
	{
		vkDestroyImageView(app->device, image.view, nullptr);
	}
	vkDestroySwapchainKHR(app->device, swapchain->handle, nullptr);

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
	vkCreateSwapchainKHR(app->device, &swapchain_create_info, nullptr, &swapchain->handle);

	uint32_t image_count;
	vkGetSwapchainImagesKHR(app->device, swapchain->handle, &image_count, nullptr);
	std::vector<VkImage> images(image_count);
	vkGetSwapchainImagesKHR(app->device, swapchain->handle, &image_count, images.data());

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

		vkCreateImageView(app->device, &image_view_create_info, nullptr, &swapchain->images[image_index].view);
		swapchain->images[image_index].image = images[image_index];
	}

	return true;
}

void recreate_swapchain_dependent_resources(App* app)
{
	ZoneScopedN("Recreation of swapchain-dependent resources");

	depth_buffer_destroy(app);
	depth_buffer_create(app);
}

void depth_buffer_create(App* app) {
	ZoneScopedN("Depth buffer creation");

	VkImageCreateInfo image_create_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.flags = 0,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = VK_FORMAT_D32_SFLOAT,
		.extent = { app->swapchain.extent.width, app->swapchain.extent.height, 1 },
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

	vmaCreateImage(app->vma_allocator, &image_create_info, &vma_allocation_info,
				   &app->depth_buffer.image, &app->depth_buffer.allocation,
				   nullptr);

	VkImageViewCreateInfo image_view_create_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = app->depth_buffer.image,
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

	vkCreateImageView(app->device, &image_view_create_info, nullptr, &app->depth_buffer.view);
}

void depth_buffer_destroy(App* app)
{
	ZoneScopedN("Depth buffer destruction");

	vkDestroyImageView(app->device, app->depth_buffer.view, nullptr);
	vmaDestroyImage(app->vma_allocator, app->depth_buffer.image, app->depth_buffer.allocation);
}

auto App::create_frame_data() -> void
{
	ZoneScopedN("Frame data creation");

	frame_data = std::vector<Frame_Data>(frames_in_flight);
	frame_data.reserve(3); // Triple buffering should be enough

	// Command pools
	{
		ZoneScopedN("Command pools creation");

		VkCommandPoolCreateInfo command_pool_create_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, // We'll reset every pool each frame and allocate new buffers
			.queueFamilyIndex = gfx_queue_family_index,
		};

		for (int frame_i=0; frame_i < frames_in_flight; frame_i++)
		{
			vkCreateCommandPool(device, &command_pool_create_info, nullptr,
								&frame_data[frame_i].frame_oddity_data[0].command_pool);
			vkCreateCommandPool(device, &command_pool_create_info, nullptr,
								&frame_data[frame_i].frame_oddity_data[1].command_pool);

			name_object(VK_OBJECT_TYPE_COMMAND_POOL,
						frame_data[frame_i].frame_oddity_data[0].command_pool,
						"Main command pool (frame {}, oddity 0)", frame_i);
			name_object(VK_OBJECT_TYPE_COMMAND_POOL,
						frame_data[frame_i].frame_oddity_data[1].command_pool,
						"Main command pool (frame {}, oddity 1)", frame_i);
		}
	}

	// Command buffers
	{
		ZoneScopedN("Command buffers allocation");

		for (int frame_i=0; frame_i < frames_in_flight; frame_i++)
		{
			for (int frame_oddity=0; frame_oddity < 2; frame_oddity++)
			{
				auto frame_oddity_data = &frame_data[frame_i].frame_oddity_data[frame_oddity];

				VkCommandBufferAllocateInfo allocate_info = {
					.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
					.commandPool = frame_oddity_data->command_pool,
					.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
					.commandBufferCount = 1,
				};

				vkAllocateCommandBuffers(device, &allocate_info, &frame_oddity_data->upload_command_buffer);
				vkAllocateCommandBuffers(device, &allocate_info, &frame_oddity_data->draw_command_buffer);

				name_object(VK_OBJECT_TYPE_COMMAND_BUFFER, frame_oddity_data->upload_command_buffer,
							"Upload command buffer (frame {}, oddity {})", frame_i, frame_oddity);
				name_object(VK_OBJECT_TYPE_COMMAND_BUFFER, frame_oddity_data->draw_command_buffer,
							"Draw command buffer (frame {}, oddity {})", frame_i, frame_oddity);
			}
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

		for (int frame_i=0; frame_i < frames_in_flight; frame_i++)
		{
			vkCreateSemaphore(device, &semaphore_create_info, nullptr, &frame_data[frame_i].present_semaphore);
			vkCreateSemaphore(device, &semaphore_create_info, nullptr, &frame_data[frame_i].render_semaphore);
			vkCreateFence(device, &fence_create_info, nullptr, &frame_data[frame_i].render_fence);
			vkCreateSemaphore(device, &semaphore_create_info, nullptr, &frame_data[frame_i].upload_semaphore);
			vkCreateFence(device, &fence_create_info, nullptr, &frame_data[frame_i].upload_fence);

			name_object(VK_OBJECT_TYPE_SEMAPHORE, frame_data[frame_i].present_semaphore,
						"Present semaphore (frame {})", frame_i);
			name_object(VK_OBJECT_TYPE_SEMAPHORE, frame_data[frame_i].render_semaphore,
						"Render semaphore (frame {})", frame_i);
			name_object(VK_OBJECT_TYPE_FENCE, frame_data[frame_i].render_fence,
						"Render fence (frame {})", frame_i);
			name_object(VK_OBJECT_TYPE_SEMAPHORE, frame_data[frame_i].upload_semaphore,
						"Upload semaphore (frame {})", frame_i);
			name_object(VK_OBJECT_TYPE_FENCE, frame_data[frame_i].upload_fence,
						"Upload fence (frame {})", frame_i);

		}
	}

	// Staging buffer
	{
		ZoneScopedN("Staging buffer allocation");

		VkBufferCreateInfo staging_buffer_create_info = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = 30000000, // 30 mb
			.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		};

		VmaAllocationCreateInfo staging_buffer_vma_create_info = {
			.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
			.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
		};

		for (int frame_i=0; frame_i < frames_in_flight; frame_i++)
		{
			VmaAllocationInfo allocation_info;
			vmaCreateBuffer(vma_allocator,
							&staging_buffer_create_info,
							&staging_buffer_vma_create_info,
							&frame_data[frame_i].staging_buffer.buffer,
							&frame_data[frame_i].staging_buffer.allocation,
							&allocation_info);
			frame_data[frame_i].staging_buffer_ptr = allocation_info.pMappedData;
			name_object(VK_OBJECT_TYPE_BUFFER, frame_data[frame_i].staging_buffer.buffer,
						"Staging buffer (frame {})", frame_i);
		}
	}
}

auto App::destroy_frame_data() -> void
{
	// Staging buffer
	for (int frame_i=0; frame_i < frames_in_flight; frame_i++)
	{
		vmaDestroyBuffer(vma_allocator, frame_data[frame_i].staging_buffer.buffer,
						 frame_data[frame_i].staging_buffer.allocation);
	}

	// Synchronization primitives
	for (int frame_i=0; frame_i < frames_in_flight; frame_i++)
	{
		vkDestroySemaphore(device, frame_data[frame_i].present_semaphore, nullptr);
		vkDestroySemaphore(device, frame_data[frame_i].render_semaphore, nullptr);
		vkDestroyFence(device, frame_data[frame_i].render_fence, nullptr);
		vkDestroySemaphore(device, frame_data[frame_i].upload_semaphore, nullptr);
		vkDestroyFence(device, frame_data[frame_i].upload_fence, nullptr);
	}

	// Command buffers
	for (int frame_i=0; frame_i < frames_in_flight; frame_i++)
	{
		for (auto &frame_oddity_data : frame_data[frame_i].frame_oddity_data)
		{
			VkCommandBuffer buffers[] = {
				frame_oddity_data.upload_command_buffer, frame_oddity_data.draw_command_buffer};
			vkFreeCommandBuffers(device, frame_oddity_data.command_pool, 2, buffers);
		}
	}

	// Command pools
	for (int frame_i=0; frame_i < frames_in_flight; frame_i++)
	{
		vkDestroyCommandPool(device, frame_data[frame_i].frame_oddity_data[0].command_pool, nullptr);
		vkDestroyCommandPool(device, frame_data[frame_i].frame_oddity_data[1].command_pool, nullptr);
	}
}

auto App::create_buffers() -> void {

	// Vertex buffer
	{
		VkBufferCreateInfo vertex_buffer_create_info = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = 100000,
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

		name_object(VK_OBJECT_TYPE_BUFFER, vertex_buffer.buffer, "Main vertex buffer");
	}

	// Uniform buffer
	{
		auto size = clamp_size_to_alignment(
			sizeof(Frame_Uniform_Data),
			device_properties.properties.limits.minUniformBufferOffsetAlignment);
		size *= frames_in_flight * 2; // frames in flight times frame oddity

		VkBufferCreateInfo buffer_create_info = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = size,
			.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		};

		VmaAllocationCreateInfo vma_buffer_create_info = {
			.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
		};

		vmaCreateBuffer(vma_allocator,
						&buffer_create_info,
						&vma_buffer_create_info,
						&per_frame_data_buffer.buffer,
						&per_frame_data_buffer.allocation,
						nullptr);

		name_object(VK_OBJECT_TYPE_BUFFER, per_frame_data_buffer.buffer, "Frame data uniform buffer");
	}
}

auto App::load_scene_data() -> void {
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn, err;
	auto filename = "assets/colored_suzanne/suzanne.obj";

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
				tinyobj::real_t u  = attrib.texcoords[2 * idx.texcoord_index + 0];
				tinyobj::real_t v  = attrib.texcoords[2 * idx.texcoord_index + 1];

				vertex_data.insert(vertex_data.end(), {vx, vy, vz, nx, ny, nz, u, 1-v}); // Vulkan UV fix
				vertices_count += 1;
			}

			index_offset += 3;
		}
	}

	spdlog::info("Loaded object, with {} vertices ({} bytes)", vertices_count, vertex_data.size() * sizeof(float));
	memcpy(vertex_buffer_ptr, vertex_data.data(), vertex_data.size() * sizeof(float));
	vmaFlushAllocation(vma_allocator, vertex_buffer.allocation, 0, vertex_data.size() * sizeof(float));

	// Texture

	auto texture = load_file("assets/colored_suzanne/ColorTexture.png");
	int32_t width, height, channels;
	auto pixels = stbi_load_from_memory(texture.data(), texture.size(), &width, &height, &channels, STBI_rgb_alpha);

	VkImageCreateInfo image_create_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = VK_FORMAT_R8G8B8A8_SRGB,
		.extent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	VmaAllocationCreateInfo allocation_create_info = {
		.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
	};

	AllocatedImage image = {};
	VmaAllocationInfo allocation_info;
	vmaCreateImage(vma_allocator, &image_create_info, &allocation_create_info,
				   &image.image, &image.allocation, &allocation_info);
	name_object(VK_OBJECT_TYPE_IMAGE, image.image, "Color texture");

	VkImageViewCreateInfo image_view_create_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = image.image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = VK_FORMAT_R8G8B8A8_SRGB,
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
	VkImageView image_view;
	vkCreateImageView(device, &image_view_create_info, nullptr, &image_view);

	VkSamplerCreateInfo sampler_create_info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.mipLodBias = 0,
		.anisotropyEnable = false,
		.maxAnisotropy = 16,
		.compareEnable = false,
		.compareOp = VK_COMPARE_OP_NEVER,
		.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE,
		.unnormalizedCoordinates = false,
	};
	VkSampler sampler;
	vkCreateSampler(device, &sampler_create_info, nullptr, &sampler);

	image_manager.sampled_images.push_back({
		.image = image,
		.view = image_view,
		.sampler = sampler,
	});
	image_manager.upload_queue.push_back({
		.pixels = pixels,
		.image_size = { static_cast<uint32_t>(width), static_cast<uint32_t>(height) },
		.image_handle = static_cast<Image_Manager::Image_Handle>(image_manager.sampled_images.size() - 1),
	});
}

auto App::create_descriptors() -> void {

	{
		VkDescriptorSetLayoutBinding set_layout_bindings[] = {
			{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
			},
			{
				.binding = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 10,
				.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
			},
		};

		VkDescriptorBindingFlags flags[] = {0, VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT};

		VkDescriptorSetLayoutBindingFlagsCreateInfo layout_flags_create_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
			.bindingCount = 2,
			.pBindingFlags = flags,
		};

		VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.pNext = &layout_flags_create_info,
			.flags = 0,
			.bindingCount = 2,
			.pBindings = set_layout_bindings,
		};

		vkCreateDescriptorSetLayout(device, &descriptor_set_layout_create_info, nullptr, &per_frame_descriptor_set_layout);
	}

	{
		VkDescriptorPoolSize pool_sizes[] = {
			{
				.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				.descriptorCount = 10,
			},
			{
				.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 100,
			},
		};

		VkDescriptorPoolCreateInfo descriptor_pool_create_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.flags = 0,
			.maxSets = 10,
			.poolSizeCount = 2,
			.pPoolSizes = pool_sizes,
		};

		vkCreateDescriptorPool(device, &descriptor_pool_create_info, nullptr, &descriptor_pool);
	}

	{
		uint32_t counts[] = { 1 };

		VkDescriptorSetVariableDescriptorCountAllocateInfo descriptor_variable_size_allocate_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
			.descriptorSetCount = 1,
			.pDescriptorCounts = counts,
		};

		VkDescriptorSetAllocateInfo descriptor_set_allocate_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.pNext = &descriptor_variable_size_allocate_info,
			.descriptorPool = descriptor_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &per_frame_descriptor_set_layout,
		};

		vkAllocateDescriptorSets(device, &descriptor_set_allocate_info, &per_frame_descriptor_set);

		VkDescriptorBufferInfo descriptor_buffer_info = {
			.buffer = per_frame_data_buffer.buffer,
			.offset = 0,
			.range = sizeof(Frame_Uniform_Data),
		};

		VkDescriptorImageInfo image_info = {
			.sampler = image_manager.sampled_images[0].sampler,
			.imageView = image_manager.sampled_images[0].view,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};

		VkWriteDescriptorSet descriptor_set_writes[] = {
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = per_frame_descriptor_set,
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				.pBufferInfo = &descriptor_buffer_info,
			},
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = per_frame_descriptor_set,
				.dstBinding = 1,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &image_info,
			}
		};

		vkUpdateDescriptorSets(device, 2, descriptor_set_writes, 0, nullptr);
	}
}

auto load_file(const char* file_path) -> std::vector<uint8_t>
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

auto App::create_shaders() -> void
{
	// If Spir-V shader is valid, casting bytes to 32-bit words shouldn't matter
	auto vert_shader_code = load_file("data/shaders/triangle_vert.spv");
	VkShaderModuleCreateInfo vert_shader_create_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = vert_shader_code.size(),
		.pCode = reinterpret_cast<const uint32_t *>(vert_shader_code.data()),
	};
	vkCreateShaderModule(device, &vert_shader_create_info, nullptr, &vertex_shader);

	auto frag_shader_code = load_file("data/shaders/triangle_frag.spv");
	VkShaderModuleCreateInfo frag_shader_create_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = frag_shader_code.size(),
		.pCode = reinterpret_cast<const uint32_t *>(frag_shader_code.data()),
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
		.stride = 8 * sizeof(float),
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
	};

	VkVertexInputAttributeDescription vertex_attributes[3];

	vertex_attributes[0] = { // Position attribute
		.location = 0,
		.binding = 0,
		.format = VK_FORMAT_R32G32B32_SFLOAT,
		.offset = 0,
	};

	vertex_attributes[1] = { // Normal attribute
		.location = 1,
		.binding = 0,
		.format = VK_FORMAT_R32G32B32_SFLOAT,
		.offset = 3 * sizeof(float),
	};

	vertex_attributes[2] = { // UV attribute
		.location = 2,
		.binding = 0,
		.format = VK_FORMAT_R32G32_SFLOAT,
		.offset = 6 * sizeof(float),
	};

	VkPipelineVertexInputStateCreateInfo vertex_input_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &binding_description,
		.vertexAttributeDescriptionCount = 3,
		.pVertexAttributeDescriptions = vertex_attributes,
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
		.pColorAttachmentFormats = &swapchain.selected_format.format,
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
		.MinImageCount = swapchain.images_count,
		.ImageCount = swapchain.images_count,
		.MSAASamples = VK_SAMPLE_COUNT_1_BIT,
		.UseDynamicRendering = true,
		.ColorAttachmentFormat = swapchain.selected_format.format,
	};
	//ImGui_ImplVulkan_LoadFunctions();
	ImGui_ImplVulkan_Init(&init_info, VK_NULL_HANDLE);

	{
		VkCommandPoolCreateInfo command_pool_create_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
			.queueFamilyIndex = gfx_queue_family_index,
		};
		VkCommandPool pool;
		vkCreateCommandPool(device, &command_pool_create_info, nullptr, &pool);

		VkCommandBuffer command_buffer;

		VkCommandBufferAllocateInfo allocate_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = pool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1,
		};
		vkAllocateCommandBuffers(device, &allocate_info, &command_buffer);

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

auto App::draw() -> void {
	ZoneScopedN("Draw");

	auto current_frame = &frame_data[frame_id];
	auto frame_oddity = static_cast<uint32_t>(frame_number / frames_in_flight) % 2;
	auto frame_oddity_data = &current_frame->frame_oddity_data[frame_oddity];

	// Reset pool and allocate command buffers
	VkCommandBuffer upload_command_buffer, draw_command_buffer;
	{
		ZoneScopedN("Reset command pool");
		vkResetCommandPool(device, frame_oddity_data->command_pool, 0);
		upload_command_buffer = frame_oddity_data->upload_command_buffer;
		draw_command_buffer = frame_oddity_data->draw_command_buffer;
	}

	// Await for previously buffered frame
	{
		ZoneScopedN("Waiting on upload");

		vkWaitForFences(device, 1, &current_frame->upload_fence, true, UINT64_MAX);
		vkResetFences(device, 1, &current_frame->upload_fence);
	}

	queue_insert_marker(gfx_queue, "Note: Frame: {} (id: {}, oddity: {})",
						frame_number, frame_id, frame_oddity);

	char *staging_buffer_ptr = static_cast<char *>(current_frame->staging_buffer_ptr);
	size_t linear_allocator = 0;

	// This is the offset inside per frame data buffer that will be used during this frame
	size_t current_per_frame_data_buffer_offset = clamp_size_to_alignment(
		sizeof(Frame_Uniform_Data),
		device_properties.properties.limits.minUniformBufferOffsetAlignment)
												  * frame_id * frame_oddity;

	// Begin recording
	VkCommandBufferBeginInfo upload_begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	vkBeginCommandBuffer(upload_command_buffer, &upload_begin_info);
	command_buffer_region_begin(upload_command_buffer, "Upload stage");

	{
		ZoneScopedN("Upload image data");

		for (auto upload_data: image_manager.upload_queue) {
			auto image_size = upload_data.image_size.width * upload_data.image_size.height * 4;

			memcpy(staging_buffer_ptr + linear_allocator, upload_data.pixels, image_size);
			stbi_image_free(upload_data.pixels); // Pixels are no longer required on RAM

			// Transition to copy layout
			VkImageMemoryBarrier to_transfer_transition_barrier = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = 0,
				.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				.image = image_manager.sampled_images[upload_data.image_handle].image.image,
				.subresourceRange = {
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
			};
			vkCmdPipelineBarrier(upload_command_buffer, VK_PIPELINE_STAGE_HOST_BIT,
								 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
								 &to_transfer_transition_barrier);

			// Enqueue copy
			VkBufferImageCopy region = {
				.imageSubresource = {
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.mipLevel = 0,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
				.imageExtent = {
					.width = upload_data.image_size.width,
					.height = upload_data.image_size.height,
					.depth = 1,
				},
			};
			vkCmdCopyBufferToImage(upload_command_buffer, current_frame->staging_buffer.buffer,
								   image_manager.sampled_images[upload_data.image_handle].image.image,
								   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
			linear_allocator += image_size;

			// Immediately transition into proper layout
			VkImageMemoryBarrier from_transform_transition_barrier = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.image = image_manager.sampled_images[upload_data.image_handle].image.image,
				.subresourceRange = {
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
			};
			vkCmdPipelineBarrier(upload_command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
								 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
								 &from_transform_transition_barrier);
		}
		image_manager.upload_queue.clear(); // Clear queue after we schedule all uploads
	}

	// Build and stage per-frame data
	{
		ZoneScopedN("Build per frame uniform data");

		glm::mat4 view = camera.get_view_matrix();
		glm::mat4 projection = glm::perspective(
			glm::radians(70.f),
			1280.f / 720.f,
			0.1f,
			200.0f);
		// projection[1][1] *= -1;
		glm::mat4 model = glm::rotate(
			glm::mat4{1.0f},
			rotation,
			glm::vec3(0, 1, 0));
		glm::mat4 render_matrix = projection * view * model;

		auto uniform_data = reinterpret_cast<Frame_Uniform_Data *>(staging_buffer_ptr + linear_allocator);

		uniform_data->render_matrix = render_matrix;

		// Upload
		VkBufferCopy region = {
			.srcOffset = linear_allocator,
			.dstOffset = current_per_frame_data_buffer_offset,
			.size = sizeof(Frame_Uniform_Data),
		};
		vkCmdCopyBuffer(upload_command_buffer, current_frame->staging_buffer.buffer, per_frame_data_buffer.buffer,
						1, &region);

		linear_allocator += sizeof(Frame_Uniform_Data);
	}

	command_buffer_region_end(upload_command_buffer);
	vkEndCommandBuffer(upload_command_buffer);

	{
		ZoneScopedN("Submit staging buffer");

		VkPipelineStageFlags dst_stage_mask[1] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
		VkSubmitInfo submit_info = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			//.waitSemaphoreCount = 1,
			//.pWaitSemaphores = &present_semaphore[index],
			//.pWaitDstStageMask = dst_stage_mask,
			.commandBufferCount = 1,
			.pCommandBuffers = &upload_command_buffer,
			.signalSemaphoreCount = 1,
			.pSignalSemaphores = &current_frame->upload_semaphore,
		};
		vkQueueSubmit(gfx_queue, 1, &submit_info, current_frame->upload_fence);
	}

	// Await for previously buffered frame
	{
		ZoneScopedN("Waiting on frame");

		vkWaitForFences(device, 1, &current_frame->render_fence, true, UINT64_MAX);
		vkResetFences(device, 1, &current_frame->render_fence);
	}

	// Acquire swapchain image_handle and recreate swapchain if necessary
	Combined_View_Image swapchain_image;
	uint32_t swapchain_image_index;
	{
		ZoneScopedN("Swapchain image_handle acquiring");

		auto acquire_result = vkAcquireNextImageKHR(device, swapchain.handle, UINT64_MAX,
													current_frame->present_semaphore, VK_NULL_HANDLE,
													&swapchain_image_index);
		swapchain_image = swapchain.images[swapchain_image_index];

		if (acquire_result == VK_SUBOPTIMAL_KHR || acquire_result == VK_ERROR_OUT_OF_DATE_KHR) {
			ZoneScopedN("Swapchain recreation request");

			// In theory, if we get VK_ERROR_OUT_OF_DATE_KHR, but we'll have minimized window, the extent will be
			// (0, 0) and swapchain will not be recreated, this no resources will be created. This is may crash,
			// since well, we need a swapchain (although we could skip rendering at all, but meh, idk).
			// But, this is yet to be observed behaviour, hope it doesn't happen. Maybe, there is a valid usage
			// guarantee for this???

			{
				ZoneScopedN("Idling on previous frames");

				// We could wait on fences, but I'm lazy
				vkDeviceWaitIdle(device);
			}

			if (recreate_swapchain(this))
			{
				recreate_swapchain_dependent_resources(this);

				// FIXME: Following unfortunately will result in validation error, since present semaphore isn't
				// being "reset" on swapchain destroy.
				vkAcquireNextImageKHR(device, swapchain.handle, UINT64_MAX, current_frame->present_semaphore,
									  VK_NULL_HANDLE, &swapchain_image_index);
				swapchain_image = swapchain.images[swapchain_image_index];
			}
		}
	}

	// Begin rendering command buffer
	VkCommandBufferBeginInfo draw_begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	vkBeginCommandBuffer(draw_command_buffer, &draw_begin_info);

	{
		ZoneScopedN("Transition to color attachment layout");

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
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, // Legal, since we're clearing this image_handle on render load anyway
			.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = swapchain_image.image,
			.subresourceRange = range,
		};

		vkCmdPipelineBarrier(
			draw_command_buffer,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &render_transition_barrier);
	}

	command_buffer_region_begin(draw_command_buffer, "Main draw pass");
	{
		ZoneScopedN("Main draw pass");

		VkClearValue color_clear_value = {.color = {.float32 = {0, 0, 0.2, 1}}};

		VkRenderingAttachmentInfo swapchain_attachment_info = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = swapchain_image.view,
			.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.resolveMode = VK_RESOLVE_MODE_NONE,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.clearValue = color_clear_value,
		};

		VkClearValue depth_clear_value = {.depthStencil = {.depth = 1}};

		VkRenderingAttachmentInfo depth_attachment_info = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = depth_buffer.view,
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
				.extent = swapchain.extent,
			},
			.layerCount = 1,
			.viewMask = 0,
			.colorAttachmentCount = 1,
			.pColorAttachments = &swapchain_attachment_info,
			.pDepthAttachment = &depth_attachment_info,
		};

		VkViewport viewport = {
			.x = 0,
			.y = (float) swapchain.extent.height,
			.width = (float) swapchain.extent.width,
			.height = -1 * (float) swapchain.extent.height,
			.minDepth = 0.0f,
			.maxDepth = 1.0f,
		};
		VkRect2D scissor = {.offset = {}, .extent = swapchain.extent,};

		vkCmdSetViewport(draw_command_buffer, 0, 1, &viewport);
		vkCmdSetScissor(draw_command_buffer, 0, 1, &scissor);

		{
			ZoneScopedN("Bind descriptor");

			uint32_t offset = current_per_frame_data_buffer_offset;
			vkCmdBindDescriptorSets(draw_command_buffer,
									VK_PIPELINE_BIND_POINT_GRAPHICS,
									pipeline_layout,
									0,
									1, &per_frame_descriptor_set,
									1, &offset);
		}

		{
			ZoneScopedN("Pipeline bind");
			vkCmdBindPipeline(draw_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		}

		vkCmdBeginRendering(draw_command_buffer, &rendering_info);
		command_buffer_region_begin(draw_command_buffer, "Rendering");
		{
			ZoneScopedN("Drawing");

			VkDeviceSize offsets = 0;
			vkCmdBindVertexBuffers(draw_command_buffer, 0, 1, &vertex_buffer.buffer, &offsets);
			vkCmdDraw(draw_command_buffer, vertices_count, 1, 0, 0);
		}
		command_buffer_region_end(draw_command_buffer);
		vkCmdEndRendering(draw_command_buffer);
	}
	command_buffer_region_end(draw_command_buffer);

	command_buffer_region_begin(draw_command_buffer, "ImGui draw pass");
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
				.extent = swapchain.extent,
			},
			.layerCount = 1,
			.viewMask = 0,
			.colorAttachmentCount = 1,
			.pColorAttachments = &imgui_pass_swapchain_attachment_info,
		};
		vkCmdBeginRendering(draw_command_buffer, &imgui_pass_rendering_info);
		{
			ImGui::Render();
			ImDrawData *draw_data = ImGui::GetDrawData();
			ImGui_ImplVulkan_RenderDrawData(draw_data, draw_command_buffer);
		}
		vkCmdEndRendering(draw_command_buffer);
	}
	command_buffer_region_end(draw_command_buffer);

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
			draw_command_buffer,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &present_transition_barrier);
	}

	vkEndCommandBuffer(draw_command_buffer);

	{
		ZoneScopedN("Submit draw");

		VkPipelineStageFlags dst_stage_mask[] = {VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
												 VK_PIPELINE_STAGE_ALL_COMMANDS_BIT}; // Transfer works too, not sure why
		VkSemaphore wait_semaphores[] = {current_frame->present_semaphore, current_frame->upload_semaphore};
		VkSubmitInfo submit_info = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount = 2,
			.pWaitSemaphores = wait_semaphores,
			.pWaitDstStageMask = dst_stage_mask,
			.commandBufferCount = 1,
			.pCommandBuffers = &draw_command_buffer,
			.signalSemaphoreCount = 1,
			.pSignalSemaphores = &current_frame->render_semaphore,
		};
		vkQueueSubmit(gfx_queue, 1, &submit_info, current_frame->render_fence);
	}

	{
		ZoneScopedN("Submit present");

		VkPresentInfoKHR present_info = {
			.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &current_frame->render_semaphore,
			.swapchainCount = 1,
			.pSwapchains = &swapchain.handle,
			.pImageIndices = &swapchain_image_index,
		};
		vkQueuePresentKHR(gfx_queue, &present_info);
	}
}