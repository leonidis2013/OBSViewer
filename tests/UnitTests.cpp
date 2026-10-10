// Host-side unit tests for OBSViewer's pure logic (no OBS or Qt needed).
// Build (from the repo root):
//   g++ -std=c++17 -Isrc tests/UnitTests.cpp src/core/Version.cpp src/viewer/Layouts.cpp
//       src/update/ReleaseInfo.cpp -o unit_tests && ./unit_tests

#include "audio/meters/LoudnessMeter.h"
#include "audio/meters/TruePeakMeter.h"
#include "core/Health.hpp"
#include "core/History.hpp"
#include "core/LevelMath.hpp"
#include "core/Version.hpp"
#include "update/ReleaseInfo.hpp"
#include "viewer/Layouts.hpp"

#include <cmath>
#include <vector>
#include <cstdio>
#include <set>
#include <string>

using namespace obsviewer;

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                                 \
	do {                                                                        \
		++checks;                                                           \
		if (!(cond)) {                                                      \
			++failures;                                                 \
			std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
		}                                                                   \
	} while (0)

static bool near(float a, float b)
{
	return std::fabs(a - b) < 1e-4f;
}

static void testLayouts()
{
	std::set<std::string> ids;
	for (const Layout &l : allLayouts()) {
		CHECK(!l.id.empty());
		CHECK(!l.name.empty());
		CHECK(ids.insert(l.id).second); // unique ids
		CHECK(!l.tiles.empty());

		std::set<int> sceneIndices;
		for (size_t i = 0; i < l.tiles.size(); ++i) {
			const Tile &t = l.tiles[i];
			// inside the canvas
			CHECK(t.rect.x >= -1e-4f && t.rect.y >= -1e-4f);
			CHECK(t.rect.x + t.rect.w <= 1.0f + 1e-4f);
			CHECK(t.rect.y + t.rect.h <= 1.0f + 1e-4f);
			CHECK(t.rect.w > 0 && t.rect.h > 0);
			// picture tiles keep the canvas aspect ratio (w == h in normalised units)
			if (t.kind != TileKind::Status)
				CHECK(near(t.rect.w, t.rect.h));
			if (t.kind == TileKind::Scene) {
				CHECK(t.sceneIndex >= 0);
				CHECK(sceneIndices.insert(t.sceneIndex).second); // each scene once
			}
			// no overlaps
			for (size_t j = i + 1; j < l.tiles.size(); ++j) {
				const NRect &a = t.rect, &b = l.tiles[j].rect;
				const bool overlap = a.x < b.x + b.w - 1e-4f && b.x < a.x + a.w - 1e-4f &&
						     a.y < b.y + b.h - 1e-4f && b.y < a.y + a.h - 1e-4f;
				CHECK(!overlap);
			}
		}
		// scene indices are 0..n-1
		int expected = 0;
		for (int idx : sceneIndices)
			CHECK(idx == expected++);
	}

	CHECK(layoutById("atem").id == "atem");
	CHECK(layoutById("does-not-exist").id == kDefaultLayoutId);
	CHECK(layoutById("atem").sceneTileCount() == 4);
	CHECK(layoutById("classic8").sceneTileCount() == 8);
	CHECK(layoutById("bigprogram").sceneTileCount() == 6);
	CHECK(layoutById("grid3").sceneTileCount() == 9);
	CHECK(layoutById("grid4").sceneTileCount() == 16);
}

static void checkLayoutGeometry(const Layout &l)
{
	for (size_t i = 0; i < l.tiles.size(); ++i) {
		const NRect &a = l.tiles[i].rect;
		CHECK(a.x >= -1e-4f && a.y >= -1e-4f && a.x + a.w <= 1.0f + 1e-4f && a.y + a.h <= 1.0f + 1e-4f);
		for (size_t j = i + 1; j < l.tiles.size(); ++j) {
			const NRect &b = l.tiles[j].rect;
			const bool overlap = a.x < b.x + b.w - 1e-4f && b.x < a.x + a.w - 1e-4f && a.y < b.y + b.h - 1e-4f &&
					     b.y < a.y + a.h - 1e-4f;
			CHECK(!overlap);
		}
	}
}

