#include <stdexcept>

#include "app.h"

#include <GLFW/glfw3.h>
#include <imgui/backends/imgui_impl_glfw.h>

class GLFW_Platform : public Platform
{
public:
	GLFWwindow* glfw_window;

	GLFW_Platform()
		: glfw_window(nullptr)
	{
	}

	void poll_events() override;

	void window_init(Window_Params params) override;
	void window_destroy() override;
	void window_set_name(std::string name) override;
	void window_set_size(uint32_t width, uint32_t height) override;
	Size window_get_size() override;
	bool window_requested_to_close() override;

	bool check_presentation_support(VkInstance instance,
									VkPhysicalDevice physical_device,
									uint32_t family_queue) override;
	void create_surface(VkInstance instance, VkSurfaceKHR* surface) override;
	std::span<const char *> get_required_extensions() override;

	void imgui_init() override;
	void imgui_new_frame() override;
	void imgui_shutdown() override;
};

void GLFW_Platform::poll_events()
{
	glfwPollEvents();
}

// --------------------------------------------------------------------------------------------------------------------

void GLFW_Platform::window_init(Window_Params params)
{
	assert(glfw_window == nullptr);

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // Disable OpenGL things
	glfw_window = glfwCreateWindow(
		static_cast<int32_t>(params.size.width),
		static_cast<int32_t>(params.size.height),
		params.name.c_str(),
		nullptr,
		nullptr);
	glfwSetWindowUserPointer(glfw_window, this);
}

void GLFW_Platform::window_destroy()
{
	assert(glfw_window != nullptr);

	glfwDestroyWindow(glfw_window);
	glfw_window = nullptr;
}

void GLFW_Platform::window_set_name(std::string name)
{
	glfwSetWindowTitle(glfw_window, name.c_str());
}

void GLFW_Platform::window_set_size(uint32_t width, uint32_t height)
{
	glfwSetWindowSize(glfw_window, static_cast<int32_t>(width), static_cast<int32_t>(height));
}

Size GLFW_Platform::window_get_size()
{
	int32_t width, height;
	glfwGetFramebufferSize(glfw_window, &width, &height);
	return Size{ static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
}

bool GLFW_Platform::window_requested_to_close() {
	return glfwWindowShouldClose(glfw_window);
}

// --------------------------------------------------------------------------------------------------------------------

std::span<const char *> GLFW_Platform::get_required_extensions()
{
	uint32_t glfw_extensions_count;
	auto glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extensions_count);
	return std::span{glfw_extensions, glfw_extensions_count};
}

bool GLFW_Platform::check_presentation_support(VkInstance instance, VkPhysicalDevice physical_device,
											   uint32_t family_queue)
{
	return glfwGetPhysicalDevicePresentationSupport(instance, physical_device, family_queue);
}

void GLFW_Platform::create_surface(VkInstance instance, VkSurfaceKHR *surface)
{
	glfwCreateWindowSurface(instance, glfw_window, nullptr, surface);
}

// --------------------------------------------------------------------------------------------------------------------

void GLFW_Platform::imgui_init()
{
	ImGui_ImplGlfw_InitForVulkan(glfw_window, true);
}

void GLFW_Platform::imgui_new_frame()
{
	ImGui_ImplGlfw_NewFrame();
}

void GLFW_Platform::imgui_shutdown()
{
	ImGui_ImplGlfw_Shutdown();
}

// --------------------------------------------------------------------------------------------------------------------

auto main() -> int32_t
{
	if(!glfwInit())
	{
		throw std::runtime_error("GLFW Could not be initialized!");
	}

	auto platform = new GLFW_Platform();

	App *app;
	app = new App(platform);
	app->entry();

	glfwTerminate();
	return 0;
}