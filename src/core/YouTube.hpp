#pragma once

#include <string>

// Small, testable helpers for finding a YouTube live stream's title.
namespace obsviewer::youtube {

// True for an 11-character YouTube video id.
bool isVideoId(const std::string &id);

// The channel's "/live" page for what the user typed in Settings: "@handle",
// a channel id ("UC…"), or a youtube.com channel URL. Empty if it isn't one.
std::string liveUrlFor(const std::string &channel);

// From a channel's /live page: the video id it points at (its canonical
// watch URL), or empty when the channel has no live or upcoming stream.
std::string videoIdFromPage(const std::string &html);

// <meta property="og:title" content="…"> from a YouTube page, entities decoded.
std::string ogTitle(const std::string &html);

// Decodes the HTML entities YouTube uses in attributes (&amp; &quot; &#39; &#NN; &#xNN;).
std::string decodeEntities(const std::string &s);

} // namespace obsviewer::youtube
