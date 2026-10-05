#pragma once

#include <QtCore>
#include "HelperHandoff.h"
#include "PlatformInstaller.h"
#include "ProcessControl.h"
#include "StartupHandshake.h"
#include "Installer.h"
#include "UpdatePaths.h"
#include "FileSystem.h"

namespace pe_bear {
namespace updater {

/**
 * The separate process that actually replaces the installation.
 *
 * It exists because of one fact about Windows: a running executable's image
 * cannot be renamed or deleted. PE-bear therefore cannot update itself in
 * place, no matter how carefully it tries -- something that is not PE-bear has
 * to do it, from outside the directory being replaced.
 *
 * Everything else about this class follows from being that something:
 *
 *  - It re-verifies rather than trusts. The digest is recomputed here, in this
 *    process, from the file on disk as it is now. PE-bear verified the package
 *    when it downloaded it, but between then and now the file sat in a
 *    directory for some period of time, and "it was fine when we checked"
 *    is not a property of a file. The paths are canonicalised here too, for
 *    the same reason.
 *  - It never forces PE-bear to quit. It waits, bounded, and refuses if the
 *    wait runs out. The alternative is terminating a process that may hold
 *    unsaved work -- the user's work, which no updater gets to discard. There
 *    is deliberately no code here capable of ending another process.
 *  - It never elevates. A target the user cannot write to is refused and
 *    reported, not escalated. Prompting for administrator rights from an
 *    update check is how an updater becomes the most attractive thing on the
 *    machine to compromise.
 *  - It commits only on proof. The new build has to answer the handshake with
 *    the right nonce and the right version. Elapsed time is not evidence: a
 *    build that starts and crashes a second later has validated nothing, and
 *    committing on a timer would destroy the only working copy.
 *
 * The order of the refusals matters as much as the refusals. Everything that
 * can be checked without changing anything is checked first, so that a bad
 * instruction, a tampered package or a target that should not be touched all
 * end with the installation exactly as it was.
 */
class UpdateHelper
{
public:
	enum Result {
		/** Installed, validated, committed. */
		Succeeded = 0,
		/** The instruction was malformed. Nothing was touched. */
		RefusedInvalidRequest,
		/** The instruction was too old to act on. Nothing was touched. */
		RefusedStaleRequest,
		/** The package is not the one described. Nothing was touched. */
		RefusedPackageMismatch,
		/** The target must not be, or cannot be, replaced. Nothing was touched. */
		RefusedTarget,
		/** PE-bear was still running when the wait ran out. Nothing was touched. */
		RefusedParentStillRunning,
		/** Something failed and the previous installation was restored. */
		RolledBack,
		/** Something failed and the restore did not finish. Needs a person. */
		NeedsAttention,
		/** A fault in the helper itself. */
		InternalError,
		RESULTS_COUNT
	};

	static QString resultToString(Result r);
	static QString resultMessage(Result r);
	/** Process exit code for @p r. Stable: PE-bear reads these back. */
	static int resultToExitCode(Result r);
	/** True when the installation is known to be exactly as it was. */
	static bool leftUntouched(Result r);

	struct Limits
	{
		Limits()
			: parentExitTimeoutMs(90 * 1000),
			parentPollIntervalMs(200),
			handshakeTimeoutMs(120 * 1000) {}

		/** How long to wait for PE-bear to close by itself. */
		int parentExitTimeoutMs;
		int parentPollIntervalMs;
		/**
		 * How long the newly installed build gets to answer. Generous on
		 * purpose: a first start after an update competes with an antivirus
		 * scanner reading every file that just appeared, and a timeout here
		 * rolls back an update that was in fact fine.
		 */
		int handshakeTimeoutMs;
	};

	/** @param fs, @param platform, @param probe, @param launcher borrowed */
	UpdateHelper(IFileSystem *fs, PlatformInstaller *platform, IProcessProbe *probe,
		IProcessLauncher *launcher, const UpdatePaths &paths,
		const Limits &limits = Limits());
	virtual ~UpdateHelper() {}

	/** Carries out @p handoff, start to finish. */
	Result run(const HelperHandoff &handoff);

	/** Every step taken, in order, for the log file and for the user. */
	QStringList journal() const { return m_journal; }
	QString lastError() const { return m_lastError; }

	/** Set once a transaction was opened; empty when nothing was touched. */
	QString transactionId() const { return m_transactionId; }

	/** Overridden in tests only. */
	void setNow(const QDateTime &now) { m_now = now; }

protected:
	/**
	 * SHA-256 of @p path, streamed.
	 *
	 * Virtual purely as a seam for tests. It cannot go through IFileSystem:
	 * that interface reads a whole file into memory, and a package is
	 * routinely hundreds of megabytes. Streaming is the point of
	 * PackageVerifier, so the real implementation calls it.
	 */
	virtual QString computeDigest(const QString &path) const;

private:
	Result refuse(Result r, const QString &why);
	void note(const QString &what);

	/** Steps 1 and 2: the instruction itself. */
	Result checkRequest(const HelperHandoff &handoff);
	/** Step 3: the bytes on disk, re-hashed here. */
	Result checkPackage(const HelperHandoff &handoff);
	/** Step 4: what is about to be replaced. */
	Result checkTarget(const HelperHandoff &handoff, InstallationInfo *info);
	/** Step 5: wait, without ever forcing it. */
	Result awaitParentExit(const HelperHandoff &handoff);
	/** Steps 7 and 8: launch the new build and judge its answer. */
	Result validateInstalled(Installer &installer, const HelperHandoff &handoff);

	static VerifiedUpdate describeUpdate(const HelperHandoff &handoff);

	IFileSystem *m_fs;
	PlatformInstaller *m_platform;
	IProcessProbe *m_probe;
	IProcessLauncher *m_launcher;
	UpdatePaths m_paths;
	Limits m_limits;

	QDateTime m_now;
	QStringList m_journal;
	QString m_lastError;
	QString m_transactionId;
};

}; // namespace updater
}; // namespace pe_bear
