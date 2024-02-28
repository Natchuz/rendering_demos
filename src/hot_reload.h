#pragma once

#if LIVEPP_ENABLED
#include <LivePP/API/x64/LPP_API_x64_CPP.h>
#endif

// Manages code hot reloading
struct Hot_Reload
{
	static Hot_Reload* ptr; // Global handle

#if LIVEPP_ENABLED
	lpp::LppSynchronizedAgent lpp_agent;
#endif

	Hot_Reload();
	~Hot_Reload();

	void dispatch_reload();
	void display_ui();
};