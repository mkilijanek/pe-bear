#include "DownloadManager.h"
#include "ReleaseClient.h"
#include "UpdatePaths.h"

using namespace pe_bear::updater;

const int DownloadManager::CONNECT_TIMEOUT_MS;
const int DownloadManager::STALL_TIMEOUT_MS;
const qint64 DownloadManager::ABSOLUTE_MAX_BYTES;

DownloadManager::DownloadManager(QObject *parent)
	: IPackageDownloader(parent), m_manager(this), m_reply(NULL),
	m_maxBytes(0), m_written(0), m_stallTimer(this), m_cancelled(false)
{
	m_stallTimer.setSingleShot(true);
	connect(&m_stallTimer, SIGNAL(timeout()), this, SLOT(onStallTimeout()));
}

DownloadManager::~DownloadManager()
{
	if (m_reply) {
		m_reply->disconnect(this);
		m_reply->abort();
		m_reply->deleteLater();
		m_reply = NULL;
	}
	discardPartial();
}

bool DownloadManager::isAcceptableUrl(const QUrl &url) const
{
	return ReleaseClient::isAllowedHost(url, ReleaseClient::allowedDownloadHosts());
}

void DownloadManager::start(const ReleaseAsset &asset, const QString &targetDir)
{
	if (isBusy()) {
		emit failed(ErrorDownloadFailed, QLatin1String("a download is already running"));
		return;
	}
	m_cancelled = false;
	m_written = 0;

	if (!asset.isValid()) {
		emit failed(ErrorDownloadFailed, QLatin1String("incomplete asset metadata"));
		return;
	}
	if (asset.size > ABSOLUTE_MAX_BYTES) {
		emit failed(ErrorSizeMismatch, QLatin1String("the published package size is implausible"));
		return;
	}
	if (!isAcceptableUrl(asset.downloadUrl)) {
		emit failed(ErrorForbiddenRedirect, QLatin1String("the download URL points to an unexpected host"));
		return;
	}
	QDir dir(targetDir);
	if (!dir.exists() && !QDir().mkpath(targetDir)) {
		emit failed(ErrorStorage, QLatin1String("the download directory could not be created"));
		return;
	}
	/* QFileInfo::fileName() strips any path the server might try to smuggle
	   in; the asset name was already screened for separators at parse time. */
	const QString baseName = QFileInfo(asset.name).fileName();
	if (baseName.isEmpty()) {
		emit failed(ErrorDownloadFailed, QLatin1String("the asset has no usable file name"));
		return;
	}
	m_finalPath = dir.absoluteFilePath(baseName);
	m_partialPath = m_finalPath + QLatin1String(".part");
	m_maxBytes = asset.size;

	/* A leftover from an interrupted run is never resumed, only replaced. */
	if (QFile::exists(m_partialPath)) {
		QFile::remove(m_partialPath);
	}
	if (QFile::exists(m_finalPath)) {
		QFile::remove(m_finalPath);
	}

	m_file.setFileName(m_partialPath);
	if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		emit failed(ErrorStorage, QLatin1String("the package file could not be opened for writing"));
		return;
	}
	UpdatePaths::restrictToOwner(m_partialPath);

	QNetworkRequest request = ReleaseClient::buildRequest(asset.downloadUrl);
	request.setRawHeader("Accept", "application/octet-stream");
	m_reply = m_manager.get(request);
	if (!m_reply) {
		discardPartial();
		emit failed(ErrorNetwork, QLatin1String("the download could not be started"));
		return;
	}
	connect(m_reply, SIGNAL(readyRead()), this, SLOT(onReadyRead()));
	connect(m_reply, SIGNAL(finished()), this, SLOT(onFinished()));
	connect(m_reply, SIGNAL(downloadProgress(qint64, qint64)),
		this, SLOT(onDownloadProgress(qint64, qint64)));
	connect(m_reply, SIGNAL(sslErrors(QList<QSslError>)), this, SLOT(onSslErrors(QList<QSslError>)));
#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
	connect(m_reply, SIGNAL(redirected(QUrl)), this, SLOT(onRedirected(QUrl)));
#endif
	m_stallTimer.start(STALL_TIMEOUT_MS);
}

