#pragma once

#include<vector>
#include<span>

#include <vulkan/vulkan.h>
#include<volk.h>
#include<vk_mem_alloc.h>

//#define GLM_FORCE_LEFT_HANDED
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include<glm/matrix.hpp>
#include <format>

// Note that VkPhysicalDeviceVulkan1xProperties properties may have invalid pNext.
struct Device_Properties
{
	VkPhysicalDeviceProperties properties;
	VkPhysicalDeviceVulkan11Properties properties11;
	VkPhysicalDeviceVulkan12Properties properties12;
	VkPhysicalDeviceVulkan13Properties properties13;
};

// List of optional features that our renderer support if running on capable device
struct Renderer_Capabilities
{
};


class Input
{
public:
	enum Button
	{
		MOUSE_BUTTON_LEFT,
		MOUSE_BUTTON_RIGHT,
		MOUSE_BUTTON_MIDDLE,
		MOUSE_BUTTON_THUMB_UP,
		MOUSE_BUTTON_THUMB_DOWN,

		KEYBOARD_BUTTON_UP,
		KEYBOARD_BUTTON_DOWN,
		KEYBOARD_BUTTON_LEFT,
		KEYBOARD_BUTTON_RIGHT,
		KEYBOARD_BUTTON_SPACE,
		KEYBOARD_BUTTON_RIGHT_SHIFT,
		KEYBOARD_BUTTON_LEFT_SHIFT,
		KEYBOARD_BUTTON_F1,
		KEYBOARD_BUTTON_F2,
		KEYBOARD_BUTTON_F3,
		KEYBOARD_BUTTON_F4,
		KEYBOARD_BUTTON_F5,
		KEYBOARD_BUTTON_F6,
		KEYBOARD_BUTTON_F7,
		KEYBOARD_BUTTON_F8,
		KEYBOARD_BUTTON_F9,
		KEYBOARD_BUTTON_F10,
		KEYBOARD_BUTTON_F11,
		KEYBOARD_BUTTON_F12,
		KEYBOARD_BUTTON_ENTER,
		KEYBOARD_BUTTON_ESCAPE,
		KEYBOARD_BUTTON_HOME,
		KEYBOARD_BUTTON_RIGHT_CONTROL,
		KEYBOARD_BUTTON_LEFT_CONTROL,
		KEYBOARD_BUTTON_DELETE,
		KEYBOARD_BUTTON_BACKSPACE,
		KEYBOARD_BUTTON_PAGE_DOWN,
		KEYBOARD_BUTTON_PAGE_UP,

		KEYBOARD_BUTTON_A,
		KEYBOARD_BUTTON_B,
		KEYBOARD_BUTTON_C,
		KEYBOARD_BUTTON_D,
		KEYBOARD_BUTTON_E,
		KEYBOARD_BUTTON_F,
		KEYBOARD_BUTTON_G,
		KEYBOARD_BUTTON_H,
		KEYBOARD_BUTTON_I,
		KEYBOARD_BUTTON_J,
		KEYBOARD_BUTTON_K,
		KEYBOARD_BUTTON_L,
		KEYBOARD_BUTTON_M,
		KEYBOARD_BUTTON_N,
		KEYBOARD_BUTTON_O,
		KEYBOARD_BUTTON_P,
		KEYBOARD_BUTTON_Q,
		KEYBOARD_BUTTON_R,
		KEYBOARD_BUTTON_S,
		KEYBOARD_BUTTON_T,
		KEYBOARD_BUTTON_U,
		KEYBOARD_BUTTON_V,
		KEYBOARD_BUTTON_W,
		KEYBOARD_BUTTON_X,
		KEYBOARD_BUTTON_Y,
		KEYBOARD_BUTTON_Z,
	};

	enum Button_State : bool
	{
		PRESSED = true,
		RELEASED = false,
	};

	Button_State buttons_states[256]; // Contain last state of button
	double_t mouse_x,       mouse_y;
	double_t mouse_x_delta, mouse_y_delta;

	// Set this to control cursor. This might also enable raw input if supported
	bool inhibit_cursor = false;
};

struct Size
{
	uint32_t width;
	uint32_t height;
};

struct Window_Params
{
	std::string name;
	Size size;
};

class Platform
{
public:

	virtual void poll_events() = 0;

	// Windowing functions
	// window_init and window_destroy will be called _exactly_ once (only one window is supported at the moment)
	virtual void window_init(Window_Params params) = 0;
	virtual void window_destroy() = 0;
	virtual void window_set_name(std::string name) = 0;
	virtual void window_set_size(uint32_t width, uint32_t height) = 0;
	virtual Size window_get_size() = 0;
	virtual bool window_requested_to_close() = 0;

	virtual bool check_presentation_support(VkInstance instance,
											VkPhysicalDevice physical_device,
											uint32_t family_queue) = 0;
	virtual void create_surface(VkInstance instance, VkSurfaceKHR* surface) = 0;
	virtual std::span<const char *> get_required_extensions() = 0; // Return required vulkan extensions

