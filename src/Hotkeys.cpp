#include "Hotkeys.hpp"

#include "App.hpp"
#include "bus/MasterBus.hpp"
#include "core/Settings.hpp"
#include "viewer/Layouts.hpp"

#include <obs-module.h>
#include <util/platform.h>

#include <plugin-support.h>

#include <QMetaObject>

#include <functional>
#include <string>

namespace obsviewer {

namespace {

constexpr const char *kFile = "hotkeys.json";

// Hotkeys fire on OBS's hotkey thread; everything runs on the Qt main thread.
void onMain(std::function<void()> fn)
{
	QMetaObject::invokeMethod(&App::get(), std::move(fn), Qt::QueuedConnection);
}

void setWindowLayout(const std::string &id)
{
	Settings &s = Settings::get();
	s.windowLayout = id;
	s.save();
	App::get().applySettings();
}

struct Action {
	const char *name;
	std::string description;
	std::function<void()> run;
};

std::vector<Action> &actions()
{
	static std::vector<Action> list;
	if (!list.empty())
		return list;
	list.push_back({"obsviewer.open_window", "OBSViewer: Open window", [] { App::get().openWindowed(); }});
	list.push_back(
		{"obsviewer.toggle_fullscreen", "OBSViewer: Fullscreen on / off", [] { App::get().toggleViewerMode(); }});
	list.push_back({"obsviewer.close_window", "OBSViewer: Close window", [] { App::get().closeViewer(); }});
	list.push_back({"obsviewer.next_layout", "OBSViewer: Next layout (window)", [] {
				const auto &all = allLayouts();
				const std::string &cur = layoutById(Settings::get().windowLayout).id;
				size_t i = 0;
				while (i < all.size() && all[i].id != cur)
					++i;
				setWindowLayout(all[(i + 1) % all.size()].id);
			}});
	// One per layout. Names must outlive registration, so they're kept static.
	static std::vector<std::string> layoutKeys;
	for (const LayoutInfo &l : allLayouts())
		layoutKeys.push_back("obsviewer.layout_" + l.id);
	size_t k = 0;
	for (const LayoutInfo &l : allLayouts()) {
		const std::string id = l.id;
		list.push_back({layoutKeys[k++].c_str(), "OBSViewer: Window layout – " + l.name,
				[id] { setWindowLayout(id); }});
	}
	list.push_back({"obsviewer.bus_toggle", "OBSViewer: Master bus on / off", [] {
				std::string error;
				const bool on = !MasterBus::get().enabled();
				if (!MasterBus::get().setEnabled(on, error))
					obs_log(LOG_WARNING, "hotkey: master bus: %s", error.c_str());
				else
					obs_log(LOG_INFO, "hotkey: master bus %s", on ? "on" : "off");
			}});
	list.push_back({"obsviewer.bus_show", "OBSViewer: Show master bus", [] { App::get().openMasterBus(nullptr); }});
	return list;
}

void onHotkey(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	const auto *action = static_cast<const Action *>(data);
	onMain(action->run);
}

std::string configFile()
{
	char *path = obs_module_config_path(kFile);
	std::string p = path ? path : "";
	bfree(path);
	return p;
}

} // namespace

Hotkeys &Hotkeys::get()
{
	static Hotkeys hotkeys;
	return hotkeys;
}

void Hotkeys::registerAll()
{
	if (!entries_.empty())
		return;
	const std::string path = configFile();
	obs_data_t *saved = !path.empty() && os_file_exists(path.c_str())
				    ? obs_data_create_from_json_file_safe(path.c_str(), "bak")
				    : nullptr;
	for (Action &a : actions()) {
		const obs_hotkey_id id = obs_hotkey_register_frontend(a.name, a.description.c_str(), &onHotkey, &a);
		if (id == OBS_INVALID_HOTKEY_ID)
			continue;
		if (saved) {
			if (obs_data_array_t *keys = obs_data_get_array(saved, a.name)) {
				obs_hotkey_load(id, keys);
				obs_data_array_release(keys);
			}
		}
		entries_.push_back({id, a.name});
	}
	if (saved)
		obs_data_release(saved);
}

void Hotkeys::save() const
{
	const std::string path = configFile();
	if (path.empty() || entries_.empty())
		return;
	obs_data_t *data = obs_data_create();
	for (const Entry &e : entries_) {
		obs_data_array_t *keys = obs_hotkey_save(e.id);
		obs_data_set_array(data, e.name, keys);
		obs_data_array_release(keys);
	}
	if (char *dir = obs_module_config_path("")) {
		os_mkdirs(dir);
		bfree(dir);
	}
	obs_data_save_json_safe(data, path.c_str(), "tmp", "bak");
	obs_data_release(data);
}

} // namespace obsviewer
