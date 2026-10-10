#include "core/Version.hpp"

#include <cctype>
#include <cstdlib>

namespace obsviewer {

namespace {

bool isNumber(const std::string &s)
{
	if (s.empty())
		return false;
	for (char c : s)
		if (!std::isdigit(static_cast<unsigned char>(c)))
			return false;
	return true;
}

std::vector<std::string> split(const std::string &s, char sep)
{
	std::vector<std::string> parts;
	std::string current;
	for (char c : s) {
		if (c == sep) {
			parts.push_back(current);
			current.clear();
		} else {
			current += c;
		}
	}
	parts.push_back(current);
	return parts;
}

} // namespace

Version Version::parse(const std::string &input)
{
	Version v;
	std::string text = input;

	// trim whitespace
	while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
		text.erase(text.begin());
	while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
		text.pop_back();

	if (!text.empty() && (text[0] == 'v' || text[0] == 'V'))
		text.erase(0, 1);

	const size_t plus = text.find('+');
	if (plus != std::string::npos)
		text.erase(plus);

	std::string core = text;
	const size_t dash = text.find('-');
	if (dash != std::string::npos) {
		core = text.substr(0, dash);
		const std::string pre = text.substr(dash + 1);
		if (pre.empty())
			return v;
		v.prerelease = split(pre, '.');
		for (const auto &id : v.prerelease)
			if (id.empty())
				return Version{};
	}

	const auto nums = split(core, '.');
	if (nums.size() < 2 || nums.size() > 3)
		return Version{};
	for (const auto &n : nums)
		if (!isNumber(n) || n.size() > 9)
			return Version{};

	v.major = std::atoi(nums[0].c_str());
	v.minor = std::atoi(nums[1].c_str());
	v.patch = nums.size() == 3 ? std::atoi(nums[2].c_str()) : 0;
	v.valid = true;
	return v;
}

std::string Version::toString() const
{
	std::string s = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
	for (size_t i = 0; i < prerelease.size(); ++i)
		s += (i == 0 ? "-" : ".") + prerelease[i];
	return s;
}

int Version::compare(const Version &a, const Version &b)
{
	if (a.major != b.major)
		return a.major < b.major ? -1 : 1;
	if (a.minor != b.minor)
		return a.minor < b.minor ? -1 : 1;
	if (a.patch != b.patch)
		return a.patch < b.patch ? -1 : 1;

	// A release ranks above any of its pre-releases.
	if (a.prerelease.empty() && b.prerelease.empty())
		return 0;
	if (a.prerelease.empty())
		return 1;
	if (b.prerelease.empty())
		return -1;

	const size_t n = a.prerelease.size() < b.prerelease.size() ? a.prerelease.size() : b.prerelease.size();
	for (size_t i = 0; i < n; ++i) {
		const std::string &x = a.prerelease[i];
		const std::string &y = b.prerelease[i];
		const bool xn = isNumber(x), yn = isNumber(y);
		if (xn && yn) {
			const long xv = std::atol(x.c_str()), yv = std::atol(y.c_str());
			if (xv != yv)
				return xv < yv ? -1 : 1;
		} else if (xn != yn) {
			return xn ? -1 : 1; // numeric identifiers rank lower
		} else if (x != y) {
			return x < y ? -1 : 1;
		}
	}
	if (a.prerelease.size() != b.prerelease.size())
		return a.prerelease.size() < b.prerelease.size() ? -1 : 1;
	return 0;
}

} // namespace obsviewer
