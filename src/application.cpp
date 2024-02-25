#include "application.h"

#include "common.h"
#include "hot_reload.h"
#include "input.h"
#include "gfx_context.h"
#include "renderer.h"

#include <algorithm>
#include <numbers>
#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_vulkan.h>

// Private functions
void build_ui();
void build_info_window();
void build_scene_window();

void application_entry(Platform* p_platform)
{
	spdlog::info("Rendering demos startup");

	app = new Application{};
	platform = p_platform;

	hot_reload_init();
	p_platform->window_init(Window_Params{ .name = "Rendering demos", .size = {1280, 720} });
	input_init();
	gfx_context_init();
	imgui_init();
	camera_init();
	renderer_init();

	timings = new Timings{ .zero_delta = true }; // First frame have dt = 0

	spdlog::info("Initialization done, running");

	while (!p_platform->window_requested_to_close())
	{
		timings_new_frame();

		hot_reload_dispatch(); // TODO: hold delta time measuring when hot reloading (exclude reload time from dt)

		p_platform->poll_events();
		p_platform->fill_input();

		renderer->upload_heap.begin_frame();

		imgui_new_frame();

		camera_update();
		build_ui();

		debug_pass->draw_line(glm::vec3(0.0, 0.0, 0.0), scene_data->sun.direction, glm::vec3(1.0, 0.0, 0.0));
		for (auto& light : scene_data->point_lights)
		{
			debug_pass->draw_sphere(light.position, light.radius, 10, 10, glm::vec3(0.5, 0.5, 0.5));
		}

		scene_data->sun.direction = glm::normalize(glm::vec3(
			cos(3.14 / 180.0 * scene_data->yaw) * cos(3.14 / 180.0 * scene_data->pitch),
			sin(3.14 / 180.0 * scene_data->pitch),
			sin(3.14 / 180.0 * scene_data->yaw) * cos(3.14 / 180.0 * scene_data->pitch)));

		renderer_dispatch();

		app->frame_number++;
	}

	spdlog::info("Exiting...");

	delete timings;

	renderer_deinit();
	imgui_deinit();
	gfx_context_deinit();
	input_destroy();

	p_platform->window_destroy();
	hot_reload_close();
}

void build_ui()
{
	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("App"))
		{
			ImGui::MenuItem("Info", nullptr, &app->ui.windows.info);
			ImGui::Separator();
			ImGui::MenuItem("Hot reload", nullptr, &app->ui.windows.hot_reload);
			ImGui::MenuItem("Camera", nullptr, &app->ui.windows.camera);
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}

	if (app->ui.windows.info)       build_info_window();
	if (app->ui.windows.hot_reload) hot_reload_build_ui();
	if (app->ui.windows.camera)     camera_build_ui();

	build_scene_window();
}

void build_info_window()
{
	ImGui::Begin("Info", &app->ui.windows.info);
	{
		ImGui::Text("Info");
	}
	ImGui::End();
}

void build_scene_window()
{
	ImGui::Begin("Scene");
	{
		if (ImGui::CollapsingHeader("Sun"))
		{
			ImGui::DragFloat("Yaw", &scene_data->yaw, 1.0f, 0.0f, 0.0f, "%.1f°");
			ImGui::DragFloat("Pitch", &scene_data->pitch, 1.0f, -90.f, 90.f, "%.1f°");

			if (scene_data->yaw < -180) scene_data->yaw += 360;
			if (scene_data->yaw > 180)  scene_data->yaw -= 360;

			ImGui::SliderFloat("Intensity", &scene_data->sun.intensity, 0, 1);
		}

		if (ImGui::CollapsingHeader("Point Lights"))
		{
			if (ImGui::Button("+"))
			{
				scene_data->point_lights.push_back({ .position = {0, 0, 0}, .intensity = 1, .radius = 1 });
			}

			// TODO check this
			for (size_t i = 0; i < scene_data->point_lights.size(); i++)
			{
				Point_Light& light = scene_data->point_lights[i];

				ImGui::PushID(i);
				if (ImGui::Button("X"))
				{
					scene_data->point_lights.erase(scene_data->point_lights.begin() + i);
					ImGui::PopID();
					break;
				}
				ImGui::DragFloat3("Position", glm::value_ptr(light.position));
				ImGui::SliderFloat("Intensity", &light.intensity, 0, 1);
				ImGui::DragFloat("Radius", &light.radius);
				ImGui::PopID();
			}
		}
	}
	ImGui::End();
}

