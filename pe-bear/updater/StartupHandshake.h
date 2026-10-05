#pragma once

#include <QtCore>
#include "FileSystem.h"
#include "Version.h"

namespace pe_bear {
namespace updater {

/**
 * How a freshly installed build proves to the helper that it works.
 *
 * The requirement this exists to satisfy: *elapsed process time is not a
 * criterion*. A new build that starts and crashes a second later has not
 * validated itself, and a helper that waited two seconds and committed would
 * have destroyed the only working copy. So the new build has to say something
 * specific, and the helper has to check it.
 *
 * The exchange is a single file, deliberately:
 *
 *   1. the helper writes a request carrying a one-time token and the version
 *      it expects, somewhere outside the directory being replaced;
 *   2. it launches the new build with the request's path;
 *   3. the new build reads the request, checks its own version against the
 *      expectation, writes a response carrying the same token, and exits;
 *   4. the helper reads the response and accepts only an exact token match
 *      with an acceptable status.
 *
 * A file rather than a socket or a pipe: the two processes are the same
 * program at two versions, on three operating systems, one of which has just
 * had its files replaced underneath it. A named pipe or local socket adds a
 * second failure domain -- permissions, naming, lifetime -- to a step whose
 * entire job is to be trustworthy. A file is also what a person can read
 * afterwards when they want to know why an update rolled back.
 *
 * The token is what makes a stale response from an earlier attempt useless:
 * without it, a leftover file saying "all fine" would validate an install that
 * never ran.
 */
class StartupHandshake
{
public:
	/** Why a response was not accepted. */
	enum Verdict {
		Accepted = 0,
		/** The new build never wrote anything. */
		NoResponse,
		/** Present but not parseable -- treated as a failure, not absence. */
		Malformed,
		/** From a different attempt. */
		TokenMismatch,
		/** It started, but it is not the version that was installed. */
		VersionMismatch,
		/** It started and reported a problem itself. */
		ReportedFailure,
		VERDICTS_COUNT
	};

	static QString verdictToString(Verdict v);
	static QString verdictMessage(Verdict v);

	struct Request
	{
		Request() {}

		/** Unpredictable, single-use. */
		QString token;
		/** What the newly installed build must report. */
		QString expectedVersion;
		/** Where the response is to be written. */
		QString responsePath;

		bool isValid() const
		{
			return token.length() >= 32 && !expectedVersion.isEmpty() && !responsePath.isEmpty();
		}

		QByteArray toJson() const;
		static Request fromJson(const QByteArray &data, bool *ok = NULL);
	};

	struct Response
	{
		Response() : started(false) {}

		QString token;
		QString version;
		/** False when the build itself decided it had not initialised. */
		bool started;
		/** The build's own reason, when it reports a failure. */
		QString detail;

		QByteArray toJson() const;
		static Response fromJson(const QByteArray &data, bool *ok = NULL);
	};

	/** @param fs borrowed, must outlive this object */
	explicit StartupHandshake(IFileSystem *fs);

	/* --- the helper's side --- */

	/**
	 * Builds a request and writes it to @p requestPath.
	 *
	 * @p requestPath must not be inside the directory being replaced: putting
	 * it there would let activation delete the very file the exchange depends
	 * on, and the resulting NoResponse would look like a crash.
	 */
	bool createRequest(const QString &requestPath, const QString &responsePath,
		const Version &expectedVersion, Request *out);

	/** Reads the response and judges it. Removes nothing. */
	Verdict verifyResponse(const Request &request) const;

	/** Deletes both files, whatever the outcome. */
	void cleanUp(const QString &requestPath, const Request &request);

	/* --- the new build's side --- */

	/**
	 * Reads the request, compares @p actualVersion against it, and writes the
	 * response. Returns false only when the exchange itself could not be
	 * carried out; a version mismatch is reported *in* the response, because
	 * the helper has to be told, not left waiting.
	 */
	bool respond(const QString &requestPath, const Version &actualVersion,
		bool initialisedSuccessfully, const QString &detail = QString());

	QString lastError() const { return m_lastError; }

private:
	bool fail(const QString &why) const;

	IFileSystem *m_fs;
	mutable QString m_lastError;
};

}; // namespace updater
}; // namespace pe_bear
