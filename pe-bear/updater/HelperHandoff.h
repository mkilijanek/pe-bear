#pragma once

#include <QtCore>
#include "Version.h"

namespace pe_bear {
namespace updater {

/**
 * What PE-bear tells the helper to do, and the only thing it tells it.
 *
 * A file rather than command-line arguments, for two reasons that are about
 * other users on the machine rather than about convenience:
 *
 *  - a command line is world-readable on every platform this runs on, so
 *    passing the package path and the expected digest that way would publish
 *    both to anyone who can run `ps`, including the window between the helper
 *    starting and it re-verifying the file;
 *  - the file is created owner-only in the updater's private directory, so
 *    another user cannot substitute the instructions, only read them if they
 *    already have the user's own access.
 *
 * What this is *not* is a privilege boundary. The helper runs as whoever
 * invoked it and never elevates -- that is a rule of the design, not an
 * oversight -- so it can do nothing its invoker could not already do by hand.
 * There is therefore no secret on the command line and no attempt to
 * authenticate the caller: that would be theatre. The checks that do carry
 * weight are the ones the helper performs itself, against the filesystem: the
 * digest is recomputed, the paths are canonicalised, the target is confirmed
 * to be a real updatable installation, and a stale instruction is refused by
 * age.
 *
 * The run id carries no security weight at all -- the handshake generates its
 * own nonce, as it must, since a value that travelled through this file could
 * have been read along with everything else in it. The id exists so that the
 * files and journal entry of one attempt cannot be confused with another's.
 */
struct HelperHandoff
{
	/** Bumped when the meaning of a field changes, never for additions. */
	static const int CURRENT_VERSION = 1;

	/** How long an instruction stays usable. Minutes, not hours: this is
	    written immediately before the helper is started, and anything older
	    is the residue of an attempt that did not finish. */
	static const int MAX_AGE_SECONDS = 15 * 60;

	/** Length of the run id, in hex characters. */
	static const int RUN_ID_HEX_LENGTH = 32;

	HelperHandoff()
		: version(CURRENT_VERSION), packageSize(0), parentPid(0), relaunch(true) {}

	int version;

	/** Names this attempt's files and journal entry. 32 hex characters. */
	QString runId;

	/** Absolute path of the verified package. */
	QString packagePath;
	/** Lowercase 64-hex SHA-256 the helper must recompute and match. */
	QString packageSha256;
	qint64 packageSize;

	/** Installation directory to be replaced, as PE-bear resolved it. */
	QString targetDir;
	/** Version expected to answer the handshake once installed. */
	QString expectedVersion;

	/** Process to wait for before anything is touched. */
	qint64 parentPid;
	/** Start PE-bear again once the update is committed. */
	bool relaunch;

	/** UTC, ISO-8601. Used only to refuse stale instructions. */
	QString createdAtUtc;

	/* Where the package came from. Recorded, never fetched -- the helper does
	   no networking at all, and an update that went wrong is much easier to
	   explain when the record says which asset it was. Required and required
	   to be https, so that a handoff naming a plain-http origin is refused
	   even though nothing here would dereference it. */
	QString assetUrl;
	QString releaseTag;
	QString assetName;

	/** Shape only: says nothing about whether the paths or digest are real. */
	bool isValid() const;

	/**
	 * Whether createdAtUtc can be read at all.
	 *
	 * Separate from ageSeconds because the two questions have different
	 * answers and must not share one: an unreadable timestamp is a malformed
	 * instruction, while an age of -1 is an instruction written a second ago
	 * by a clock a second ahead -- which is ordinary. Folding both into a
	 * single return value is how the tolerance for clock skew ends up
	 * unreachable.
	 */
	bool hasUsableTimestamp() const;

	/** Seconds since createdAtUtc; negative when it is dated ahead of @p now.
	    Only meaningful when hasUsableTimestamp(). */
	qint64 ageSeconds(const QDateTime &now) const;

	Version expected() const { return Version::fromString(expectedVersion); }

	QByteArray toJson() const;
	static HelperHandoff fromJson(const QByteArray &data, bool *ok = NULL);

	/** A fresh id of RUN_ID_HEX_LENGTH hex characters. */
	static QString generateRunId();

	/** File name used inside the updater's private directory. */
	static QString fileName();
};

}; // namespace updater
}; // namespace pe_bear
