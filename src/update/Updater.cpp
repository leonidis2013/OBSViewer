#include "update/Updater.hpp"

#include "core/Settings.hpp"
#include "update/Keychain.hpp"
#include "update/UpdateDialog.hpp"

#include <obs.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <curl/curl.h>

#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#endif

#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QStandardPaths>
#include <QWidget>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <functional>

namespace obsviewer {

namespace {

constexpr int64_t kAutoCheckIntervalSeconds = 12 * 60 * 60;

struct HttpResult {
	long status = 0;
	std::string body;
	std::string error;
	bool cancelled = false;
};

struct Transfer {
	std::string *body = nullptr;
	FILE *file = nullptr;
	std::atomic<bool> *cancel = nullptr;
	std::function<void(int64_t, int64_t)> progress;
	std::chrono::steady_clock::time_point lastProgress{};
	size_t limit = 8 * 1024 * 1024; // in-memory responses are small; cap them
};

size_t writeCallback(char *data, size_t size, size_t count, void *userdata)
{
	auto *t = static_cast<Transfer *>(userdata);
	const size_t n = size * count;
	if (t->file)
		return fwrite(data, 1, n, t->file);
	if (t->body) {
		if (t->body->size() + n > t->limit)
			return 0;
		t->body->append(data, n);
	}
	return n;
}

int progressCallback(void *userdata, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t)
{
	auto *t = static_cast<Transfer *>(userdata);
	if (t->cancel && t->cancel->load())
		return 1; // abort
	if (t->progress) {
		const auto tick = std::chrono::steady_clock::now();
		if (tick - t->lastProgress > std::chrono::milliseconds(100) || (total > 0 && now == total)) {
			t->lastProgress = tick;
			t->progress(static_cast<int64_t>(now), static_cast<int64_t>(total));
		}
	}
	return 0;
}

HttpResult httpGet(const std::string &url, const std::string &token, const char *accept, std::atomic<bool> &cancel,
		   FILE *file = nullptr, std::function<void(int64_t, int64_t)> progress = {})
{
	HttpResult result;
	CURL *curl = curl_easy_init();
	if (!curl) {
		result.error = "could not start a network request";
		return result;
	}

	Transfer transfer;
	transfer.body = file ? nullptr : &result.body;
	transfer.file = file;
	transfer.cancel = &cancel;
	transfer.progress = std::move(progress);

	const std::string userAgent = std::string("OBSViewer/") + PLUGIN_VERSION;
	struct curl_slist *headers = nullptr;
	headers = curl_slist_append(headers, (std::string("Accept: ") + accept).c_str());
	headers = curl_slist_append(headers, "X-GitHub-Api-Version: 2022-11-28");
	if (!token.empty())
		headers = curl_slist_append(headers, ("Authorization: Bearer " + token).c_str());

	char errorBuffer[CURL_ERROR_SIZE] = {0};
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent.c_str());
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
	// Never forward the Authorization header to the CDN host GitHub redirects downloads to.
	curl_easy_setopt(curl, CURLOPT_UNRESTRICTED_AUTH, 0L);
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
	curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &transfer);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &transfer);

	const CURLcode code = curl_easy_perform(curl);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);

	if (code == CURLE_ABORTED_BY_CALLBACK) {
		result.cancelled = true;
		result.error = "cancelled";
	} else if (code != CURLE_OK) {
		result.error = errorBuffer[0] ? errorBuffer : curl_easy_strerror(code);
	}

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	return result;
}

std::string str(obs_data_t *data, const char *key)
{
	const char *v = obs_data_get_string(data, key);
	return v ? std::string(v) : std::string();
}

std::vector<Release> parseReleases(const std::string &json, bool &ok)
{
	std::vector<Release> releases;
	ok = false;

	// obs_data needs an object at the root; GitHub returns an array.
	const std::string wrapped = "{\"releases\":" + json + "}";
	obs_data_t *root = obs_data_create_from_json(wrapped.c_str());
	if (!root)
		return releases;

	obs_data_array_t *array = obs_data_get_array(root, "releases");
	if (array) {
		ok = true;
		const size_t count = obs_data_array_count(array);
		for (size_t i = 0; i < count; ++i) {
			obs_data_t *item = obs_data_array_item(array, i);
			Release r;
			r.tag = str(item, "tag_name");
			r.name = str(item, "name");
			r.body = str(item, "body");
			r.htmlUrl = str(item, "html_url");
			r.publishedAt = str(item, "published_at");
			r.draft = obs_data_get_bool(item, "draft");
			r.prerelease = obs_data_get_bool(item, "prerelease");
			r.version = Version::parse(r.tag);

			obs_data_array_t *assets = obs_data_get_array(item, "assets");
			const size_t assetCount = assets ? obs_data_array_count(assets) : 0;
			for (size_t j = 0; j < assetCount; ++j) {
				obs_data_t *a = obs_data_array_item(assets, j);
				ReleaseAsset asset;
				asset.name = str(a, "name");
				asset.apiUrl = str(a, "url");
				asset.downloadUrl = str(a, "browser_download_url");
				asset.sha256 = digestToSha256(str(a, "digest"));
				asset.size = obs_data_get_int(a, "size");
				r.assets.push_back(asset);
				obs_data_release(a);
			}
			obs_data_array_release(assets);

			releases.push_back(std::move(r));
			obs_data_release(item);
		}
		obs_data_array_release(array);
	}
	obs_data_release(root);
	return releases;
}

