#include "update/ReleaseInfo.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace obsviewer {

namespace {

bool endsWith(const std::string &s, const std::string &suffix)
{
	if (s.size() < suffix.size())
		return false;
	return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin(), [](char a, char b) {
		return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
	});
}

bool isHex64(const std::string &s)
{
	if (s.size() != 64)
		return false;
	return std::all_of(s.begin(), s.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); });
}

std::string lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(),
		       [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
	return s;
}

std::string trim(const std::string &s)
{
	size_t a = 0, b = s.size();
	while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
		++a;
	while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
		--b;
	return s.substr(a, b - a);
}

} // namespace

const ReleaseAsset *Release::installer() const
{
	for (const auto &a : assets)
		if (endsWith(a.name, ".pkg"))
			return &a;
	return nullptr;
}

const ReleaseAsset *Release::checksums() const
{
	for (const auto &a : assets)
		if (lower(a.name) == "sha256sums.txt")
			return &a;
	return nullptr;
}

int selectUpdate(const std::vector<Release> &releases, const Version &current, bool includePrereleases,
		 const std::string &skipped, bool ignoreSkipped)
{
	const Version skippedVersion = Version::parse(skipped);
	int best = -1;
	for (size_t i = 0; i < releases.size(); ++i) {
		const Release &r = releases[i];
		if (r.draft || !r.version.valid)
			continue;
		if ((r.prerelease || r.version.isPrerelease()) && !includePrereleases)
			continue;
		if (!(r.version > current))
			continue;
		if (!r.installer())
			continue;
		if (!ignoreSkipped && skippedVersion.valid && r.version == skippedVersion)
			continue;
		if (best < 0 || r.version > releases[static_cast<size_t>(best)].version)
			best = static_cast<int>(i);
	}
	return best;
}

std::string findChecksum(const std::string &sumsText, const std::string &fileName)
{
	std::istringstream in(sumsText);
	std::string line;
	while (std::getline(in, line)) {
		line = trim(line);
		if (line.empty())
			continue;

		// "<hex>  <name>" or "<hex> *<name>" (sha256sum / shasum output)
		const size_t space = line.find_first_of(" \t");
		if (space != std::string::npos) {
			const std::string first = line.substr(0, space);
			std::string rest = trim(line.substr(space));
			if (!rest.empty() && rest[0] == '*')
				rest.erase(0, 1);
			if (isHex64(first) && rest == fileName)
				return lower(first);
		}

		// "<name>: <hex>"
		const size_t colon = line.rfind(':');
		if (colon != std::string::npos) {
			const std::string name = trim(line.substr(0, colon));
			const std::string hash = trim(line.substr(colon + 1));
			if (isHex64(hash) && name == fileName)
				return lower(hash);
		}
	}
	return {};
}

std::string digestToSha256(const std::string &digest)
{
	const std::string prefix = "sha256:";
	if (digest.size() != prefix.size() + 64 || lower(digest.substr(0, prefix.size())) != prefix)
		return {};
	const std::string hex = digest.substr(prefix.size());
	return isHex64(hex) ? lower(hex) : std::string();
}

} // namespace obsviewer