static void testAutoLayout()
{
	CHECK(makeAutoLayout(0).tiles.size() == 2);
	CHECK(makeAutoLayout(0).sceneTileCount() == 0);
	for (int n : {1, 3, 4, 5, 8, 9, 10, 16, 17, 30, 50, 72}) {
		const Layout l = makeAutoLayout(n);
		CHECK(l.sceneTileCount() == n);
		checkLayoutGeometry(l);
		// scene tiles are never bigger than a quarter of the width
		for (const auto &t : l.tiles)
			if (t.kind == TileKind::Scene)
				CHECK(t.rect.w <= 0.25f + 1e-4f);
	}
	// 4 scenes: one row of quarter-width tiles, like ATEM
	{
		const Layout l = makeAutoLayout(4);
		CHECK(near(l.bounds().h, 0.75f));
	}
	// 10 scenes: 5 per row, two rows
	{
		const Layout l = makeAutoLayout(10);
		CHECK(near(l.tiles[2].rect.w, 0.2f));
		CHECK(near(l.bounds().h, 0.9f));
	}
	// 2 scenes: centred
	{
		const Layout l = makeAutoLayout(2);
		CHECK(near(l.tiles[2].rect.x, 0.25f) && near(l.tiles[3].rect.x, 0.5f));
	}
	// more than fits: capped, never overflows
	CHECK(makeAutoLayout(500).sceneTileCount() == 72);
	checkLayoutGeometry(makeAutoLayout(500));

	CHECK(resolveLayout(layoutById("auto"), 9).sceneTileCount() == 9);
	CHECK(resolveLayout(layoutById("atem"), 9).sceneTileCount() == 4);
	CHECK(layoutById("nope").id == "atem");
}

static void testVersions()
{
	const Version a = Version::parse("v0.1.0");
	CHECK(a.valid && a.major == 0 && a.minor == 1 && a.patch == 0 && !a.isPrerelease());
	CHECK(Version::parse("1.2").valid && Version::parse("1.2").patch == 0);
	CHECK(!Version::parse("latest").valid);
	CHECK(!Version::parse("").valid);
	CHECK(!Version::parse("1.2.3.4").valid);
	CHECK(!Version::parse("1.x.3").valid);
	CHECK(!Version::parse("1.2.3-").valid);
	CHECK(Version::parse("1.2.3+build.5") == Version::parse("1.2.3"));

	CHECK(Version::parse("0.1.1") > Version::parse("0.1.0"));
	CHECK(Version::parse("0.2.0") > Version::parse("0.1.9"));
	CHECK(Version::parse("1.0.0") > Version::parse("0.99.99"));
	CHECK(Version::parse("0.2.0") > Version::parse("0.2.0-rc.1"));
	CHECK(Version::parse("0.2.0-rc.1") > Version::parse("0.2.0-beta.2"));
	CHECK(Version::parse("0.2.0-beta.10") > Version::parse("0.2.0-beta.2"));
	CHECK(Version::parse("0.2.0-beta.2") > Version::parse("0.2.0-beta"));
	CHECK(Version::parse("0.2.0-beta") > Version::parse("0.2.0-1"));
	CHECK(Version::parse("V3.4.5").toString() == "3.4.5");
	CHECK(Version::parse("1.0.0-beta.2").toString() == "1.0.0-beta.2");
}

static Release makeRelease(const std::string &tag, bool pre = false, bool draft = false, bool pkg = true)
{
	Release r;
	r.tag = tag;
	r.version = Version::parse(tag);
	r.prerelease = pre;
	r.draft = draft;
	if (pkg)
		r.assets.push_back(ReleaseAsset{"OBSViewer-" + r.version.toString() + "-macos-universal.pkg", "", "",
						"", 1000});
	r.assets.push_back(ReleaseAsset{"SHA256SUMS.txt", "", "", "", 100});
	return r;
}

