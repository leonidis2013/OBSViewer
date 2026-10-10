#pragma once

#include "core/Version.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace obsviewer {

struct ReleaseAsset {
	std::string name;
	std::string apiUrl;      // https://api.github.com/repos/{o}/{r}/releases/assets/{id}
	std::string downloadUrl; // browser_download_url
	std::string sha256;      // lowercase hex, from GitHub's "digest" when present
	int64_t size = 0;
};

struct Release {
	std::string tag;
	std::string name;
	std::string body; // release notes (Markdown)
	std::string htmlUrl;
	std::string publishedAt;
	bool draft = false;
	bool prerelease = false;
	Version version;
	std::vector<ReleaseAsset> assets;

	const ReleaseAsset *installer() const; // first *.pkg
	const ReleaseAsset *checksums() const; // SHA256SUMS.txt
};

// Picks the newest release that is newer than `current`, not a draft, not a
// pre-release unless `includePrereleases`, has a .pkg, and isn't `skipped`
// (unless forced). Returns -1 if there is none.
int selectUpdate(const std::vector<Release> &releases, const Version &current, bool includePrereleases,
		 const std::string &skipped, bool ignoreSkipped);

// Finds the hash for `fileName` in SHA256SUMS-style text ("<hex>  <name>" or
// "<name>: <hex>"). Returns an empty string when not found.
std::string findChecksum(const std::string &sumsText, const std::string &fileName);

// "sha256:abcd…" → "abcd…" (lowercase); anything else → "".
std::string digestToSha256(const std::string &digest);

} // namespace obsviewer