void timings_new_frame()
{
	// When called at start of a frame, frame_time_stamp indicated previous frame
	auto now = std::chrono::high_resolution_clock::now();
	auto time_span = std::chrono::duration_cast<std::chrono::duration<float>>(now - timings->frame_time_stamp);
	timings->delta_time = time_span.count();
	timings->frame_time_stamp = now;

	if (timings->zero_delta)
	{
		timings->delta_time = 0;
		timings->zero_delta = false;
	}
}

void imgui_init()
{
	ZoneScopedN("ImGui init");

	imgui_data = new ImGui_Data;

	{
		VkDescriptorPoolSize pool_sizes[] = {
			{ VK_DESCRIPTOR_TYPE_SAMPLER,                1000 },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
			{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,   1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,   1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
			{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,       1000 },

		};

		VkDescriptorPoolCreateInfo pool_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
			.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes),
			.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes),
			.pPoolSizes = pool_sizes,
		};

		vkCreateDescriptorPool(gfx_context->device, &pool_info, nullptr, &imgui_data->descriptor_pool);
	}

	ImGui::CreateContext();
	ImGui::StyleColorsDark();

	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	io.Fonts->AddFontDefault();

	platform->imgui_init();

	ImGui_ImplVulkan_InitInfo init_info = {
		.Instance       = gfx_context->instance,
		.PhysicalDevice = gfx_context->physical_device,
		.Device         = gfx_context->device,
		.QueueFamily    = gfx_context->gfx_queue_family_index,
		.Queue          = gfx_context->gfx_queue,
		.PipelineCache  = VK_NULL_HANDLE,
		.DescriptorPool = imgui_data->descriptor_pool,
		.MinImageCount  = gfx_context->swapchain.images_count,
		.ImageCount     = gfx_context->swapchain.images_count,
		.MSAASamples    = VK_SAMPLE_COUNT_1_BIT,
		.UseDynamicRendering   = true,
		.ColorAttachmentFormat = gfx_context->swapchain.selected_format.format,
	};
	ImGui_ImplVulkan_Init(&init_info, VK_NULL_HANDLE);

	{
		VkCommandPoolCreateInfo command_pool_create_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
			.queueFamilyIndex = gfx_context->gfx_queue_family_index,
		};
		VkCommandPool pool;
		vkCreateCommandPool(gfx_context->device, &command_pool_create_info, nullptr, &pool);

		VkCommandBuffer command_buffer;

		VkCommandBufferAllocateInfo allocate_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool        = pool,
			.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1,
		};
		vkAllocateCommandBuffers(gfx_context->device, &allocate_info, &command_buffer);

		VkCommandBufferBeginInfo begin_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		};
		vkBeginCommandBuffer(command_buffer, &begin_info);

		ImGui_ImplVulkan_CreateFontsTexture(command_buffer);

		VkSubmitInfo end_info = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.commandBufferCount = 1,
			.pCommandBuffers    = &command_buffer,
		};
		vkEndCommandBuffer(command_buffer);

		vkQueueSubmit(gfx_context->gfx_queue, 1, &end_info, VK_NULL_HANDLE);
		vkDeviceWaitIdle(gfx_context->device);

		ImGui_ImplVulkan_DestroyFontUploadObjects();
	}
}

void imgui_deinit()
{
	delete imgui_data;
}

void imgui_new_frame()
{
	ZoneScopedN("ImGui new frame");

	ImGui_ImplVulkan_NewFrame();
	platform->imgui_new_frame();
	ImGui::NewFrame();
}

