#include "common.h"
#include "gfx_context.h"
#include "renderer.h"

#include <volk.h>
#include <imgui.h>

#include "hot_reload.h"

// Private functions
#if LIVEPP_ENABLED
void pre_reload_cleanup();           // Deinitialize all resources that are bound to be hot reloaded
void post_reload_reinitialization(); // Initialize resources after reload
#endif

void hot_reload_init()
{
#if LIVEPP_ENABLED
	ZoneScopedN("Hot reload initialization");

	hot_reload = new Hot_Reload{};

	hot_reload->lpp_agent = lpp::LppCreateSynchronizedAgent(L"sdk/LivePP");

	if (!lpp::LppIsValidSynchronizedAgent(&hot_reload->lpp_agent))
	{
		//throw std::runtime_error("Live++ error: invalid agent"); TODO
	}
	hot_reload->lpp_agent.EnableModule(lpp::LppGetCurrentModulePath(), lpp::LPP_MODULES_OPTION_ALL_IMPORT_MODULES,
						   nullptr, nullptr);

	spdlog::info("Enabled Live++ agent");
#endif
}

void hot_reload_close()
{
#if LIVEPP_ENABLED
	ZoneScopedN("Hot reload deinitialization");

	assert(hot_reload != nullptr);

	lpp::LppDestroySynchronizedAgent(&hot_reload->lpp_agent);
	delete hot_reload;

	spdlog::info("Destroyed Live++ agent");
#endif
}

void hot_reload_dispatch()
{
#if LIVEPP_ENABLED
	assert(hot_reload != nullptr);

	if (hot_reload->lpp_agent.WantsReload())
	{
		ZoneScopedN("Hot reloading process");

		auto start_time = std::chrono::high_resolution_clock::now();
		//spdlog::info("-----[ Beginning hot reload (frame {}) ]-----", app->frame_number);
		spdlog::info("Cleanup...");

		pre_reload_cleanup();

		hot_reload->lpp_agent.CompileAndReloadChanges(lpp::LPP_RELOAD_BEHAVIOUR_WAIT_UNTIL_CHANGES_ARE_APPLIED);
		spdlog::info("Live++ reloaded. Reinitializing...");

		post_reload_reinitialization();

		auto finish_time = std::chrono::high_resolution_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::duration<float>>(finish_time - start_time);
		spdlog::info("-----[ Hot reload done! ({} s) ]-----", duration.count());
	}
#endif
}

void hot_reload_build_ui()
{
	ZoneScopedN("Hot reloading UI");

	ImGui::Begin("Hot reloading");

#if LIVEPP_ENABLED
	ImGui::Text("Live++ version %s", LPP_VERSION);
	ImGui::SeparatorText("On reload:");
	ImGui::Checkbox("Rebuild frame data", &hot_reload->rebuild_frame_data);

	ImGui::Separator();

	if (ImGui::Button("Schedule hot reload"))
	{
		hot_reload->lpp_agent.ScheduleReload();
	}
#else
	ImGui::Text("Live++ disabled!");
#endif

	ImGui::End();
}

#if LIVEPP_ENABLED

void pre_reload_cleanup()
{
	ZoneScopedN("Pre reload cleanup");

	vkDeviceWaitIdle(gfx_context->device);

	if (hot_reload->rebuild_frame_data)
	{
		renderer_destroy_frame_data();
	}
}

void post_reload_reinitialization()
{
	ZoneScopedN("Post reload reinitialization");

	if (hot_reload->rebuild_frame_data)
	{
		renderer_create_frame_data();
	}
}

#endif