void DownloadManager::onReadyRead()
{
	if (!m_reply) return;
	m_stallTimer.start(STALL_TIMEOUT_MS);

	const QByteArray chunk = m_reply->readAll();
	if (chunk.isEmpty()) return;

	if (m_written + chunk.size() > m_maxBytes) {
		/* The server is sending more than the release said this asset weighs. */
		failWith(ErrorSizeMismatch, QLatin1String("the download exceeded the published size"));
		return;
	}
	const qint64 written = m_file.write(chunk);
	if (written != chunk.size()) {
		failWith(ErrorStorage, QLatin1String("the package could not be written to disk"));
		return;
	}
	m_written += written;
}

void DownloadManager::onDownloadProgress(qint64 received, qint64 total)
{
	m_stallTimer.start(STALL_TIMEOUT_MS);
	emit progress(received, (total > 0) ? total : m_maxBytes);
}

void DownloadManager::onSslErrors(const QList<QSslError> &errors)
{
	QString detail;
	if (!errors.isEmpty()) {
		detail = errors.first().errorString();
	}
	failWith(ErrorTls, detail);
}

void DownloadManager::onRedirected(const QUrl &url)
{
	if (!isAcceptableUrl(url)) {
		failWith(ErrorForbiddenRedirect, QLatin1String("redirect to an unexpected host was refused"));
	}
}

void DownloadManager::onStallTimeout()
{
	failWith(ErrorTimeout, QLatin1String("the download stalled"));
}

void DownloadManager::cancel()
{
	if (!isBusy()) return;
	m_cancelled = true;
	QNetworkReply *reply = m_reply;
	teardown();
	reply->abort();
	reply->deleteLater();
	discardPartial();
	emit failed(ErrorCancelled, QString());
}

void DownloadManager::failWith(UpdateError error, const QString &detail)
{
	if (!m_reply) return;
	QNetworkReply *reply = m_reply;
	teardown();
	reply->abort();
	reply->deleteLater();
	discardPartial();
	emit failed(error, detail);
}

void DownloadManager::teardown()
{
	m_stallTimer.stop();
	if (m_reply) {
		m_reply->disconnect(this);
		m_reply = NULL;
	}
	if (m_file.isOpen()) {
		m_file.close();
	}
}

void DownloadManager::discardPartial()
{
	if (m_file.isOpen()) {
		m_file.close();
	}
	if (!m_partialPath.isEmpty() && QFile::exists(m_partialPath)) {
		QFile::remove(m_partialPath);
	}
}

void DownloadManager::onFinished()
{
	QNetworkReply *reply = m_reply;
	if (!reply) return;

	const QByteArray tail = reply->readAll();
	if (!tail.isEmpty()) {
		if (m_written + tail.size() > m_maxBytes) {
			failWith(ErrorSizeMismatch, QLatin1String("the download exceeded the published size"));
			return;
		}
		if (m_file.write(tail) != tail.size()) {
			failWith(ErrorStorage, QLatin1String("the package could not be written to disk"));
			return;
		}
		m_written += tail.size();
	}

	const QNetworkReply::NetworkError netError = reply->error();
	const QString netErrorString = reply->errorString();
	const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
	const QUrl finalUrl = reply->url();

	teardown();
	reply->deleteLater();

	if (m_cancelled) {
		discardPartial();
		return;
	}
	if (netError != QNetworkReply::NoError) {
		discardPartial();
		const UpdateError mapped = (netError == QNetworkReply::TimeoutError)
			? ErrorTimeout : ErrorDownloadFailed;
		emit failed(mapped, netErrorString);
		return;
	}
	if (!isAcceptableUrl(finalUrl)) {
		discardPartial();
		emit failed(ErrorForbiddenRedirect, QLatin1String("the package came from an unexpected host"));
		return;
	}
	if (status != 0 && status != 200) {
		discardPartial();
		emit failed(ErrorDownloadFailed,
			QLatin1String("unexpected HTTP status ") + QString::number(status));
		return;
	}
	if (m_written != m_maxBytes) {
		discardPartial();
		emit failed(ErrorSizeMismatch, QLatin1String("the download is incomplete"));
		return;
	}

	if (QFile::exists(m_finalPath)) {
		QFile::remove(m_finalPath);
	}
	if (!QFile::rename(m_partialPath, m_finalPath)) {
		discardPartial();
		emit failed(ErrorStorage, QLatin1String("the package could not be moved into place"));
		return;
	}
	UpdatePaths::restrictToOwner(m_finalPath);
	m_partialPath.clear();
	emit finished(m_finalPath);
}
