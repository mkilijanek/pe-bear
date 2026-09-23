#pragma once

#include <QtCore>
#include <QtNetwork>
#include "UpdateTypes.h"

namespace pe_bear {
namespace updater {

/**
 * Fetches one package. Abstract so the state machine can be tested without a
 * server, and so that a future mirror/offline source can be dropped in.
 */
class IPackageDownloader : public QObject
{
	Q_OBJECT

public:
	explicit IPackageDownloader(QObject *parent = NULL) : QObject(parent) {}
	virtual ~IPackageDownloader() {}

	virtual void start(const ReleaseAsset &asset, const QString &targetDir) = 0;
	virtual void cancel() = 0;
	virtual bool isBusy() const = 0;

signals:
	void progress(qint64 received, qint64 total);
	/** Emitted with the absolute path of the completed, not yet verified file. */
	void finished(const QString &path);
	void failed(int error, const QString &detail);
};

//----------------------------------------------------------------------

/**
 * Streams a release asset to disk without blocking the GUI.
 *
 * The file is written to "<name>.part" and only renamed into place once the
 * transfer completes, so a half-written package can never be mistaken for a
 * finished one. The transfer is capped at the size the release metadata
 * declared: a server that sends more than it promised is cut off rather than
 * allowed to fill the disk. Any failure or cancellation removes the partial
 * file -- v1 deliberately has no Range resume, because resuming means trusting
 * bytes from an earlier, unverified session.
 *
 * One download at a time, by construction.
 */
class DownloadManager : public IPackageDownloader
{
	Q_OBJECT

public:
	static const int CONNECT_TIMEOUT_MS = 10 * 1000;
	/** Idle timeout: no progress at all for this long ends the transfer. */
	static const int STALL_TIMEOUT_MS = 60 * 1000;
	/** Refuse anything absurd even before the per-asset cap applies. */
	static const qint64 ABSOLUTE_MAX_BYTES = Q_INT64_C(1024) * 1024 * 1024;

	explicit DownloadManager(QObject *parent = NULL);
	virtual ~DownloadManager();

	virtual void start(const ReleaseAsset &asset, const QString &targetDir);
	virtual void cancel();
	virtual bool isBusy() const { return m_reply != NULL; }

	QString partialPath() const { return m_partialPath; }

protected:
	/**
	 * Decides whether a URL may be fetched. In production this is the release
	 * host allowlist over HTTPS and nothing else; the download tests override
	 * it to reach a local server. There is deliberately no setter, so the
	 * policy cannot be relaxed at runtime.
	 */
	virtual bool isAcceptableUrl(const QUrl &url) const;

private slots:
	void onReadyRead();
	void onFinished();
	void onDownloadProgress(qint64 received, qint64 total);
	void onSslErrors(const QList<QSslError> &errors);
	void onRedirected(const QUrl &url);
	void onStallTimeout();

private:
	void failWith(UpdateError error, const QString &detail);
	void discardPartial();
	void teardown();

	QNetworkAccessManager m_manager;
	QNetworkReply *m_reply;
	QFile m_file;
	QString m_partialPath;
	QString m_finalPath;
	qint64 m_maxBytes;
	qint64 m_written;
	QTimer m_stallTimer;
	bool m_cancelled;
};

}; // namespace updater
}; // namespace pe_bear
