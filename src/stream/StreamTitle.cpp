#include "stream/StreamTitle.hpp"

#include "core/Settings.hpp"
#include "core/YouTube.hpp"

#include <obs.h>
#include <obs-frontend-api.h>
#include <util/config-file.h>
#include <plugin-support.h>

#include <curl/curl.h>

#include <QDateTime>
#include <QTimer>

#include <cstring>

namespace obsviewer {

namespace {

constexpr qint64 kLiveIntervalMs = 60 * 1000;
constexpr qint64 kIdleIntervalMs = 3 * 60 * 1000;
constexpr size_t kMaxPage = 4 * 1024 * 1024;

struct Body {
	std::string *text = nullptr;
	const std::atomic<bool> *cancel = nullptr;
};

size_t onData(char *data, size_t size, size_t count, void *userdata)
{
	auto *b = static_cast<Body *>(userdata);
	const size_t n = size * count;
	if (b->text->size() + n > kMaxPage)
		return 0;
	b->text->append(data, n);
	return n;
}

int onProgress(void *userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	return static_cast<Body *>(userdata)->cancel->load() ? 1 : 0;
}

// GET over https; returns the HTTP status (0 = failed) and the body.
long httpGet(const std::string &url, std::string &body, const std::atomic<bool> &cancel)
{
	CURL *curl = curl_easy_init();
	if (!curl)
		return 0;
	Body b{&body, &cancel};
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	// A browser-like agent, or YouTube serves a stripped-down page.
	curl_easy_setopt(curl, CURLOPT_USERAGENT,
			 "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) "
			 "Version/17.0 Safari/605.1.15");
	struct curl_slist *headers = curl_slist_append(nullptr, "Accept-Language: en-US,en;q=0.8");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
	curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, ""); // gzip
	// Skip YouTube's EU cookie-consent page.
	curl_easy_setopt(curl, CURLOPT_COOKIE, "CONSENT=YES+1; SOCS=CAI");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onData);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &b);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, onProgress);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &b);
	long status = 0;
	if (curl_easy_perform(curl) == CURLE_OK)
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	return status;
}

// Title of a public / unlisted video from YouTube's oEmbed endpoint.
std::string oembedTitle(const std::string &videoId, const std::atomic<bool> &cancel, long &status)
{
	std::string body;
	status = httpGet("https://www.youtube.com/oembed?format=json&url=https://www.youtube.com/watch?v=" + videoId, body,
		     cancel);
	if (status != 200)
		return std::string();
	obs_data_t *data = obs_data_create_from_json(body.c_str());
	if (!data)
		return std::string();
	std::string title = obs_data_get_string(data, "title");
	obs_data_release(data);
	return title;
}

} // namespace

StreamTitle &StreamTitle::get()
{
	static StreamTitle instance;
	return instance;
}

void StreamTitle::start()
{
	if (timer_)
		return;
	timer_ = new QTimer();
	timer_->setInterval(2000);
	QObject::connect(timer_, &QTimer::timeout, [this] { tick(); });
	timer_->start();
	nextFetchMs_ = QDateTime::currentMSecsSinceEpoch() + 5000; // let OBS finish starting
}

void StreamTitle::stop()
{
	if (timer_) {
		timer_->stop();
		delete timer_;
		timer_ = nullptr;
	}
	cancel_ = true;
	if (worker_.joinable())
		worker_.join();
	cancel_ = false;
}

void StreamTitle::refreshNow()
{
	nextFetchMs_ = 0;
	tick();
}

std::string StreamTitle::shownTitle() const
{
	if (!Settings::get().showStreamTitle)
		return std::string();
	std::lock_guard<std::mutex> lock(mutex_);
	return title_;
}

std::string StreamTitle::status() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return status_;
}

void StreamTitle::tick()
{
	if (!Settings::get().showStreamTitle)
		return;
	if (worker_.joinable() && !busy_)
		worker_.join();
	const bool streaming = obs_frontend_streaming_active();
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	// The broadcast id is in place once the stream has started: look right away
	// (and again a little later, once YouTube has the broadcast live).
	if (streaming && !wasStreaming_)
		nextFetchMs_ = now + 3000;
	wasStreaming_ = streaming;
	if (busy_ || now < nextFetchMs_)
		return;
	nextFetchMs_ = now + (streaming ? kLiveIntervalMs : kIdleIntervalMs);
	launch();
}

void StreamTitle::launch()
{
	Inputs in;
	if (obs_service_t *service = obs_frontend_get_streaming_service()) {
		obs_data_t *settings = obs_service_get_settings(service);
		const std::string serviceName = obs_data_get_string(settings, "service");
		const std::string id = obs_data_get_string(settings, "broadcast_id");
		if (youtube::isVideoId(id))
			in.broadcastId = id;
		obs_data_release(settings);
		// Manage Broadcast's remembered title, only for a YouTube service.
		if (serviceName.find("YouTube") != std::string::npos)
			if (config_t *cfg = obs_frontend_get_profile_config())
				if (const char *t = config_get_string(cfg, "YouTube", "Title"))
					in.profileTitle = t;
	}
	in.liveUrl = youtube::liveUrlFor(Settings::get().youtubeChannel);

	if (in.broadcastId.empty() && in.liveUrl.empty() && in.profileTitle.empty()) {
		std::lock_guard<std::mutex> lock(mutex_);
		title_.clear();
		status_ = "Not found: connect OBS to your YouTube account (Settings ▸ Stream), or enter your channel below.";
		return;
	}

	if (worker_.joinable())
		worker_.join();
	busy_ = true;
	worker_ = std::thread([this, in] {
		std::string title, status;
		fetch(in, title, status, cancel_);
		if (!cancel_) {
			std::lock_guard<std::mutex> lock(mutex_);
			if (title != title_ && !title.empty())
				obs_log(LOG_INFO, "stream title: %s", title.c_str());
			title_ = title;
			status_ = status;
		}
		busy_ = false;
	});
}

void StreamTitle::fetch(const Inputs &in, std::string &title, std::string &status, const std::atomic<bool> &cancel)
{
	long code = 0;
	if (!in.broadcastId.empty()) {
		title = oembedTitle(in.broadcastId, cancel, code);
		if (!title.empty()) {
			status = "From the YouTube account OBS is connected to.";
			return;
		}
	}
	if (!in.liveUrl.empty() && !cancel) {
		std::string page;
		code = httpGet(in.liveUrl, page, cancel);
		if (code == 200) {
			const std::string id = youtube::videoIdFromPage(page);
			if (!id.empty()) {
				title = youtube::ogTitle(page);
				if (title.empty())
					title = oembedTitle(id, cancel, code);
				if (!title.empty()) {
					status = "From your channel's live page.";
					return;
				}
			}
			status = "Your channel has no live or upcoming stream right now.";
		} else {
			status = code == 404 ? "YouTube doesn't know that channel. Check the @handle."
					     : "Couldn't reach YouTube (" + std::to_string(code) + ").";
		}
	}
	if (!in.profileTitle.empty()) {
		title = in.profileTitle;
		status = "From OBS's Manage Broadcast settings (the stream may be private, or not live yet).";
		return;
	}
	if (status.empty())
		status = in.broadcastId.empty() ? "No YouTube broadcast found."
						: "YouTube didn't give the title (a private stream?). Enter your channel below.";
}

} // namespace obsviewer
