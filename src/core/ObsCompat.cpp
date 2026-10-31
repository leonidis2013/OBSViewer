#include "core/ObsCompat.hpp"

#include <dlfcn.h>

namespace obsviewer::compat {

bool obs33OrNewer()
{
	return (obs_get_version() >> 24) >= 33;
}

void enableMonitoring(obs_source_t *source)
{
	if (!source)
		return;
	if (obs33OrNewer()) {
		// Not in the OBS 32 headers this is built against: look it up.
		using SetEnabled = void (*)(obs_source_t *, bool);
		static const auto setEnabled =
			reinterpret_cast<SetEnabled>(dlsym(RTLD_DEFAULT, "obs_source_set_monitoring_enabled"));
		if (setEnabled) {
			setEnabled(source, true);
			return;
		}
	}
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
	obs_source_set_monitoring_type(source, OBS_MONITORING_TYPE_MONITOR_ONLY);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
}

} // namespace obsviewer::compat
