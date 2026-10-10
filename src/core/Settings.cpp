#include "core/Settings.hpp"

#include <obs-module.h>
#include <util/platform.h>

#include <plugin-support.h>

namespace obsviewer {

namespace {

constexpr const char *kFileName = "config.json";

std::string getString(obs_data_t *data, const char *key, const std::string &fallback)
{
	if (!obs_data_has_user_value(data, key))
		return fallback;
	const char *value = obs_data_get_string(data, key);
	return value ? std::string(value) : fallback;
}

bool getBool(obs_data_t *data, const char *key, bool fallback)
{
	return obs_data_has_user_value(data, key) ? obs_data_get_bool(data, key) : fallback;
}

} // namespace

Settings &Settings::get()
{
	static Settings settings;
	return settings;
}

void Settings::load()
{
	char *path = obs_module_config_path(kFileName);
	if (!path)
		return;

	obs_data_t *data = os_file_exists(path) ? obs_data_create_from_json_file_safe(path, "bak") : nullptr;
	bfree(path);
	if (!data)
		return;

	const Settings defaults;
	dockLayout = getString(data, "dock_layout", defaults.dockLayout);
	windowLayout = getString(data, "window_layout", defaults.windowLayout);
	windowScreen = getString(data, "window_screen", defaults.windowScreen);
	showLabels = getBool(data, "show_labels", defaults.showLabels);
	clickAction = obs_data_has_user_value(data, "click_action") ? static_cast<int>(obs_data_get_int(data, "click_action"))
								     : defaults.clickAction;
	if (clickAction < 0 || clickAction > 2)
		clickAction = defaults.clickAction;
	fullscreenToolbar = getBool(data, "fullscreen_toolbar", defaults.fullscreenToolbar);
	showSidebar = getBool(data, "show_sidebar", defaults.showSidebar);
	showNetbar = getBool(data, "show_netbar", defaults.showNetbar);
	alarms = getBool(data, "alarms", defaults.alarms);
	tileMeters = getBool(data, "tile_meters", defaults.tileMeters);
	loudnessPreset = obs_data_has_user_value(data, "loudness_preset")
				 ? static_cast<int>(obs_data_get_int(data, "loudness_preset"))
				 : defaults.loudnessPreset;
	customTargetLufs = obs_data_has_user_value(data, "custom_target_lufs")
				   ? static_cast<float>(obs_data_get_double(data, "custom_target_lufs"))
				   : defaults.customTargetLufs;
	meterTrack = obs_data_has_user_value(data, "meter_track") ? static_cast<int>(obs_data_get_int(data, "meter_track"))
								   : defaults.meterTrack;
	if (meterTrack < 0 || meterTrack > 6)
		meterTrack = 0;
	updateAutoCheck = getBool(data, "update_auto_check", defaults.updateAutoCheck);
	updateIncludePrereleases = getBool(data, "update_include_prereleases", defaults.updateIncludePrereleases);
	updateRepo = getString(data, "update_repo", defaults.updateRepo);
	updateSkippedVersion = getString(data, "update_skipped_version", defaults.updateSkippedVersion);
	updateLastCheck = obs_data_has_user_value(data, "update_last_check") ? obs_data_get_int(data, "update_last_check")
									       : 0;
	if (updateRepo.empty())
		updateRepo = defaults.updateRepo;

	obs_data_release(data);
}

void Settings::save() const
{
	char *dir = obs_module_config_path("");
	if (dir) {
		os_mkdirs(dir);
		bfree(dir);
	}

	char *path = obs_module_config_path(kFileName);
	if (!path)
		return;

	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "dock_layout", dockLayout.c_str());
	obs_data_set_string(data, "window_layout", windowLayout.c_str());
	obs_data_set_string(data, "window_screen", windowScreen.c_str());
	obs_data_set_bool(data, "show_labels", showLabels);
	obs_data_set_int(data, "click_action", clickAction);
	obs_data_set_bool(data, "fullscreen_toolbar", fullscreenToolbar);
	obs_data_set_bool(data, "show_sidebar", showSidebar);
	obs_data_set_bool(data, "show_netbar", showNetbar);
	obs_data_set_bool(data, "alarms", alarms);
	obs_data_set_bool(data, "tile_meters", tileMeters);
	obs_data_set_int(data, "loudness_preset", loudnessPreset);
	obs_data_set_double(data, "custom_target_lufs", customTargetLufs);
	obs_data_set_int(data, "meter_track", meterTrack);
	obs_data_set_bool(data, "update_auto_check", updateAutoCheck);
	obs_data_set_bool(data, "update_include_prereleases", updateIncludePrereleases);
	obs_data_set_string(data, "update_repo", updateRepo.c_str());
	obs_data_set_string(data, "update_skipped_version", updateSkippedVersion.c_str());
	obs_data_set_int(data, "update_last_check", updateLastCheck);

	if (!obs_data_save_json_safe(data, path, "tmp", "bak"))
		obs_log(LOG_WARNING, "could not save settings to %s", path);

	obs_data_release(data);
	bfree(path);
}

} // namespace obsviewer