void camera_init()
{
	camera = new Camera{
		.position = {0.f, 0.f, -2.f},
		.front = {0, 0, 1},
		.up = {0, 1, 0},
		.right = {1, 0, 0},
		.yaw = 90,
		.pitch = 0,
		.velocity = 20,
		.speed_multiplier = 5,
		.mouse_sensitivity = 0.05,
	};
}


void camera_deinit()
{
	delete camera;
}

void camera_update()
{
	ZoneScopedN("Camera update");

	// FIXME: this camera works... but there are many inconsistencies with system handedness
	// that should be fixed ASAP as I have a feeling that bumping into them will be very common

	// Focus window
	if (input->buttons_states[Button::MOUSE_BUTTON_RIGHT] == Button_State::PRESSED && !ImGui::GetIO().WantCaptureMouse)
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
	camera->yaw   += static_cast<float>(input->mouse_x_delta) * camera->mouse_sensitivity;
	camera->pitch += static_cast<float>(input->mouse_y_delta) * camera->mouse_sensitivity;
	camera->pitch = std::clamp(camera->pitch, -89.f, 89.f);
	camera->front = glm::normalize(glm::vec3{
		cos(glm::radians(camera->yaw)) * cos(glm::radians(camera->pitch)),
		sin(glm::radians(camera->pitch)),
		sin(glm::radians(camera->yaw)) * cos(glm::radians(camera->pitch)),
	});
	camera->right = glm::normalize(glm::cross(WORLD_UP, camera->front));
	camera->up    = glm::normalize(glm::cross(camera->front, camera->right));

	// Movement
	glm::vec3 delta_position = {};
	if (input->buttons_states[Button::KEYBOARD_BUTTON_W]) delta_position += camera->front;
	if (input->buttons_states[Button::KEYBOARD_BUTTON_S]) delta_position -= camera->front;
	if (input->buttons_states[Button::KEYBOARD_BUTTON_A]) delta_position += camera->right; // Handedness whatever
	if (input->buttons_states[Button::KEYBOARD_BUTTON_D]) delta_position -= camera->right;
	if (input->buttons_states[Button::KEYBOARD_BUTTON_E]) delta_position += WORLD_UP;
	if (input->buttons_states[Button::KEYBOARD_BUTTON_Q]) delta_position -= WORLD_UP;

	if (delta_position != glm::zero<glm::vec3>()) // TODO swap for epsilon compare
	{
		camera->position += glm::normalize(delta_position) * (camera->velocity * timings->delta_time *
													  (input->buttons_states[Button::KEYBOARD_BUTTON_LEFT_SHIFT] ?
													  camera->speed_multiplier : 1));
	}
}

glm::mat4x4 camera_get_view_matrix()
{
	return glm::lookAt(camera->position, camera->position + camera->front, camera->up);
}

void camera_build_ui()
{
	ImGui::Begin("Camera");
	ImGui::Text("Yaw: %f", camera->yaw);
	ImGui::Text("Pitch: %f", camera->pitch);
	ImGui::Text("Front: %f %f %f", camera->front.x, camera->front.y, camera->front.z);
	ImGui::Text("Up: %f %f %f", camera->up.x, camera->up.y, camera->up.z);
	ImGui::Text("Right: %f %f %f", camera->right.x, camera->right.y, camera->right.z);
	ImGui::DragFloat3("Position:", glm::value_ptr(camera->position), 0.1, -100, 100);
	ImGui::DragFloat("Mouse sens:", &camera->mouse_sensitivity, 0.005, 0.001, 1);
	ImGui::DragFloat("Camera speed:", &camera->velocity, 0.005, 0.001, 50);
	ImGui::DragFloat("Fast multiplier:", &camera->speed_multiplier, 0.005, 1, 10);
	ImGui::Text("dx: %f  dy: %f", input->mouse_x_delta, input->mouse_y_delta);
	if (input->buttons_states[Button::KEYBOARD_BUTTON_LEFT_SHIFT])
	{
		ImGui::Text("SHIFT");
	}
	ImGui::End();
}