static void testSelection()
{
	const Version current = Version::parse("0.1.0");
	std::vector<Release> rs = {
		makeRelease("latest", true),         // rolling build: not semver
		makeRelease("v0.1.0"),               // same as current
		makeRelease("v0.2.0"),               // candidate
		makeRelease("v0.3.0-beta.1", true),  // pre-release
		makeRelease("v0.4.0", false, true),  // draft
		makeRelease("v0.2.5", false, false, false), // no installer
	};

	int i = selectUpdate(rs, current, false, "", false);
	CHECK(i == 2);
	i = selectUpdate(rs, current, true, "", false);
	CHECK(i == 3);
	// skipped version is ignored on automatic checks but offered on manual ones
	CHECK(selectUpdate(rs, current, false, "0.2.0", false) == -1);
	CHECK(selectUpdate(rs, current, false, "0.2.0", true) == 2);
	// nothing newer
	CHECK(selectUpdate(rs, Version::parse("0.2.0"), false, "", false) == -1);

	CHECK(rs[2].installer() && rs[2].installer()->name == "OBSViewer-0.2.0-macos-universal.pkg");
	CHECK(rs[2].checksums() != nullptr);
	CHECK(rs[5].installer() == nullptr);
}

static void testChecksums()
{
	const std::string h1(64, 'a');
	const std::string h2 = "ABCDEF0123456789abcdef0123456789ABCDEF0123456789abcdef0123456789";
	const std::string text = h1 + "  OBSViewer-0.2.0-macos-universal.pkg\n" + h2 + " *other.zip\n" +
				 "    notes.txt: " + std::string(64, 'b') + "\n";

	CHECK(findChecksum(text, "OBSViewer-0.2.0-macos-universal.pkg") == h1);
	CHECK(findChecksum(text, "other.zip") == "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789");
	CHECK(findChecksum(text, "notes.txt") == std::string(64, 'b'));
	CHECK(findChecksum(text, "missing.pkg").empty());
	CHECK(findChecksum("short  x.pkg", "x.pkg").empty());

	CHECK(digestToSha256("sha256:" + h2) == "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789");
	CHECK(digestToSha256("sha512:" + h1).empty());
	CHECK(digestToSha256("").empty());
	CHECK(digestToSha256("sha256:xyz").empty());
}


static void testHistory()
{
	History<int> h(5);
	CHECK(h.empty() && h.capacity() == 5);
	for (int i = 1; i <= 3; ++i)
		h.push(i);
	CHECK(h.size() == 3 && h.at(0) == 1 && h.latest() == 3);
	for (int i = 4; i <= 8; ++i)
		h.push(i);
	CHECK(h.size() == 5);
	CHECK(h.at(0) == 4 && h.at(4) == 8 && h.latest() == 8);
	const auto v = h.toVector();
	CHECK(v.size() == 5 && v.front() == 4 && v.back() == 8);
	h.clear();
	CHECK(h.empty());
}

static HealthInputs healthyLive()
{
	HealthInputs in;
	in.streaming = true;
	in.cpuPct = 30.0;
	in.diskFreeGB = 200.0;
	in.audioActive = true;
	in.shortTermLufs = -14.2f;
	in.integratedLufs = -14.4f;
	in.truePeakRecentDb = -3.0f;
	return in;
}

