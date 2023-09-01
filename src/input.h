#pragma once

// Please update Input::buttons_states if number of elements in this enum changes.
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

// This struct gets updated and read by the platform
struct Input
{
	// Last reported state of a button.
	// Please update if number of elements in Button enum changes.
	Button_State buttons_states[256];

	double mouse_x,       mouse_y;
	double mouse_x_delta, mouse_y_delta;

	bool inhibit_cursor = false; // Set this to control cursor. This might also enable raw input if supported
};

inline Input* input;

// Following methods are so trivial it makes no sense for separate .cpp file

inline void input_init()
{
	input = new Input{};
}

inline void input_destroy()
{
	delete input;
}