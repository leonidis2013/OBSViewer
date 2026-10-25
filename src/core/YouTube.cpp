#include "core/YouTube.hpp"

#include <cctype>
#include <cstdlib>

namespace obsviewer::youtube {

namespace {

bool idChar(char c)
{
	return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_';
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

void appendUtf8(std::string &out, unsigned long cp)
{
	if (cp < 0x80) {
		out += static_cast<char>(cp);
	} else if (cp < 0x800) {
		out += static_cast<char>(0xC0 | (cp >> 6));
		out += static_cast<char>(0x80 | (cp & 0x3F));
	} else if (cp < 0x10000) {
		out += static_cast<char>(0xE0 | (cp >> 12));
		out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (cp & 0x3F));
	} else if (cp < 0x110000) {
		out += static_cast<char>(0xF0 | (cp >> 18));
		out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
		out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (cp & 0x3F));
	}
}

// Value of attribute `attr` in the tag that contains position `pos`.
std::string attributeInTag(const std::string &html, size_t pos, const std::string &attr)
{
	const size_t open = html.rfind('<', pos);
	const size_t close = html.find('>', pos);
	if (open == std::string::npos || close == std::string::npos)
		return std::string();
	const std::string tag = html.substr(open, close - open);
	const std::string key = attr + "=\"";
	const size_t k = tag.find(key);
	if (k == std::string::npos)
		return std::string();
	const size_t start = k + key.size();
	const size_t end = tag.find('"', start);
	return end == std::string::npos ? std::string() : tag.substr(start, end - start);
}

} // namespace

bool isVideoId(const std::string &id)
{
	if (id.size() != 11)
		return false;
	for (char c : id)
		if (!idChar(c))
			return false;
	return true;
}

std::string liveUrlFor(const std::string &channelIn)
{
	std::string c = trim(channelIn);
	if (c.empty() || c == "@")
		return std::string();
	// Full URL: keep the channel part.
	for (const char *prefix : {"https://", "http://"})
		if (c.rfind(prefix, 0) == 0)
			c = c.substr(std::string(prefix).size());
	for (const char *host : {"www.youtube.com/", "m.youtube.com/", "youtube.com/"})
		if (c.rfind(host, 0) == 0) {
			c = c.substr(std::string(host).size());
			break;
		}
	while (!c.empty() && c.back() == '/')
		c.pop_back();
	if (c.size() >= 5 && c.compare(c.size() - 5, 5, "/live") == 0)
		c.erase(c.size() - 5);
	if (c.empty())
		return std::string();
	// Drop a trailing tab ("/@church/videos" → "/@church").
	if (c[0] == '@') {
		const size_t slash = c.find('/');
		if (slash != std::string::npos)
			c.erase(slash);
	} else if (c.rfind("channel/", 0) == 0 || c.rfind("c/", 0) == 0 || c.rfind("user/", 0) == 0) {
		const size_t first = c.find('/');
		const size_t second = c.find('/', first + 1);
		if (second != std::string::npos)
			c.erase(second);
	}

	std::string path;
	if (c[0] == '@') {
		path = c;
	} else if (c.rfind("channel/", 0) == 0 || c.rfind("c/", 0) == 0 || c.rfind("user/", 0) == 0) {
		path = c;
	} else if (c.size() == 24 && c.rfind("UC", 0) == 0) {
		path = "channel/" + c;
	} else {
		bool plain = true;
		for (char ch : c)
			if (!(idChar(ch) || ch == '.'))
				plain = false;
		if (!plain)
			return std::string();
		path = "@" + c; // a handle typed without the @
	}
	for (char ch : path)
		if (!(idChar(ch) || ch == '@' || ch == '/' || ch == '.'))
			return std::string();
	return "https://www.youtube.com/" + path + "/live";
}

std::string videoIdFromPage(const std::string &html)
{
	size_t pos = 0;
	while ((pos = html.find("rel=\"canonical\"", pos)) != std::string::npos) {
		const std::string href = attributeInTag(html, pos, "href");
		const size_t v = href.find("watch?v=");
		if (v != std::string::npos) {
			const std::string id = href.substr(v + 8, 11);
			if (isVideoId(id))
				return id;
		}
		pos += 1;
	}
	return std::string();
}

std::string ogTitle(const std::string &html)
{
	const size_t pos = html.find("property=\"og:title\"");
	if (pos == std::string::npos)
		return std::string();
	return decodeEntities(attributeInTag(html, pos, "content"));
}

std::string decodeEntities(const std::string &s)
{
	std::string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size(); ++i) {
		if (s[i] != '&') {
			out += s[i];
			continue;
		}
		const size_t semi = s.find(';', i);
		if (semi == std::string::npos || semi - i > 10) {
			out += s[i];
			continue;
		}
		const std::string ent = s.substr(i + 1, semi - i - 1);
		if (ent == "amp")
			out += '&';
		else if (ent == "quot")
			out += '"';
		else if (ent == "apos")
			out += '\'';
		else if (ent == "lt")
			out += '<';
		else if (ent == "gt")
			out += '>';
		else if (ent.size() > 1 && ent[0] == '#') {
			const bool hex = ent[1] == 'x' || ent[1] == 'X';
			char *end = nullptr;
			const std::string digits = ent.substr(hex ? 2 : 1);
			const unsigned long cp = std::strtoul(digits.c_str(), &end, hex ? 16 : 10);
			if (digits.empty() || !end || *end != '\0') {
				out += s.substr(i, semi - i + 1);
			} else {
				appendUtf8(out, cp);
			}
		} else {
			out += s.substr(i, semi - i + 1);
		}
		i = semi;
	}
	return out;
}

} // namespace obsviewer::youtube