static void testHealth()
{
	// Idle OBS with nothing configured: nothing is a problem.
	{
		HealthInputs in;
		const HealthReport r = evaluateHealth(in);
		CHECK(r.overall <= Level::Good);
		CHECK(r.levelOf("Stream") == Level::Off);
		CHECK(r.levelOf("Network") == Level::Off);
	}
	// Healthy live stream: all good.
	{
		const HealthReport r = evaluateHealth(healthyLive());
		CHECK(r.overall == Level::Good);
		CHECK(r.levelOf("Loudness") == Level::Good);
		CHECK(r.checks.size() >= 9);
	}
	// Reconnecting is a problem; a past reconnect is a warning.
	{
		HealthInputs in = healthyLive();
		in.reconnecting = true;
		CHECK(evaluateHealth(in).levelOf("Stream") == Level::Bad);
		in.reconnecting = false;
		in.reconnects = 1;
		CHECK(evaluateHealth(in).levelOf("Stream") == Level::Warn);
	}
	// Network drops.
	{
		HealthInputs in = healthyLive();
		in.networkDropsRecent = 3;
		CHECK(evaluateHealth(in).levelOf("Network") == Level::Warn);
		in.networkDropsRecent = 40;
		CHECK(evaluateHealth(in).levelOf("Network") == Level::Bad);
		in.networkDropsRecent = 0;
		in.networkDropPct = 2.5;
		CHECK(evaluateHealth(in).levelOf("Network") == Level::Bad);
	}
	// Encoder, rendering and CPU thresholds.
	{
		HealthInputs in = healthyLive();
		in.encoderSkipPct = 1.0;
		in.renderLagPct = 6.0;
		in.cpuPct = 80.0;
		const HealthReport r = evaluateHealth(in);
		CHECK(r.levelOf("Encoder") == Level::Warn);
		CHECK(r.levelOf("Rendering") == Level::Bad);
		CHECK(r.levelOf("CPU") == Level::Warn);
		CHECK(r.overall == Level::Bad);
	}
	// Disk space while recording.
	{
		HealthInputs in = healthyLive();
		in.recording = true;
		in.diskFreeGB = 100.0;
		in.recordHoursLeft = 2.0;
		CHECK(evaluateHealth(in).levelOf("Disk") == Level::Warn);
		in.recordHoursLeft = 0.5;
		CHECK(evaluateHealth(in).levelOf("Disk") == Level::Bad);
		in.recording = false;
		in.diskFreeGB = 3.0;
		CHECK(evaluateHealth(in).levelOf("Disk") == Level::Bad);
	}
	// Loudness against the target, peaks and silence.
	{
		HealthInputs in = healthyLive();
		in.integratedLufs = -18.0f;
		CHECK(evaluateHealth(in).levelOf("Loudness") == Level::Warn);
		in.integratedLufs = -9.0f;
		CHECK(evaluateHealth(in).levelOf("Loudness") == Level::Bad);
		in.integratedLufs = -120.0f; // not measured yet: fall back to short-term
		in.shortTermLufs = -14.0f;
		CHECK(evaluateHealth(in).levelOf("Loudness") == Level::Good);

		in.truePeakRecentDb = -0.5f;
		CHECK(evaluateHealth(in).levelOf("Peaks") == Level::Warn);
		in.truePeakRecentDb = 0.4f;
		CHECK(evaluateHealth(in).levelOf("Peaks") == Level::Bad);

		in.silentSeconds = 12.0;
		CHECK(evaluateHealth(in).levelOf("Silence") == Level::Bad);
		in.streaming = false;
		CHECK(evaluateHealth(in).levelOf("Silence") == Level::Warn);
	}
	// No audio at all: audio checks are off, not alarms.
	{
		HealthInputs in = healthyLive();
		in.audioActive = false;
		const HealthReport r = evaluateHealth(in);
		CHECK(r.levelOf("Loudness") == Level::Off);
		CHECK(r.levelOf("Peaks") == Level::Off);
		CHECK(r.levelOf("Silence") == Level::Off);
	}
}

// Same reference as BroadcastMaster: a 997 Hz stereo sine at -23 dBFS reads -23 LUFS.
static void testLoudness()
{
	const double fs = 48000.0;
	const double kTwoPi = 6.283185307179586;
	bm::LoudnessMeter meter;
	bm::TruePeakMeter tp;
	meter.prepare(fs);
	tp.prepare(fs);
	const double a = std::pow(10.0, -23.0 / 20.0);
	std::vector<float> l(480), r(480);
	long long t = 0;
	for (int block = 0; block < 100 * 20; ++block) { // 20 s
		for (size_t i = 0; i < l.size(); ++i, ++t)
			l[i] = r[i] = static_cast<float>(a * std::sin(kTwoPi * 997.0 * static_cast<double>(t) / fs));
		meter.process(l.data(), r.data(), static_cast<int>(l.size()));
		tp.process(l.data(), r.data(), static_cast<int>(l.size()));
	}
	CHECK(std::fabs(meter.shortTerm() - (-23.0f)) < 0.15f);
	CHECK(std::fabs(meter.integrated() - (-23.0f)) < 0.15f);
	CHECK(std::fabs(tp.holdDb() - (-23.0f)) < 0.3f);
}

