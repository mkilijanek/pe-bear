#pragma once

#include <QtCore>
#include "PlatformInstaller.h"
#include "Transaction.h"
#include "TransactionJournal.h"
#include "FileSystem.h"
#include "UpdatePaths.h"

namespace pe_bear {
namespace updater {

/**
 * Orchestrates an installation: decides the order of the steps, and what to do
 * when one of them fails.
 *
 * Platform-independent on purpose. Everything OS-specific sits behind
 * PlatformInstaller, so the ordering -- which is where the damage lives if it
 * is wrong -- can be driven through every failure point on any host.
 *
 * The sequence, and the reason for it:
 *
 *   1. refuse outright if the installation must not be touched
 *   2. open the transaction, so a record exists before anything changes
 *   3. stage the package, while the installation is still untouched
 *   4. check the staged layout, still before anything is swapped
 *   5. move the installation aside
 *   6. activate, recording the steps first
 *   7. hand back to the caller for the startup handshake
 *
 * Steps 3 and 4 come before 5 deliberately. A package that fails to unpack, or
 * unpacks into something that is not PE-bear, must be discovered while the
 * working installation is still in place -- not after it has been moved.
 */
class Installer
{
public:
	/** What the caller must do next, once prepareAndActivate returns. */
	enum Outcome {
		/** Activated; the new build must now confirm itself. */
		AwaitingValidation = 0,
		/** Refused before anything changed. */
		RefusedUntouched,
		/** Failed, and the previous installation was restored. */
		FailedRolledBack,
		/** Failed, and rollback did not complete. Needs a person. */
		FailedNeedsAttention
	};

	static QString outcomeToString(Outcome o);

	/** @param fs, @param platform, @param journal borrowed; must outlive this */
	Installer(IFileSystem *fs, PlatformInstaller *platform, TransactionJournal *journal,
		const UpdatePaths &paths);

	void setInstallation(const InstallationInfo &info) { m_installation = info; }

	/**
	 * Runs steps 1 to 6. On AwaitingValidation the new build is in place and
	 * the transaction is in Activated; the caller launches it and then calls
	 * confirmValidated or rollBack.
	 */
	Outcome prepareAndActivate(const VerifiedUpdate &update);

	/** The new build confirmed itself: finish and discard the backup. */
	bool confirmValidated();

	/** The new build did not confirm itself: restore the previous one. */
	Outcome rollBack(const QString &reason);

	/** Path to launch for the startup handshake. Empty before activation. */
	QString installedExecutablePath() const;

	Transaction& transaction() { return m_tx; }
	QString lastError() const { return m_lastError; }

private:
	Outcome abandon(const QString &reason);
	bool refuse(const QString &reason);

	IFileSystem *m_fs;
	PlatformInstaller *m_platform;
	TransactionJournal *m_journal;
	UpdatePaths m_paths;
	InstallationInfo m_installation;

	Transaction m_tx;
	QString m_targetDir;
	QString m_lastError;
};

}; // namespace updater
}; // namespace pe_bear
