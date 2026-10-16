// JUCE side of the master bus: hosts VST3 / Audio Unit plugins in a serial chain.
#include "bus/Engine.hpp"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <mutex>
#include <thread>

namespace obsviewer::bus {

namespace {

constexpr int kMaxChannels = 16;
// A plugin that takes longer than this to be scanned is treated as hung.
constexpr juce::uint32 kScanTimeoutMs = 45000;

float toDb(float lin)
{
	return lin > 1.0e-6f ? 20.0f * std::log10(lin) : -120.0f;
}

float fromDb(float db)
{
	return std::pow(10.0f, db / 20.0f);
}

// Decaying peak per channel (instant attack, ~20 dB/s fall).
struct PeakMeter {
	std::atomic<float> peak[2];
	PeakMeter()
	{
		peak[0] = 0.0f;
		peak[1] = 0.0f;
	}
	void feed(const float *const *ch, int frames, double sampleRate)
	{
		const float decay = std::pow(10.0f, -1.0f * static_cast<float>(frames) /
							    static_cast<float>(std::max(1.0, sampleRate)));
		for (int c = 0; c < 2; ++c) {
			float block = 0.0f;
			for (int i = 0; i < frames; ++i)
				block = std::max(block, std::fabs(ch[c][i]));
			const float held = peak[c].load(std::memory_order_relaxed) * decay;
			peak[c].store(std::max(block, held), std::memory_order_relaxed);
		}
	}
	float db(int c) const { return toDb(peak[c].load(std::memory_order_relaxed)); }
};

void applyGain(float *const *ch, int frames, float gain)
{
	if (std::fabs(gain - 1.0f) < 1.0e-6f)
		return;
	for (int c = 0; c < 2; ++c)
		juce::FloatVectorOperations::multiply(ch[c], gain, frames);
}

// Floating window for a plugin's own editor.
class EditorWindow : public juce::DocumentWindow {
public:
	EditorWindow(const juce::String &title, juce::AudioProcessorEditor *editor)
		: juce::DocumentWindow(title, juce::Colours::black, juce::DocumentWindow::closeButton)
	{
		setUsingNativeTitleBar(true);
		setContentOwned(editor, true);
		setResizable(editor->isResizable(), false);
		centreWithSize(getWidth(), getHeight());
		setVisible(true);
		toFront(true);
	}
	void closeButtonPressed() override { setVisible(false); }
};

} // namespace

struct Slot {
	std::unique_ptr<juce::AudioPluginInstance> plugin;
	juce::PluginDescription desc;
	std::atomic<bool> bypass{false};
	std::atomic<float> inGain{1.0f};
	std::atomic<float> outGain{1.0f};
	float inDb = 0.0f, outDb = 0.0f;
	PeakMeter pre, post;
	std::unique_ptr<EditorWindow> window;
	bool wide = false; // plugin wants more than 2 channels

	~Slot()
	{
		window.reset(); // the editor must go before its processor
		if (plugin)
			plugin->releaseResources();
	}
};

struct Engine::Impl {
	bool initialised = false;
	std::string dataDir;
	juce::AudioPluginFormatManager formats;
	juce::KnownPluginList known;

	// Only the main thread changes `chain`, and it holds this lock while it does.
	// The audio thread holds it while processing. Main-thread reads need no lock
	// (nothing else writes), so meters and the UI never stall the audio.
	mutable std::mutex chainLock;
	std::vector<std::shared_ptr<Slot>> chain;

	double sampleRate = 48000.0;
	int maxBlock = 1024;
	juce::AudioBuffer<float> wideBuffer{kMaxChannels, 1024};
	juce::MidiBuffer midi;

	std::atomic<float> masterIn{1.0f}, masterOut{1.0f};
	float masterInDb = 0.0f, masterOutDb = 0.0f;
	PeakMeter masterPre, masterPost;

	// scanning (out of process)
	struct ScanJob {
		int format = 0;
		juce::String file;
	};
	std::string helperPath;
	std::deque<ScanJob> queue;
	size_t queueTotal = 0;
	bool scanActive = false;
	std::unique_ptr<juce::ChildProcess> child;
	ScanJob childJob;
	std::string childName;
	juce::uint32 childStarted = 0;
	std::string startupWarning;