	// ImGui related functions.
	// Temporary, will be replaced by custom, universal implementation on top of abstracted input, windowing, etc.
	virtual void imgui_init() = 0;
	virtual void imgui_shutdown() = 0;
	virtual void imgui_new_frame() = 0;

	// Input related functions
	virtual void fill_input(Input* input) = 0;
};

class Camera
{
public:
	glm::vec3 position;
	glm::vec3 front, up, right;
	float yaw, pitch;
	float velocity;
	float speed_multiplier;

	float mouse_sensitivity;

	auto init() -> void;
	auto update(Input* input, float delta_time) -> void; // Depends on event polling
	[[nodiscard]] auto get_view_matrix() const -> glm::mat4x4;
};

struct Timings
{
	std::chrono::high_resolution_clock::time_point frame_time_stamp;
	float delta_time;

	// Called on each frame
	auto update_timings() -> void;
};

struct AllocatedBuffer {
	VkBuffer buffer;
	VmaAllocation allocation;
};

struct AllocatedImage {
	VkImage image;
	VmaAllocation allocation;
};

// Returns size of block that includes alignment
size_t clamp_size_to_alignment(size_t block_size, size_t alignment);

// Objects "owned by frame" for double or triple buffering
struct Frame_Data
{
	struct Frame_Oddity_Data
	{
		VkCommandPool command_pool;
		VkCommandBuffer upload_command_buffer;
		VkCommandBuffer draw_command_buffer;
	};
	Frame_Oddity_Data frame_oddity_data[2];

	// Semaphore signaling swapchain presentation event, or more specifically, swapchain image acquire event
	VkSemaphore present_semaphore;
	VkSemaphore render_semaphore; // Same as fence below. Presentation event waits on it
	VkFence render_fence; // Will be signaled once all rendering operations for this frame are done
	VkSemaphore upload_semaphore; // Signal finishing of upload operation
	VkFence upload_fence; // Same as above

	AllocatedBuffer staging_buffer; // Staging buffer that will update other buffers with changed data
	void* staging_buffer_ptr;
};

struct Frame_Uniform_Data
{
	glm::mat4x4 render_matrix;
};

class App
{
public:
	explicit App(Platform* platform) { // NOLINT(cppcoreguidelines-pro-type-member-init)
		this->platform = platform;
	}

	auto entry() -> void;

private:
	Platform* platform;
	Input input{}; // Especially important to zero out mouse_x and mouse_y at init

	Timings timings;

	float rotation = 0;

	// Subsystems
	Camera camera;

	VkInstance instance{};
	uint32_t instance_version{};
	VkDebugUtilsMessengerEXT debug_util_messenger{};

	VkPhysicalDevice physical_device{};
	Device_Properties device_properties{};
	VkDevice device{};
	VkQueue gfx_queue{};
	uint32_t gfx_queue_family_index{};

	VmaAllocator vma_allocator{};

	VkSurfaceKHR surface{};

	VkSwapchainKHR swapchain{};
	VkFormat swapchain_image_format;
	uint32_t swapchain_images_count{};
	std::vector<VkImage> swapchain_images;
	std::vector<VkImageView> swapchain_image_views;

	uint32_t frames_in_flight;
	uint32_t frame_id; // Represent frame id in multiple buffering sense - 0, 1 or 2 for triple buffering.
	std::vector<Frame_Data> frame_data;

	AllocatedBuffer vertex_buffer{};
	void* vertex_buffer_ptr{};

	AllocatedBuffer per_frame_data_buffer{};

	AllocatedImage depth_buffer{};
	VkImageView depth_buffer_view{};

	VkDescriptorSetLayout per_frame_descriptor_set_layout{};
	VkDescriptorPool descriptor_pool{};
	VkDescriptorSet per_frame_descriptor_set{};
	
	VkShaderModule vertex_shader{};
	VkShaderModule fragment_shader{};

	VkExtent2D window_extent{};

	VkPipelineLayout pipeline_layout{};
	VkPipeline pipeline{};

	VkDescriptorPool imgui_descriptor_pool{};

	bool is_running = false;
	uint64_t frame_number{}; // Number of frame from start

	uint32_t vertices_count{};

	// Convenience function for naming Vulkan objects with auto formatting via std::format
	// Usage valid only after device is created.
	// Only use with Vulkan objects!
	template <typename Handle, class... Args>
	auto name_object(VkObjectType object_type, Handle handle, std::format_string<Args...> fmt, Args&&... args) -> void;

	auto create_instance() -> void;
	auto create_device() -> void;
    auto create_allocator() -> void;
	auto create_surface() -> void;
	auto create_swapchain(bool recreate = false) -> void;

	auto create_frame_data() -> void;

	auto create_buffers() -> void;
	auto upload_vertex_data() -> void;
	auto create_depth_buffer() -> void;
	auto create_descriptors() -> void;
	auto create_shaders() -> void;
	auto create_pipeline() -> void;

	auto init_imgui() -> void;

	auto draw() -> void;
};