static void testLevelMath()
{
	using namespace obsviewer::levels;
	CHECK(clean(-INFINITY) == kFloorDb);
	CHECK(clean(NAN) == kFloorDb);
	CHECK(clean(-200.0f) == kFloorDb);
	CHECK(clean(-6.0f) == -6.0f);

	// instant rise, 20 dB/s fall
	CHECK(ballistic(-40.0f, -10.0f, 0.05) == -10.0f);
	CHECK(std::fabs(ballistic(-10.0f, -60.0f, 0.5) - (-20.0f)) < 1e-4f);
	CHECK(ballistic(-110.0f, kFloorDb, 5.0) == kFloorDb);

	CHECK(meterPos(0.0f) == 1.0f);
	CHECK(meterPos(-60.0f) == 0.0f);
	CHECK(std::fabs(meterPos(-30.0f) - 0.5f) < 1e-4f);
	CHECK(meterPos(6.0f) == 1.0f);

	// combine: loudest unmuted source per channel
	{
		SourceReading a, b, m;
		a.peak[0] = -12.0f; a.peak[1] = -20.0f; a.rms[0] = -18.0f; a.rms[1] = -26.0f;
		b.peak[0] = -30.0f; b.peak[1] = -6.0f;  b.rms[0] = -36.0f; b.rms[1] = -12.0f;
		m.peak[0] = m.peak[1] = 0.0f; m.muted = true; m.clipped = true;
		const TileLevels t = combine({a, b, m});
		CHECK(t.hasAudio && !t.allMuted && !t.clipped);
		CHECK(t.peak[0] == -12.0f && t.peak[1] == -6.0f);
		CHECK(t.rms[0] == -18.0f && t.rms[1] == -12.0f);
	}
	{
		SourceReading m1, m2;
		m1.muted = m2.muted = true;
		const TileLevels t = combine({m1, m2});
		CHECK(t.hasAudio && t.allMuted && t.peak[0] == kFloorDb);
	}
	{
		const TileLevels t = combine({});
		CHECK(!t.hasAudio && !t.allMuted);
	}
	{
		SourceReading c;
		c.clipped = true;
		CHECK(combine({c}).clipped);
	}
}

static void testPerformance()
{
	PerfInputs in;
	in.fpsTarget = 30.0;
	in.fpsActive = 30.0;
	in.renderMs = 4.0;
	in.cpuPct = 20.0;
	in.memoryMB = 900.0;
	HealthReport r = evaluatePerformance(in);
	CHECK(r.overall == Level::Good);
	CHECK(r.levelOf("Frame rate") == Level::Good);
	CHECK(r.levelOf("Encoding") == Level::Off);
	CHECK(r.levelOf("Stream encoder") == Level::Off);

	in.fpsActive = 28.0; // 93%
	CHECK(evaluatePerformance(in).levelOf("Frame rate") == Level::Warn);
	in.fpsActive = 20.0;
	CHECK(evaluatePerformance(in).levelOf("Frame rate") == Level::Bad);
	in.fpsActive = 30.0;

	in.renderMs = 25.0; // 75% of 33.3 ms
	CHECK(evaluatePerformance(in).levelOf("Render time") == Level::Warn);
	in.renderMs = 32.0;
	CHECK(evaluatePerformance(in).levelOf("Render time") == Level::Bad);
	in.renderMs = 4.0;

	in.encoding = true;
	in.encoderSkipPct = 6.0;
	in.streamEncoder = "Apple VT H264 Hardware · 1920×1080 · 6000 kbps";
	r = evaluatePerformance(in);
	CHECK(r.levelOf("Encoding") == Level::Bad);
	CHECK(r.levelOf("Stream encoder") == Level::Good);
	CHECK(r.overall == Level::Bad);
}

int main()
{
	testLayouts();
	testAutoLayout();
	testVersions();
	testSelection();
	testChecksums();
	testHistory();
	testHealth();
	testPerformance();
	testLoudness();
	testLevelMath();
	std::printf("%d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
