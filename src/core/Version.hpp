#pragma once

#include <string>
#include <vector>

namespace obsviewer {

// Semantic version ("1.2.3", "v1.2.3-beta.2"). Build metadata after '+' is ignored.
struct Version {
	int major = 0;
	int minor = 0;
	int patch = 0;
	std::vector<std::string> prerelease; // dot-separated identifiers after '-'
	bool valid = false;

	static Version parse(const std::string &text);

	bool isPrerelease() const { return !prerelease.empty(); }
	std::string toString() const;

	// <0 if a < b, 0 if equal, >0 if a > b (semver precedence)
	static int compare(const Version &a, const Version &b);
};

inline bool operator<(const Version &a, const Version &b)
{
	return Version::compare(a, b) < 0;
}
inline bool operator>(const Version &a, const Version &b)
{
	return Version::compare(a, b) > 0;
}
inline bool operator==(const Version &a, const Version &b)
{
	return Version::compare(a, b) == 0;
}

} // namespace obsviewer
