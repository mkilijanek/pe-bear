#pragma once

#include <QtCore>
#include "UpdateTypes.h"

namespace pe_bear {
namespace updater {

/**
 * Confirms that a downloaded package is exactly the one the release metadata
 * described: same byte count, same SHA-256.
 *
 * The digest is computed by streaming the file in fixed-size chunks, so a
 * multi-hundred-megabyte package never has to be held in memory. A package
 * that fails is deleted immediately -- leaving it on disk invites it being
 * picked up later by something less careful.
 *
 * This result is not a trust anchor. The installer helper recomputes the
 * digest in its own process before touching anything.
 */
class PackageVerifier
{
public:
	static const int CHUNK_BYTES = 64 * 1024;

	struct Result
	{
		Result() : ok(false), error(ErrorNone), size(0), deleted(false) {}

		bool ok;
		UpdateError error;
		QString sha256;
		qint64 size;
		/** True when a failing package was removed from disk. */
		bool deleted;
	};

	/** Streams @p path and returns its lowercase hex SHA-256, or empty on I/O error. */
	static QString computeSha256(const QString &path, UpdateError *error = NULL);

	/**
	 * @param expectedSize    size published for the asset; must match exactly
	 * @param expectedSha256  lowercase 64-hex digest published for the asset
	 * @param removeOnFailure delete the package when it does not match
	 */
	static Result verify(const QString &path, qint64 expectedSize,
		const QString &expectedSha256, bool removeOnFailure = true);
};

//----------------------------------------------------------------------

/**
 * The same verification, spread over the event loop.
 *
 * Hashing a package of a few hundred megabytes in one call would freeze the
 * window for as long as it takes. This reads a bounded number of chunks per
 * event-loop pass instead, so the GUI keeps repainting and the user can still
 * cancel. No extra thread is involved, which keeps the failure modes simple.
 */
class AsyncPackageVerifier : public QObject
{
	Q_OBJECT

public:
	/** Chunks hashed per pass; keeps each pass in the low milliseconds. */
	static const int CHUNKS_PER_PASS = 32;

	explicit AsyncPackageVerifier(QObject *parent = NULL);
	virtual ~AsyncPackageVerifier();

	/** Starts verifying. Emits finished() later, never before returning. */
	void start(const QString &path, qint64 expectedSize, const QString &expectedSha256,
		bool removeOnFailure = true);
	void cancel();
	bool isBusy() const { return m_busy; }

signals:
	void progress(qint64 hashed, qint64 total);
	void finished(const pe_bear::updater::PackageVerifier::Result &result);

private slots:
	void step();

private:
	void finishWith(const PackageVerifier::Result &result);
	void reset();

	QFile m_file;
	QCryptographicHash m_hash;
	QTimer m_timer;
	QByteArray m_buffer;
	QString m_path;
	QString m_expectedSha256;
	qint64 m_expectedSize;
	qint64 m_hashed;
	bool m_removeOnFailure;
	bool m_busy;
};

}; // namespace updater
}; // namespace pe_bear

Q_DECLARE_METATYPE(pe_bear::updater::PackageVerifier::Result)