	uint64_t generation = 0; // bumped when the chain is cleared; stale async loads are dropped
	uint64_t revision = 0;   // bumped on any chain or plugin-list change (for the UI)
	bool loadingChain = false;
	std::string pendingChain;

	juce::File dir() const { return juce::File(juce::String::fromUTF8(dataDir.c_str())); }
	juce::File listFile() const { return dir().getChildFile("plugins.xml"); }
	juce::File resultFile() const { return dir().getChildFile("scan-result.xml"); }
	// Holds the identifier of a plugin while it is being created inside OBS.
	juce::File loadingFile() const { return dir().getChildFile("loading-plugin.txt"); }

	void killChild()
	{
		if (child) {
			if (child->isRunning())
				child->kill();
			child.reset();
		}
	}

	// Display name for a plugin file or AU identifier (never loads the plugin).
	juce::String shortName(const juce::String &file)
	{
		if (file.startsWith("AudioUnit:")) {
			for (auto *f : formats.getFormats())
				if (f->getName() == "AudioUnit")
					return f->getNameOfPluginFromIdentifier(file);
			return file;
		}
		return juce::File(file).getFileNameWithoutExtension();
	}

	bool launch(const ScanJob &job)
	{
		resultFile().deleteFile();
		juce::StringArray args;
		args.add(juce::String::fromUTF8(helperPath.c_str()));
		args.add(formats.getFormat(job.format)->getName());
		args.add(job.file);
		args.add(resultFile().getFullPathName());
		child = std::make_unique<juce::ChildProcess>();
		if (!child->start(args, 0)) {
			child.reset();
			return false;
		}
		childJob = job;
		childName = shortName(job.file).toStdString();
		childStarted = juce::Time::getMillisecondCounter();
		return true;
	}

	// The helper writes its result file only after a clean scan; no file means
	// it crashed, hung or found nothing loadable.
	void collect(bool timedOut)
	{
		const juce::File result = resultFile();
		std::unique_ptr<juce::XmlElement> xml = timedOut ? nullptr : juce::parseXML(result);
		result.deleteFile();
		bool found = false;
		if (xml) {
			for (auto *e : xml->getChildIterator()) {
				juce::PluginDescription d;
				if (d.loadFromXml(*e)) {
					known.addType(d);
					found = true;
				}
			}
		}
		if (!found)
			known.addToBlacklist(childJob.file);
	}

	void saveList() const
	{
		if (auto xml = known.createXml())
			xml->writeTo(listFile());
	}

	void configure(juce::AudioPluginInstance &p, Slot &s) const
	{
		// Ask for plain stereo; fall back to whatever the plugin insists on.
		juce::AudioProcessor::BusesLayout stereo;
		stereo.inputBuses.add(juce::AudioChannelSet::stereo());
		stereo.outputBuses.add(juce::AudioChannelSet::stereo());
		if (p.getBusCount(true) > 0 && p.getBusCount(false) > 0 && p.checkBusesLayoutSupported(stereo))
			p.setBusesLayout(stereo);
		p.enableAllBuses();
		p.setRateAndBufferSizeDetails(sampleRate, maxBlock);
		p.setNonRealtime(false);
		p.prepareToPlay(sampleRate, maxBlock);
		s.wide = std::max(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()) != 2;
	}

