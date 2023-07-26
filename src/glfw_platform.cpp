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

	double_t previous_mouse_x = 0, previous_mouse_y = 0;
	bool was_inhibited = false;

	void fill_input(Input* input) override;
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
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	//glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
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

#define TRANSLATE_KEY(INPUT_KEY, GLFW_KEY) input->buttons_states[Input::Button::INPUT_KEY] = \
	glfwGetKey(glfw_window, GLFW_KEY) == GLFW_PRESS ? Input::Button_State::PRESSED : Input::Button_State::RELEASED;

#define TRANSLATE_MOUSE_BUTTON(INPUT_KEY, GLFW_KEY) input->buttons_states[Input::Button::INPUT_KEY] = \
	glfwGetMouseButton(glfw_window, GLFW_KEY) == GLFW_PRESS                                           \
	? Input::Button_State::PRESSED : Input::Button_State::RELEASED;

void GLFW_Platform::fill_input(Input *input) {
	TRANSLATE_MOUSE_BUTTON(MOUSE_BUTTON_LEFT,       GLFW_MOUSE_BUTTON_1);
	TRANSLATE_MOUSE_BUTTON(MOUSE_BUTTON_RIGHT,      GLFW_MOUSE_BUTTON_2);
	TRANSLATE_MOUSE_BUTTON(MOUSE_BUTTON_MIDDLE,     GLFW_MOUSE_BUTTON_3);
	TRANSLATE_MOUSE_BUTTON(MOUSE_BUTTON_THUMB_UP,   GLFW_MOUSE_BUTTON_4);
	TRANSLATE_MOUSE_BUTTON(MOUSE_BUTTON_THUMB_DOWN, GLFW_MOUSE_BUTTON_5);

	TRANSLATE_KEY(KEYBOARD_BUTTON_UP,            GLFW_KEY_UP);
	TRANSLATE_KEY(KEYBOARD_BUTTON_DOWN,          GLFW_KEY_DOWN);
	TRANSLATE_KEY(KEYBOARD_BUTTON_LEFT,          GLFW_KEY_LEFT);
	TRANSLATE_KEY(KEYBOARD_BUTTON_RIGHT,         GLFW_KEY_RIGHT);
	TRANSLATE_KEY(KEYBOARD_BUTTON_SPACE,         GLFW_KEY_SPACE);
	TRANSLATE_KEY(KEYBOARD_BUTTON_RIGHT_SHIFT,   GLFW_KEY_RIGHT_SHIFT);
	TRANSLATE_KEY(KEYBOARD_BUTTON_LEFT_SHIFT,    GLFW_KEY_LEFT_SHIFT);
	TRANSLATE_KEY(KEYBOARD_BUTTON_F1,            GLFW_KEY_F1);
	TRANSLATE_KEY(KEYBOARD_BUTTON_F2,            GLFW_KEY_F2);
	TRANSLATE_KEY(KEYBOARD_BUTTON_F3,            GLFW_KEY_F3);
	TRANSLATE_KEY(KEYBOARD_BUTTON_F4,            GLFW_KEY_F4);
	TRANSLATE_KEY(KEYBOARD_BUTTON_F5,            GLFW_KEY_F5);
	TRANSLATE_KEY(KEYBOARD_BUTTON_F6,            GLFW_KEY_F6);
	TRANSLATE_KEY(KEYBOARD_BUTTON_F7,            GLFW_KEY_F7);
	TRANSLATE_KEY(KEYBOARD_BUTTON_F8,            GLFW_KEY_F8);
	TRANSLATE_KEY(KEYBOARD_BUTTON_F9,            GLFW_KEY_F9);
	TRANSLATE_KEY(KEYBOARD_BUTTON_F10,           GLFW_KEY_F10);
	TRANSLATE_KEY(KEYBOARD_BUTTON_F11,           GLFW_KEY_F11);
	TRANSLATE_KEY(KEYBOARD_BUTTON_F12,           GLFW_KEY_F12);
	TRANSLATE_KEY(KEYBOARD_BUTTON_ENTER,         GLFW_KEY_ENTER);
	TRANSLATE_KEY(KEYBOARD_BUTTON_ESCAPE,        GLFW_KEY_ESCAPE);
	TRANSLATE_KEY(KEYBOARD_BUTTON_HOME,          GLFW_KEY_HOME);
	TRANSLATE_KEY(KEYBOARD_BUTTON_RIGHT_CONTROL, GLFW_KEY_RIGHT_CONTROL);
	TRANSLATE_KEY(KEYBOARD_BUTTON_LEFT_CONTROL,  GLFW_KEY_LEFT_CONTROL);
	TRANSLATE_KEY(KEYBOARD_BUTTON_DELETE,        GLFW_KEY_DELETE);
	TRANSLATE_KEY(KEYBOARD_BUTTON_BACKSPACE,     GLFW_KEY_BACKSPACE);
	TRANSLATE_KEY(KEYBOARD_BUTTON_PAGE_DOWN,     GLFW_KEY_PAGE_DOWN);
	TRANSLATE_KEY(KEYBOARD_BUTTON_PAGE_UP,       GLFW_KEY_PAGE_UP);
	TRANSLATE_KEY(KEYBOARD_BUTTON_A,             GLFW_KEY_A);
	TRANSLATE_KEY(KEYBOARD_BUTTON_B,             GLFW_KEY_B);
	TRANSLATE_KEY(KEYBOARD_BUTTON_C,             GLFW_KEY_C);
	TRANSLATE_KEY(KEYBOARD_BUTTON_D,             GLFW_KEY_D);
	TRANSLATE_KEY(KEYBOARD_BUTTON_E,             GLFW_KEY_E);
	TRANSLATE_KEY(KEYBOARD_BUTTON_F,             GLFW_KEY_F);
	TRANSLATE_KEY(KEYBOARD_BUTTON_G,             GLFW_KEY_G);
	TRANSLATE_KEY(KEYBOARD_BUTTON_H,             GLFW_KEY_H);
	TRANSLATE_KEY(KEYBOARD_BUTTON_I,             GLFW_KEY_I);
	TRANSLATE_KEY(KEYBOARD_BUTTON_J,             GLFW_KEY_J);
	TRANSLATE_KEY(KEYBOARD_BUTTON_K,             GLFW_KEY_K);
	TRANSLATE_KEY(KEYBOARD_BUTTON_L,             GLFW_KEY_L);
	TRANSLATE_KEY(KEYBOARD_BUTTON_M,             GLFW_KEY_M);
	TRANSLATE_KEY(KEYBOARD_BUTTON_N,             GLFW_KEY_N);
	TRANSLATE_KEY(KEYBOARD_BUTTON_O,             GLFW_KEY_O);
	TRANSLATE_KEY(KEYBOARD_BUTTON_P,             GLFW_KEY_P);
	TRANSLATE_KEY(KEYBOARD_BUTTON_Q,             GLFW_KEY_Q);
	TRANSLATE_KEY(KEYBOARD_BUTTON_R,             GLFW_KEY_R);
	TRANSLATE_KEY(KEYBOARD_BUTTON_S,             GLFW_KEY_S);
	TRANSLATE_KEY(KEYBOARD_BUTTON_T,             GLFW_KEY_T);
	TRANSLATE_KEY(KEYBOARD_BUTTON_U,             GLFW_KEY_U);
	TRANSLATE_KEY(KEYBOARD_BUTTON_V,             GLFW_KEY_V);
	TRANSLATE_KEY(KEYBOARD_BUTTON_W,             GLFW_KEY_W);
	TRANSLATE_KEY(KEYBOARD_BUTTON_X,             GLFW_KEY_X);
	TRANSLATE_KEY(KEYBOARD_BUTTON_Y,             GLFW_KEY_Y);
	TRANSLATE_KEY(KEYBOARD_BUTTON_Z,             GLFW_KEY_Z);

	previous_mouse_x = input->mouse_x;
	previous_mouse_y = input->mouse_y;
	glfwGetCursorPos(glfw_window, &input->mouse_x, &input->mouse_y);
	input->mouse_x_delta = input->mouse_x - previous_mouse_x;
	input->mouse_y_delta = previous_mouse_y - input->mouse_y; // GLFW has top-left as origin

	if (was_inhibited != input->inhibit_cursor)
	{
		if (input->inhibit_cursor)
		{
			glfwSetInputMode(glfw_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
			if (glfwRawMouseMotionSupported())
			{
				glfwSetInputMode(glfw_window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
			}
		}
		else
		{
			glfwSetInputMode(glfw_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		}

		was_inhibited = input->inhibit_cursor;
	}
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