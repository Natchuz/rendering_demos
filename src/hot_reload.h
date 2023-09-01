#pragma once

#if LIVEPP_ENABLED
#include <LivePP/API/LPP_API_x64_CPP.h>
#endif

// Manages code hot reloading
struct Hot_Reload
{
#if LIVEPP_ENABLED
	lpp::LppSynchronizedAgent lpp_agent;

	bool rebuild_frame_data = true; // Reload settings and window
#endif
};

inline Hot_Reload* hot_reload; // Global handle initialized by hot_reload_init()

void hot_reload_init();
void hot_reload_close();
void hot_reload_dispatch();

void hot_reload_build_ui();