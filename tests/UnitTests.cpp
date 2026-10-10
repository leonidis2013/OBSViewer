// Host-side unit tests for OBSViewer's pure logic (no OBS or Qt needed).
// Build (from the repo root):
//   g++ -std=c++17 -Isrc tests/UnitTests.cpp src/core/Version.cpp src/viewer/Layouts.cpp
//       src/update/ReleaseInfo.cpp -o unit_tests && ./unit_tests

#include "core/Version.hpp"
#include "update/ReleaseInfo.hpp"
#include "viewer/Layouts.hpp"

#include <cmath>
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

int main()
{
	testLayouts();
	testVersions();
	testSelection();
	testChecksums();
	std::printf("%d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
