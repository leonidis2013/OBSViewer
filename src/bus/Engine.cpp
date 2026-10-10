// JUCE side of the master bus: hosts VST3 / Audio Unit plugins in a serial chain.
#include "bus/Engine.hpp"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>

namespace obsviewer::bus {

namespace {

constexpr int kMaxChannels = 16;

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

	mutable std::mutex chainLock; // audio thread only try_locks
	std::vector<std::shared_ptr<Slot>> chain;

	double sampleRate = 48000.0;
	int maxBlock = 1024;
	juce::AudioBuffer<float> wideBuffer{kMaxChannels, 1024};
	juce::MidiBuffer midi;

	std::atomic<float> masterIn{1.0f}, masterOut{1.0f};
	float masterInDb = 0.0f, masterOutDb = 0.0f;
	PeakMeter masterPre, masterPost;

	// scanning
	std::unique_ptr<juce::PluginDirectoryScanner> scanner;
	int scanFormat = 0;
	bool rescanAll = false;

	juce::File dir() const { return juce::File(juce::String::fromUTF8(dataDir.c_str())); }
	juce::File listFile() const { return dir().getChildFile("plugins.xml"); }
	juce::File pedalFile() const { return dir().getChildFile("scan-in-progress.txt"); }

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

	std::shared_ptr<Slot> at(int index) const
	{
		std::lock_guard<std::mutex> lock(chainLock);
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

bool Engine::init(const std::string &dataDir)
{
	if (impl_->initialised)
		return true;
	// Runs JUCE inside OBS as a guest: JUCE's message handling rides on the
	// host's macOS run loop, the same way plug-ins work inside a DAW.
	juce::initialiseJuce_GUI();
	impl_->dataDir = dataDir;
	impl_->dir().createDirectory();
	juce::addDefaultFormatsToManager(impl_->formats);
	if (auto xml = juce::parseXML(impl_->listFile()))
		impl_->known.recreateFromXml(*xml);
	impl_->initialised = true;
	return true;
}

void Engine::shutdown()
{
	if (!impl_->initialised)
		return;
	impl_->scanner.reset();
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
	impl_->scanFormat = 0;
	impl_->rescanAll = rescanAll;
	if (rescanAll)
		impl_->known.clear();
	impl_->scanner.reset();
}

bool Engine::scanStep(std::string &current, float &progress)
{
	auto &im = *impl_;
	const int numFormats = im.formats.getNumFormats();
	while (im.scanFormat < numFormats) {
		if (!im.scanner) {
			auto *format = im.formats.getFormat(im.scanFormat);
			im.scanner = std::make_unique<juce::PluginDirectoryScanner>(
				im.known, *format, format->getDefaultLocationsToSearch(), true, im.pedalFile(), false);
		}
		juce::String name;
		if (im.scanner->scanNextFile(!im.rescanAll, name)) {
			current = name.toStdString();
			progress = (static_cast<float>(im.scanFormat) + im.scanner->getProgress()) /
				   static_cast<float>(std::max(1, numFormats));
			return true;
		}
		im.scanner.reset();
		++im.scanFormat;
	}
	im.saveList();
	progress = 1.0f;
	current.clear();
	return false;
}

bool Engine::scanning() const
{
	return impl_->scanner != nullptr;
}

// ---------------------------------------------------------------- chain

bool Engine::addPlugin(const std::string &id, std::string &error, int insertAt)
{
	auto &im = *impl_;
	auto desc = im.known.getTypeForIdentifierString(juce::String::fromUTF8(id.c_str()));
	if (!desc) {
		error = "That plugin isn't in the scanned list any more. Scan for plugins again.";
		return false;
	}
	juce::String err;
	auto instance = im.formats.createPluginInstance(*desc, im.sampleRate, im.maxBlock, err);
	if (!instance) {
		error = err.isNotEmpty() ? err.toStdString() : std::string("The plugin couldn't be loaded.");
		return false;
	}
	auto slot = std::make_shared<Slot>();
	slot->desc = *desc;
	im.configure(*instance, *slot);
	slot->plugin = std::move(instance);

	std::lock_guard<std::mutex> lock(im.chainLock);
	if (insertAt < 0 || static_cast<size_t>(insertAt) > im.chain.size())
		im.chain.push_back(std::move(slot));
	else
		im.chain.insert(im.chain.begin() + insertAt, std::move(slot));
	return true;
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
}

void Engine::setBypass(int index, bool bypass)
{
	if (auto s = impl_->at(index))
		s->bypass = bypass;
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
	std::lock_guard<std::mutex> lock(impl_->chainLock);
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
	impl_->masterOutDb = std::clamp(outDb, -24.0f, 24.0f);
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
	std::lock_guard<std::mutex> lock(impl_->chainLock);
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
	std::vector<std::shared_ptr<Slot>> gone;
	{
		std::lock_guard<std::mutex> lock(impl_->chainLock);
		gone.swap(impl_->chain);
	}
	gone.clear();
}

std::string Engine::saveChain() const
{
	juce::ValueTree root("OBSViewerMasterBus");
	root.setProperty("version", 1, nullptr);
	root.setProperty("masterIn", impl_->masterInDb, nullptr);
	root.setProperty("masterOut", impl_->masterOutDb, nullptr);
	std::vector<std::shared_ptr<Slot>> chain;
	{
		std::lock_guard<std::mutex> lock(impl_->chainLock);
		chain = impl_->chain;
	}
	for (const auto &s : chain) {
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

bool Engine::loadChain(const std::string &text, std::string &errors)
{
	clearChain();
	if (text.empty())
		return true;
	const juce::ValueTree root = juce::ValueTree::fromXml(juce::String::fromUTF8(text.c_str()));
	if (!root.isValid())
		return false;
	setMasterGains(static_cast<float>(root.getProperty("masterIn", 0.0)),
		       static_cast<float>(root.getProperty("masterOut", 0.0)));
	bool ok = true;
	for (int i = 0; i < root.getNumChildren(); ++i) {
		const auto v = root.getChild(i);
		std::string error;
		const std::string id = v.getProperty("id").toString().toStdString();
		if (!addPlugin(id, error)) {
			errors += v.getProperty("name").toString().toStdString() + ": " + error + "\n";
			ok = false;
			continue;
		}
		const int index = slotCount() - 1;
		if (auto s = impl_->at(index)) {
			juce::MemoryBlock state;
			if (state.fromBase64Encoding(v.getProperty("state").toString()) && state.getSize() > 0)
				s->plugin->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
			s->bypass = static_cast<bool>(v.getProperty("bypass", false));
		}
		setSlotGains(index, static_cast<float>(v.getProperty("in", 0.0)), static_cast<float>(v.getProperty("out", 0.0)));
	}
	return ok;
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

	std::unique_lock<std::mutex> lock(im.chainLock, std::try_to_lock);
	if (lock.owns_lock()) {
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

} // namespace obsviewer::bus
