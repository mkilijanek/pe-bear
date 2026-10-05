#include "Installer.h"

using namespace pe_bear::updater;

QString Installer::outcomeToString(Outcome o)
{
	switch (o) {
		case AwaitingValidation: return QLatin1String("AwaitingValidation");
		case RefusedUntouched: return QLatin1String("RefusedUntouched");
		case FailedRolledBack: return QLatin1String("FailedRolledBack");
		case FailedNeedsAttention: return QLatin1String("FailedNeedsAttention");
		default: return QLatin1String("Invalid");
	}
}

Installer::Installer(IFileSystem *fs, PlatformInstaller *platform,
	TransactionJournal *journal, const UpdatePaths &paths)
	: m_fs(fs), m_platform(platform), m_journal(journal), m_paths(paths),
	m_tx(fs, journal)
{
}

bool Installer::refuse(const QString &reason)
{
	m_lastError = reason;
	return false;
}

Installer::Outcome Installer::abandon(const QString &reason)
{
	/* Called once the transaction is open, so there may be changes to undo.
	   The distinction between a completed and an incomplete rollback is the
	   difference between "nothing happened" and "a person needs to look", and
	   must not be collapsed. */
	m_lastError = reason;
	if (m_tx.rollBack(reason)) return FailedRolledBack;
	m_lastError = reason + QLatin1String(" | ") + m_tx.lastError();
	return FailedNeedsAttention;
}

Installer::Outcome Installer::prepareAndActivate(const VerifiedUpdate &update)
{
	m_lastError.clear();
	if (!m_fs || !m_platform || !m_journal) {
		refuse(QLatin1String("not wired up"));
		return RefusedUntouched;
	}
	if (!update.isValid()) {
		refuse(QLatin1String("the update is not verified"));
		return RefusedUntouched;
	}

	/* 1. Refused before anything is opened or touched. */
	QString why;
	if (!m_platform->canInstall(m_installation, &why)) {
		refuse(why);
		return RefusedUntouched;
	}
	m_targetDir = m_installation.installDir;
	if (m_targetDir.isEmpty()) {
		refuse(QLatin1String("the installation directory is not known"));
		return RefusedUntouched;
	}

	/* Which directory staging will use is decided here, as a decision and
	   nothing more: nothing is written to it yet, so a refusal above and below
	   this point still leaves the disk untouched. The root itself is created
	   only after the record exists -- see step 3. */
	const QString stagingRoot = m_paths.stagingDir();
	if (stagingRoot.isEmpty()) {
		refuse(QLatin1String("the update directory layout is incomplete"));
		return RefusedUntouched;
	}

	/* 2. A record exists before the first change, and it names the staging
	      root. Every path from here on -- including a crash in the very next
	      step, while the root is being created -- therefore ends in either an
	      untouched disk or a record that says exactly what to undo. */
	TransactionRecord seed;
	seed.targetDir = m_targetDir;
	seed.packagePath = update.packagePath;
	seed.packageSize = update.size;
	seed.packageSha256 = update.sha256;
	seed.fromVersion = Version::current().toString();
	seed.toVersion = update.candidate.release.version.toString();

	const QString txId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	const QString stagingDir = QDir::cleanPath(stagingRoot + QDir::separator() + txId);
	seed.stagingDir = stagingDir;
	seed.stagingRoot = stagingRoot;

	/* The backup goes beside the installation, not into the updater's private
	   directory under the user's data location.
	
	   Moving the installation aside is a rename, and a rename cannot cross a
	   filesystem. An installation on any volume other than the one holding the
	   user's data directory would therefore have failed to back up, and every
	   such update would have ended in a rollback -- the staging path already
	   carried a copy fallback for exactly this reason, and the backup path did
	   not. Beside the target it is the same volume by construction, so the
	   move stays atomic and no copy of the old build is ever made.
	
	   The parent is known writable: canInstall required it above. */
	const QString targetParent = parentDirectoryOf(m_targetDir);
	if (targetParent.isEmpty()) {
		refuse(QLatin1String("cannot determine where to keep the backup of ")
			+ QDir::toNativeSeparators(m_targetDir));
		return RefusedUntouched;
	}
	const QString backupDir = QDir::cleanPath(targetParent + QDir::separator()
		+ QLatin1String(UpdatePaths::BACKUP_DIR_NAME) + txId);

	if (!m_tx.begin(seed, txId)) {
		refuse(m_tx.lastError());
		return RefusedUntouched;
	}

	/* 3. The first filesystem change of the whole run, and it happens only
	      now -- after canInstall has agreed the installation may be touched,
	      and after a record naming this directory is on disk. A root that will
	      not create is rolled back like any other failure; the record is
	      closed rather than orphaned, and the installation was never touched.

	      Created through IFileSystem, not UpdatePaths::prepare(): that helper
	      predates the interface and talks to QDir directly, so calling it here
	      would route the installer around the very abstraction the design
	      rests on -- and would be untestable without touching the real
	      filesystem. */
	if (!m_fs->makeDir(stagingRoot)) {
		return abandon(QLatin1String("could not prepare the staging directory: ")
			+ m_fs->lastError());
	}
	m_fs->restrictToOwner(stagingRoot);

	/* 4. Staged while the installation is still untouched, so a package that
	      will not unpack costs nothing. */
	QList<TransactionOp> stagingOps;
	if (!m_platform->prepareStaging(update, stagingDir, &stagingOps)) {
		return abandon(QLatin1String("the package could not be unpacked: ")
			+ m_platform->lastError());
	}
	/* Recorded even though nothing is installed yet: the staging tree is real
	   and has to be cleaned up on any later failure. */
	if (!stagingOps.isEmpty() && !m_tx.markStagingOps(stagingOps)) {
		return abandon(QLatin1String("could not record the staged files: ") + m_tx.lastError());
	}

	/* 5. Still before the swap. A digest proves the bytes, not that they are
	      the right program. */
	QString layoutReason;
	if (!m_platform->verifyStagedLayout(stagingDir, &layoutReason)) {
		return abandon(QLatin1String("the unpacked package does not look like PE-bear: ")
			+ layoutReason);
	}

	/* 6. The installation moves aside. From here a failure means a rollback
	      that actually has to restore something. */
	if (!m_tx.backup(backupDir)) {
		return abandon(QLatin1String("could not move the installation aside: ")
			+ m_tx.lastError());
	}

	/* 7. Activation. The steps go into the journal before the state advances,
	      so an interruption leaves them undoable. */
	QList<TransactionOp> activationOps;
	if (!m_platform->activate(stagingDir, m_targetDir, &activationOps)) {
		return abandon(QLatin1String("could not put the new files in place: ")
			+ m_platform->lastError());
	}
	if (!m_tx.markActivated(activationOps)) {
		return abandon(QLatin1String("could not record activation: ") + m_tx.lastError());
	}

	return AwaitingValidation;
}

bool Installer::confirmValidated()
{
	if (m_tx.state() != TxActivated) {
		return refuse(QLatin1String("validation expects Activated, not ")
			+ transactionStateToString(m_tx.state()));
	}
	if (!m_tx.markValidated()) return refuse(m_tx.lastError());
	if (!m_tx.commit()) return refuse(m_tx.lastError());
	return true;
}

Installer::Outcome Installer::rollBack(const QString &reason)
{
	return abandon(reason);
}

QString Installer::installedExecutablePath() const
{
	if (m_targetDir.isEmpty() || !m_platform) return QString();
	return m_platform->executablePathIn(m_targetDir);
}