QString httpErrorMessage(const HttpResult &r, bool haveToken)
{
	if (!r.error.empty())
		return QObject::tr("Network error: %1").arg(QString::fromStdString(r.error));
	switch (r.status) {
	case 401:
		return QObject::tr("GitHub rejected the access token. Enter a new token in OBSViewer Settings ▸ Updates.");
	case 403:
		return haveToken ? QObject::tr("GitHub refused the request (403). Check that the token has read access "
					       "to the repository's Contents.")
				 : QObject::tr("GitHub refused the request (403). You may have hit the hourly limit; "
					       "try again later or add a token in Settings ▸ Updates.");
	case 404:
		return haveToken ? QObject::tr("The release repository wasn't found. Check the repository name and that "
					       "the token can read it.")
				 : QObject::tr("The release repository wasn't found. If it is private, add a GitHub "
					       "access token in OBSViewer Settings ▸ Updates.");
	default:
		return QObject::tr("GitHub returned HTTP %1.").arg(r.status);
	}
}

std::string sha256OfFile(const std::string &path)
{
#ifdef __APPLE__
	FILE *f = std::fopen(path.c_str(), "rb");
	if (!f)
		return {};
	CC_SHA256_CTX ctx;
	CC_SHA256_Init(&ctx);
	unsigned char buffer[1 << 16];
	size_t n;
	while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0)
		CC_SHA256_Update(&ctx, buffer, static_cast<CC_LONG>(n));
	std::fclose(f);
	unsigned char digest[CC_SHA256_DIGEST_LENGTH];
	CC_SHA256_Final(digest, &ctx);
	static const char *hex = "0123456789abcdef";
	std::string out;
	for (unsigned char b : digest) {
		out += hex[b >> 4];
		out += hex[b & 0xF];
	}
	return out;
#else
	(void)path;
	return {};
#endif
}

QWidget *mainWindow()
{
	return static_cast<QWidget *>(obs_frontend_get_main_window());
}

} // namespace

Updater::Updater(QObject *parent) : QObject(parent) {}

Updater::~Updater()
{
	cancel();
}

Version Updater::currentVersion()
{
	return Version::parse(PLUGIN_VERSION);
}

void Updater::cancel()
{
	cancel_ = true;
	if (worker_.joinable())
		worker_.join();
	cancel_ = false;
}

bool Updater::startWorker(std::function<void()> job)
{
	if (busy_.load())
		return false;
	if (worker_.joinable())
		worker_.join();
	busy_ = true;
	worker_ = std::thread([this, job = std::move(job)] {
		job();
		busy_ = false;
	});
	return true;
}

void Updater::checkIfDue()
{
	const Settings &s = Settings::get();
	if (!s.updateAutoCheck)
		return;
	const int64_t now = static_cast<int64_t>(std::time(nullptr));
	if (s.updateLastCheck > 0 && now - s.updateLastCheck < kAutoCheckIntervalSeconds)
		return;
	checkNow(false);
}

Updater::CheckResult Updater::fetchReleases(const std::string &repo, const std::string &token)
{
	CheckResult result;
	const std::string url = "https://api.github.com/repos/" + repo + "/releases?per_page=30";
	const HttpResult http = httpGet(url, token, "application/vnd.github+json", cancel_);
	if (http.cancelled)
		return result;
	if (!http.error.empty() || http.status != 200) {
		result.error = httpErrorMessage(http, !token.empty());
		return result;
	}
	bool ok = false;
	result.releases = parseReleases(http.body, ok);
	if (!ok) {
		result.error = tr("GitHub's reply couldn't be read.");
		return result;
	}
	result.ok = true;
	return result;
}

void Updater::checkNow(bool interactive)
{
	if (dialog_) {
		dialog_->show();
		dialog_->raise();
		return;
	}

	const std::string repo = Settings::get().updateRepo;
	const bool started = startWorker([this, interactive, repo] {
		const std::string token = keychain::loadToken();
		CheckResult result = fetchReleases(repo, token);
		if (cancel_.load())
			return;
		QMetaObject::invokeMethod(
			this, [this, interactive, result] { handleCheckResult(interactive, result); },
			Qt::QueuedConnection);
	});

	if (!started && interactive)
		QMessageBox::information(mainWindow(), tr("OBSViewer"), tr("An update check is already running."));
}

