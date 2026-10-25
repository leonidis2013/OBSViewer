/*
OBSViewer — ATEM-style multiview with stream and audio metrics for OBS Studio.
Copyright (C) 2026 leonidis2013

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "App.hpp"
#include "audio/StreamMonitor.hpp"
#include "bus/MasterBus.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <curl/curl.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_name(void)
{
	return "OBSViewer";
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return "ATEM-style multiview with tally, labels and stream status, plus an in-plugin updater.";
}

bool obs_module_load(void)
{
	curl_global_init(CURL_GLOBAL_DEFAULT);
	obsviewer::MasterBus::registerSource();
	obsviewer::StreamMonitor::registerSource();
	obsviewer::App::get().init();
	obs_log(LOG_INFO, "loaded (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obsviewer::App::get().shutdown();
	curl_global_cleanup();
	obs_log(LOG_INFO, "unloaded");
}
