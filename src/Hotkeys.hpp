#pragma once

#include <obs.h>

#include <vector>

namespace obsviewer {

// OBSViewer actions in OBS ▸ Settings ▸ Hotkeys (so keyboard shortcuts and a
// Stream Deck's "Hotkey" buttons can drive them). Bindings are saved in the
// plugin's config folder (hotkeys.json). Main thread.
class Hotkeys {
public:
	static Hotkeys &get();
	void registerAll();
	void save() const;

private:
	Hotkeys() = default;
	struct Entry {
		obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
		const char *name = nullptr; // stable key used for saving
	};
	std::vector<Entry> entries_;
};

} // namespace obsviewer