void Updater::handleCheckResult(bool interactive, const CheckResult &result)
{
	if (!result.ok) {
		obs_log(LOG_WARNING, "update check failed: %s", result.error.toUtf8().constData());
		if (interactive)
			QMessageBox::warning(mainWindow(), tr("OBSViewer Updates"), result.error);
		return;
	}

	Settings &s = Settings::get();
	s.updateLastCheck = static_cast<int64_t>(std::time(nullptr));
	s.save();

	const Version current = currentVersion();
	const int index =
		selectUpdate(result.releases, current, s.updateIncludePrereleases, s.updateSkippedVersion, interactive);

	if (index < 0) {
		obs_log(LOG_INFO, "no update available (running %s)", PLUGIN_VERSION);
		if (interactive)
			QMessageBox::information(mainWindow(), tr("OBSViewer Updates"),
						 tr("You're up to date. OBSViewer %1 is the newest version.")
							 .arg(QString::fromStdString(current.toString())));
		return;
	}

	const Release &release = result.releases[static_cast<size_t>(index)];
	obs_log(LOG_INFO, "update available: %s", release.tag.c_str());

	dialog_ = new UpdateDialog(release, this, mainWindow());
	dialog_->setAttribute(Qt::WA_DeleteOnClose);
	dialog_->show();
	dialog_->raise();
}

void Updater::download(const Release &release)
{
	const std::string repo = Settings::get().updateRepo;
	const bool started = startWorker([this, release, repo] {
		runDownload(release, repo, keychain::loadToken());
	});
	if (!started)
		emit downloadFailed(tr("Another update task is still running. Try again in a moment."));
}

void Updater::runDownload(Release release, std::string repo, std::string token)
{
	(void)repo;
	auto fail = [this](const QString &message) {
		QMetaObject::invokeMethod(this, [this, message] { emit downloadFailed(message); }, Qt::QueuedConnection);
	};

	const ReleaseAsset *pkg = release.installer();
	if (!pkg) {
		fail(tr("This release has no macOS installer."));
		return;
	}

	// Expected checksum: GitHub's asset digest, else SHA256SUMS.txt from the release.
	std::string expected = pkg->sha256;
	if (expected.empty()) {
		if (const ReleaseAsset *sums = release.checksums()) {
			const HttpResult http = httpGet(sums->apiUrl, token, "application/octet-stream", cancel_);
			if (http.cancelled)
				return;
			if (http.error.empty() && http.status == 200)
				expected = findChecksum(http.body, pkg->name);
		}
	}
	if (expected.empty()) {
		fail(tr("The release doesn't publish a checksum for its installer, so it can't be verified. "
			"Download it from GitHub instead."));
		return;
	}

	QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
	if (dir.isEmpty())
		dir = QDir::tempPath();
	QDir().mkpath(dir);
	const QString finalPath = QDir(dir).filePath(QString::fromStdString(pkg->name));
	const QString partPath = finalPath + QStringLiteral(".part");
	const std::string part = QFile::encodeName(partPath).toStdString();

	FILE *file = std::fopen(part.c_str(), "wb");
	if (!file) {
		fail(tr("Couldn't write to %1.").arg(dir));
		return;
	}

	const HttpResult http = httpGet(pkg->apiUrl, token, "application/octet-stream", cancel_, file,
					[this](int64_t now, int64_t total) {
						QMetaObject::invokeMethod(
							this,
							[this, now, total] {
								emit downloadProgress(static_cast<qint64>(now),
										      static_cast<qint64>(total));
							},
							Qt::QueuedConnection);
					});
	std::fclose(file);

	if (http.cancelled) {
		QFile::remove(partPath);
		return;
	}
	if (!http.error.empty() || http.status != 200) {
		QFile::remove(partPath);
		fail(httpErrorMessage(http, !token.empty()));
		return;
	}

	const std::string actual = sha256OfFile(part);
	if (actual != expected) {
		QFile::remove(partPath);
		obs_log(LOG_WARNING, "installer checksum mismatch: expected %s, got %s", expected.c_str(),
			actual.c_str());
		fail(tr("The downloaded installer failed its checksum check and was deleted. Nothing was installed."));
		return;
	}

	QFile::remove(finalPath);
	if (!QFile::rename(partPath, finalPath)) {
		QFile::remove(partPath);
		fail(tr("Couldn't save the installer to %1.").arg(dir));
		return;
	}

	QMetaObject::invokeMethod(this, [this, finalPath] { emit downloadFinished(finalPath); }, Qt::QueuedConnection);
}

} // namespace obsviewer