	// Main thread only.
	std::shared_ptr<Slot> at(int index) const
	{
		return index >= 0 && static_cast<size_t>(index) < chain.size() ? chain[static_cast<size_t>(index)] : nullptr;
	}
};

Engine &Engine::get()
{
	static Engine engine;
	return engine;
}

Engine::Engine() : impl_(std::make_unique<Impl>()) {}
Engine::~Engine() = default;

bool Engine::init(const std::string &dataDir, const std::string &scanHelper)
{
	auto &im = *impl_;
	if (im.initialised)
		return true;
	// Runs JUCE inside OBS as a guest: JUCE's message handling rides on the
	// host's macOS run loop, the same way plug-ins work inside a DAW.
	juce::initialiseJuce_GUI();
	im.dataDir = dataDir;
	im.helperPath = scanHelper;
	im.dir().createDirectory();
	juce::addDefaultFormatsToManager(im.formats);
	if (auto xml = juce::parseXML(im.listFile()))
		im.known.recreateFromXml(*xml);
	// The old in-process scanner's marker; no longer used.
	im.dir().getChildFile("scan-in-progress.txt").deleteFile();

	// OBS quit (most likely crashed) while a plugin was being created last time:
	// take that plugin off the list so the saved chain doesn't crash OBS again.
	const juce::File loading = im.loadingFile();
	if (loading.existsAsFile()) {
		const juce::String id = loading.loadFileAsString().trim();
		loading.deleteFile();
		if (auto desc = im.known.getTypeForIdentifierString(id)) {
			im.startupWarning = (desc->name + " was being loaded when OBS last closed unexpectedly, "
						     "so it has been skipped. Use \"Rescan all\" to try it again.")
						    .toStdString();
			im.known.addToBlacklist(desc->fileOrIdentifier);
			im.known.removeType(*desc);
			im.saveList();
		}
	}
	im.initialised = true;
	return true;
}

void Engine::shutdown()
{
	if (!impl_->initialised)
		return;
	cancelScan();
	clearChain();
	juce::shutdownJuce_GUI();
	impl_->initialised = false;
}

bool Engine::ready() const
{
	return impl_->initialised;
}

// ---------------------------------------------------------------- plugin list

std::vector<PluginInfo> Engine::plugins() const
{
	std::vector<PluginInfo> out;
	auto types = impl_->known.getTypes();
	std::sort(types.begin(), types.end(), [](const auto &a, const auto &b) {
		if (a.manufacturerName != b.manufacturerName)
			return a.manufacturerName.compareIgnoreCase(b.manufacturerName) < 0;
		return a.name.compareIgnoreCase(b.name) < 0;
	});
	for (const auto &t : types) {
		if (t.isInstrument)
			continue; // the bus processes audio; skip synths
		out.push_back(PluginInfo{t.createIdentifierString().toStdString(), t.name.toStdString(),
					 t.manufacturerName.toStdString(), t.pluginFormatName.toStdString()});
	}
	return out;
}

void Engine::startScan(bool rescanAll)
{
	auto &im = *impl_;
	cancelScan();
	if (rescanAll) {
		im.known.clear();
		im.known.clearBlacklistedFiles();
	}
	const juce::StringArray blacklist = im.known.getBlacklistedFiles();
	for (int f = 0; f < im.formats.getNumFormats(); ++f) {
		auto *format = im.formats.getFormat(f);
		// Lists files (VST3) or registered components (AU) without loading them.
		const juce::StringArray files =
			format->searchPathsForPlugins(format->getDefaultLocationsToSearch(), true, true);
		for (const auto &file : files) {
			if (blacklist.contains(file))
				continue;
			// The bus only processes audio: skip instruments and MIDI tools.
			if (file.startsWith("AudioUnit:Synths/") || file.startsWith("AudioUnit:MidiEffects/") ||
			    file.startsWith("AudioUnit:Generators/"))
				continue;
			if (!rescanAll && im.known.isListingUpToDate(file, *format))
				continue;
			im.queue.push_back({f, file});
		}
	}
	im.queueTotal = im.queue.size();
	im.scanActive = true;
}

bool Engine::scanStep(std::string &current, float &progress)
{
	auto &im = *impl_;
	if (!im.scanActive) {
		progress = 1.0f;
		current.clear();
		return false;
	}

	if (im.child) {
		const bool running = im.child->isRunning();
		const bool timedOut = running && juce::Time::getMillisecondCounter() - im.childStarted > kScanTimeoutMs;
		if (running && !timedOut) {
			current = im.childName;
			progress = im.queueTotal ? static_cast<float>(im.queueTotal - im.queue.size() - 1) /
							   static_cast<float>(im.queueTotal)
						 : 1.0f;
			return true;
		}
		im.killChild();
		im.collect(timedOut);
	}

	while (!im.queue.empty()) {
		const Impl::ScanJob job = im.queue.front();
		im.queue.pop_front();
		if (im.launch(job)) {
			current = im.childName;
			progress = static_cast<float>(im.queueTotal - im.queue.size() - 1) /
				   static_cast<float>(std::max<size_t>(1, im.queueTotal));
			return true;
		}
		// The helper is missing or won't start: never fall back to scanning
		// inside OBS, just stop.
		im.queue.clear();
		im.startupWarning = "The plugin scanner (obsviewer-scan) couldn't be started. Reinstall OBSViewer.";
	}

	im.scanActive = false;
	im.saveList();
	++im.revision;
	progress = 1.0f;
	current.clear();
	return false;
}

bool Engine::scanning() const
{
	return impl_->scanActive;
}

void Engine::cancelScan()
{
	auto &im = *impl_;
	im.killChild();
	im.queue.clear();
	if (im.scanActive)
		im.saveList();
	im.scanActive = false;
}

std::vector<std::string> Engine::failedPlugins() const
{
	std::vector<std::string> out;
	for (const auto &f : impl_->known.getBlacklistedFiles())
		out.push_back(impl_->shortName(f).toStdString());
	std::sort(out.begin(), out.end());
	return out;
}

std::string Engine::takeStartupWarning()
{
	std::string w;
	w.swap(impl_->startupWarning);
	return w;
}

// ---------------------------------------------------------------- chain

void Engine::addPlugin(const std::string &id, int insertAt, AddDone done, const std::string &state, bool bypass,
		       float inDb, float outDb)
{
	auto &im = *impl_;
	auto desc = im.known.getTypeForIdentifierString(juce::String::fromUTF8(id.c_str()));
	if (!desc) {
		if (done)
			done(false, "That plugin isn't in the scanned list any more. Scan for plugins again.", -1);
		return;
	}
	// If the plugin takes OBS down while it's being created, the next start finds
	// this marker and skips the plugin (see init).
	im.loadingFile().replaceWithText(desc->createIdentifierString());
	const juce::PluginDescription description = *desc;
	const uint64_t generation = im.generation;

	// Asynchronous creation: required for out-of-process Audio Units (AUv3), which
	// need OBS's run loop free while they start. Others complete just the same.
	im.formats.createPluginInstanceAsync(
		description, im.sampleRate, im.maxBlock,
		[this, description, generation, insertAt, done, state, bypass, inDb,
		 outDb](std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String &err) {
			auto &im = *impl_;
			im.loadingFile().deleteFile();
			if (generation != im.generation) // chain cleared or replaced meanwhile
				return;
			if (!instance) {
				if (done)
					done(false,
					     err.isNotEmpty() ? err.toStdString() : std::string("The plugin couldn't be loaded."),
					     -1);
				return;
			}
			auto slot = std::make_shared<Slot>();
			slot->desc = description;
			im.configure(*instance, *slot);
			if (!state.empty()) {
				juce::MemoryBlock block;
				if (block.fromBase64Encoding(juce::String::fromUTF8(state.c_str())) && block.getSize() > 0) {
					im.loadingFile().replaceWithText(description.createIdentifierString());
					instance->setStateInformation(block.getData(), static_cast<int>(block.getSize()));
					im.loadingFile().deleteFile();
				}
			}
			slot->bypass = bypass;
			slot->inDb = std::clamp(inDb, -24.0f, 24.0f);
			slot->outDb = std::clamp(outDb, -24.0f, 24.0f);
			slot->inGain = fromDb(slot->inDb);
			slot->outGain = fromDb(slot->outDb);
			slot->plugin = std::move(instance);

			int index;
			{
				std::lock_guard<std::mutex> lock(im.chainLock);
				if (insertAt < 0 || static_cast<size_t>(insertAt) > im.chain.size()) {
					im.chain.push_back(std::move(slot));
					index = static_cast<int>(im.chain.size()) - 1;
				} else {
					im.chain.insert(im.chain.begin() + insertAt, std::move(slot));
					index = insertAt;
				}
			}
			++im.revision;
			if (done)
				done(true, std::string(), index);
		});
}

void Engine::removeSlot(int index)
{
	std::shared_ptr<Slot> gone;
	{
		std::lock_guard<std::mutex> lock(impl_->chainLock);
		if (index < 0 || static_cast<size_t>(index) >= impl_->chain.size())
			return;
		gone = impl_->chain[static_cast<size_t>(index)];
		impl_->chain.erase(impl_->chain.begin() + index);
	}
	++impl_->revision;
	gone.reset(); // plugin destroyed here, on the main thread
}

void Engine::moveSlot(int from, int to)
{
	std::lock_guard<std::mutex> lock(impl_->chainLock);
	auto &c = impl_->chain;
	const int n = static_cast<int>(c.size());
	if (from < 0 || from >= n || to < 0 || to >= n || from == to)
		return;
	auto s = c[static_cast<size_t>(from)];
	c.erase(c.begin() + from);
	c.insert(c.begin() + to, s);
	++impl_->revision;
}

void Engine::setBypass(int index, bool bypass)
{
	if (auto s = impl_->at(index)) {
		s->bypass = bypass;
		++impl_->revision;
	}
}

void Engine::setSlotGains(int index, float inDb, float outDb)
{
	if (auto s = impl_->at(index)) {
		s->inDb = std::clamp(inDb, -24.0f, 24.0f);
		s->outDb = std::clamp(outDb, -24.0f, 24.0f);
		s->inGain = fromDb(s->inDb);
		s->outGain = fromDb(s->outDb);
	}
}

void Engine::openEditor(int index)
{
	auto s = impl_->at(index);
	if (!s || !s->plugin)
		return;
	if (s->window) {
		s->window->setVisible(true);
		s->window->toFront(true);
		return;
	}
	juce::AudioProcessorEditor *editor = s->plugin->hasEditor() ? s->plugin->createEditorAndMakeActive() : nullptr;
	if (!editor)
		editor = new juce::GenericAudioProcessorEditor(*s->plugin);
	s->window = std::make_unique<EditorWindow>(s->desc.name + "  ·  OBSViewer Master Bus", editor);
}

int Engine::slotCount() const
{
	return static_cast<int>(impl_->chain.size());
}

SlotInfo Engine::slot(int index) const
{
	SlotInfo info;
	if (auto s = impl_->at(index)) {
		info.name = s->desc.name.toStdString();
		info.vendor = s->desc.manufacturerName.toStdString();
		info.format = s->desc.pluginFormatName.toStdString();
		info.bypass = s->bypass.load();
		info.inGainDb = s->inDb;
		info.outGainDb = s->outDb;
		info.latency = s->plugin ? s->plugin->getLatencySamples() : 0;
	}
	return info;
}

Levels Engine::slotLevels(int index) const
{
	Levels l;
	if (auto s = impl_->at(index)) {
		for (int c = 0; c < 2; ++c) {
			l.pre[c] = s->pre.db(c);
			l.post[c] = s->post.db(c);
		}
		const float pre = std::max(l.pre[0], l.pre[1]);
		const float post = std::max(l.post[0], l.post[1]) - s->outDb;
		l.reduction = pre > -60.0f ? std::max(0.0f, (pre + s->inDb) - post) : 0.0f;
	}
	return l;
}

void Engine::setMasterGains(float inDb, float outDb)
{
	impl_->masterInDb = std::clamp(inDb, -24.0f, 24.0f);
	impl_->masterOutDb = std::clamp(outDb, -60.0f, 24.0f); // the output fader goes down to −60
	impl_->masterIn = fromDb(impl_->masterInDb);
	impl_->masterOut = fromDb(impl_->masterOutDb);
}

float Engine::masterInDb() const
{
	return impl_->masterInDb;
}

float Engine::masterOutDb() const
{
	return impl_->masterOutDb;
}

Levels Engine::masterLevels() const
{
	Levels l;
	for (int c = 0; c < 2; ++c) {
		l.pre[c] = impl_->masterPre.db(c);
		l.post[c] = impl_->masterPost.db(c);
	}
	return l;
}

int Engine::latencySamples() const
{
	int total = 0;
	for (const auto &s : impl_->chain)
		if (s->plugin)
			total += s->plugin->getLatencySamples();
	return total;
}

double Engine::sampleRate() const
{
	return impl_->sampleRate;
}

void Engine::clearChain()
{
	++impl_->generation; // drop plugins still on their way in
	++impl_->revision;
	impl_->loadingChain = false;
	impl_->pendingChain.clear();
	std::vector<std::shared_ptr<Slot>> gone;
	{
		std::lock_guard<std::mutex> lock(impl_->chainLock);
		gone.swap(impl_->chain);
	}
	gone.clear();
}

std::string Engine::saveChain() const
{
	if (impl_->loadingChain)
		return impl_->pendingChain; // never save a half-restored chain
	juce::ValueTree root("OBSViewerMasterBus");
	root.setProperty("version", 1, nullptr);
	root.setProperty("masterIn", impl_->masterInDb, nullptr);
	root.setProperty("masterOut", impl_->masterOutDb, nullptr);
	for (const auto &s : impl_->chain) {
		juce::ValueTree v("Slot");
		v.setProperty("id", s->desc.createIdentifierString(), nullptr);
		v.setProperty("name", s->desc.name, nullptr);
		v.setProperty("bypass", s->bypass.load(), nullptr);
		v.setProperty("in", s->inDb, nullptr);
		v.setProperty("out", s->outDb, nullptr);
		if (s->plugin) {
			juce::MemoryBlock state;
			s->plugin->getStateInformation(state);
			v.setProperty("state", state.toBase64Encoding(), nullptr);
		}
		root.appendChild(v, nullptr);
	}
	return root.toXmlString().toStdString();
}

void Engine::loadChain(const std::string &text, std::function<void(const std::string &errors)> done)
{
	auto &im = *impl_;
	clearChain();
	if (text.empty()) {
		if (done)
			done({});
		return;
	}
	const juce::ValueTree root = juce::ValueTree::fromXml(juce::String::fromUTF8(text.c_str()));
	if (!root.isValid()) {
		if (done)
			done("The saved chain couldn't be read.");
		return;
	}
	setMasterGains(static_cast<float>(root.getProperty("masterIn", 0.0)),
		       static_cast<float>(root.getProperty("masterOut", 0.0)));

	// While plugins arrive one by one, saving keeps the full saved chain.
	im.loadingChain = true;
	im.pendingChain = text;

	struct Job {
		std::vector<juce::ValueTree> slots;
		size_t next = 0;
		std::string errors;
		std::function<void(const std::string &)> done;
	};
	auto job = std::make_shared<Job>();
	for (int i = 0; i < root.getNumChildren(); ++i)
		job->slots.push_back(root.getChild(i));
	job->done = std::move(done);
	const uint64_t generation = im.generation;

	// One plugin per turn of OBS's event loop, so OBS stays responsive.
	auto step = std::make_shared<std::function<void()>>();
	*step = [this, job, generation, step] {
		auto &im = *impl_;
		if (generation != im.generation)
			return;
		if (job->next >= job->slots.size()) {
			im.loadingChain = false;
			im.pendingChain.clear();
			if (job->done)
				job->done(job->errors);
			*step = nullptr; // break the self-reference
			return;
		}
		const juce::ValueTree v = job->slots[job->next++];
		addPlugin(v.getProperty("id").toString().toStdString(), -1,
			  [job, step, v](bool ok, const std::string &error, int) {
				  if (!ok)
					  job->errors += v.getProperty("name").toString().toStdString() + ": " + error + "\n";
				  juce::MessageManager::callAsync([step] {
					  if (*step)
						  (*step)();
				  });
			  },
			  v.getProperty("state").toString().toStdString(), static_cast<bool>(v.getProperty("bypass", false)),
			  static_cast<float>(v.getProperty("in", 0.0)), static_cast<float>(v.getProperty("out", 0.0)));
	};
	juce::MessageManager::callAsync([step] {
		if (*step)
			(*step)();
	});
}

bool Engine::loadingChain() const
{
	return impl_->loadingChain;
}

uint64_t Engine::revision() const
{
	return impl_->revision;
}

float Engine::totalReduction() const
{
	float total = 0.0f;
	for (size_t i = 0; i < impl_->chain.size(); ++i)
		if (!impl_->chain[i]->bypass.load())
			total += slotLevels(static_cast<int>(i)).reduction;
	return total;
}

// ---------------------------------------------------------------- audio

void Engine::prepare(double sampleRate, int maxBlock)
{
	auto &im = *impl_;
	std::lock_guard<std::mutex> lock(im.chainLock);
	if (std::fabs(sampleRate - im.sampleRate) < 0.5 && maxBlock <= im.maxBlock)
		return;
	im.sampleRate = sampleRate;
	im.maxBlock = std::max(maxBlock, im.maxBlock);
	im.wideBuffer.setSize(kMaxChannels, im.maxBlock, false, true, false);
	for (auto &s : im.chain)
		if (s->plugin) {
			s->plugin->releaseResources();
			im.configure(*s->plugin, *s);
		}
}

void Engine::process(float *left, float *right, int frames)
{
	auto &im = *impl_;
	float *io[2] = {left, right};

	applyGain(io, frames, im.masterIn.load(std::memory_order_relaxed));
	im.masterPre.feed(io, frames, im.sampleRate);

	// Blocks only while the main thread is adding, removing or moving a plugin
	// (a few vector operations; plugins are created and prepared outside the lock).
	std::unique_lock<std::mutex> lock(im.chainLock);
	{
		for (int offset = 0; offset < frames; offset += im.maxBlock) {
			const int n = std::min(im.maxBlock, frames - offset);
			float *ch[2] = {left + offset, right + offset};
			for (auto &s : im.chain) {
				if (!s->plugin)
					continue;
				applyGain(ch, n, s->inGain.load(std::memory_order_relaxed));
				s->pre.feed(ch, n, im.sampleRate);

				if (!s->wide) {
					juce::AudioBuffer<float> buffer(ch, 2, n);
					if (s->bypass.load(std::memory_order_relaxed))
						s->plugin->processBlockBypassed(buffer, im.midi);
					else
						s->plugin->processBlock(buffer, im.midi);
				} else {
					// Plugins with other channel counts get a wider scratch buffer.
					const int pc = std::min(kMaxChannels,
								std::max({s->plugin->getTotalNumInputChannels(),
									  s->plugin->getTotalNumOutputChannels(), 2}));
					juce::AudioBuffer<float> buffer(im.wideBuffer.getArrayOfWritePointers(), pc, n);
					buffer.clear();
					buffer.copyFrom(0, 0, ch[0], n);
					buffer.copyFrom(1, 0, ch[1], n);
					if (s->bypass.load(std::memory_order_relaxed))
						s->plugin->processBlockBypassed(buffer, im.midi);
					else
						s->plugin->processBlock(buffer, im.midi);
					juce::FloatVectorOperations::copy(ch[0], buffer.getReadPointer(0), n);
					juce::FloatVectorOperations::copy(ch[1], buffer.getReadPointer(1), n);
				}
				im.midi.clear();

				applyGain(ch, n, s->outGain.load(std::memory_order_relaxed));
				s->post.feed(ch, n, im.sampleRate);
			}
		}
	}

	applyGain(io, frames, im.masterOut.load(std::memory_order_relaxed));
	im.masterPost.feed(io, frames, im.sampleRate);
}

// ---------------------------------------------------------------- scan helper

int runScanHelper(int argc, char **argv)
{
	if (argc < 4)
		return 2;
	const juce::String formatName = juce::String::fromUTF8(argv[1]);
	const juce::String identifier = juce::String::fromUTF8(argv[2]);
	const juce::File resultFile(juce::String::fromUTF8(argv[3]));

	juce::ScopedJuceInitialiser_GUI gui;
#if JUCE_MAC
	juce::Process::setDockIconVisible(false);
#endif
	juce::AudioPluginFormatManager formats;
	juce::addDefaultFormatsToManager(formats);
	juce::AudioPluginFormat *format = nullptr;
	for (auto *f : formats.getFormats())
		if (f->getName() == formatName)
			format = f;
	if (format == nullptr)
		return 3;

	juce::OwnedArray<juce::PluginDescription> results;
	juce::PluginDescription probe;
	probe.fileOrIdentifier = identifier;
	if (format->requiresUnblockedMessageThreadDuringCreation(probe)) {
		// Out-of-process Audio Units need the message loop running while they load.
		std::thread worker([&] {
			format->findAllTypesForFile(results, identifier);
			juce::MessageManager::getInstance()->stopDispatchLoop();
		});
		juce::MessageManager::getInstance()->runDispatchLoop();
		worker.join();
	} else {
		format->findAllTypesForFile(results, identifier);
	}

	if (results.isEmpty())
		return 4;
	juce::XmlElement list("LIST");
	for (auto *d : results)
		list.addChildElement(d->createXml().release());
	const juce::File temp = resultFile.getSiblingFile(resultFile.getFileName() + ".tmp");
	if (!list.writeTo(temp) || !temp.moveFileTo(resultFile))
		return 5;
	return 0;
}

} // namespace obsviewer::bus
