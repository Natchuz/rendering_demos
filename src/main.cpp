#include "app.h"

auto main() -> int32_t
{
	if (!init_global_libs()) return 1;

	bool error = false;
	App::ptr = new App;

	if (!App::ptr->init())
	{
		error = true;
		goto deinit_app;
	}
	App::ptr->run();

	deinit_app:
	App::ptr->deinit();
	deinit_libs:
	deinit_global_libs();

	return error ? 1 : 0;
}