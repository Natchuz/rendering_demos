#include "common.h"
#include "gfx_context.h"
#include "renderer.h"

#include <volk.h>
#include <imgui.h>

#include "hot_reload.h"

Hot_Reload* Hot_Reload::ptr;

Hot_Reload::Hot_Reload()
{
#if LIVEPP_ENABLED
	ZoneScopedN("Hot reload startup");

	lpp::LppLocalPreferences preferences = lpp::LppCreateDefaultLocalPreferences();
	lpp_agent = lpp::LppCreateSynchronizedAgent(&preferences, L"sdk/LivePP");

	if (!lpp::LppIsValidSynchronizedAgent(&lpp_agent))
	{
		throw std::runtime_error("Live++ error: invalid agent");
	}
	lpp_agent.EnableModule(lpp::LppGetCurrentModulePath(), lpp::LPP_MODULES_OPTION_ALL_IMPORT_MODULES,
						   nullptr, nullptr);

	spdlog::info("Enabled Live++ agent");
#endif
}

Hot_Reload::~Hot_Reload()
{
#if LIVEPP_ENABLED
	ZoneScopedN("Hot reload shutdown");

	lpp::LppDestroySynchronizedAgent(&lpp_agent);
#endif
}

void Hot_Reload::dispatch_reload()
{
#if LIVEPP_ENABLED
	if (lpp_agent.WantsReload(lpp::LPP_RELOAD_OPTION_SYNCHRONIZE_WITH_COMPILATION_AND_RELOAD))
	{
		ZoneScopedN("Hot reloading process");

		auto start_time = std::chrono::high_resolution_clock::now();

		lpp_agent.Reload(lpp::LPP_RELOAD_BEHAVIOUR_WAIT_UNTIL_CHANGES_ARE_APPLIED);
		spdlog::info("Live++ reloaded. Reinitializing...");

		auto finish_time = std::chrono::high_resolution_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::duration<float>>(finish_time - start_time);
		spdlog::info("-----[ Hot reload done! ({} s) ]-----", duration.count());
	}
#endif
}

void Hot_Reload::display_ui()
{
	ImGui::Begin("Hot reloading");

#if LIVEPP_ENABLED
	ImGui::Text("Live++ version %s", LPP_VERSION);
	
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