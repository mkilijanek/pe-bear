#pragma once

#include <QtCore>

namespace pe_bear {
namespace updater {

/**
 * Private working area of the updater.
 *
 * Downloads, staging and transaction records are kept apart so that no step
 * can ever overwrite the inputs of another, and every run gets its own
 * randomly named subdirectory. Everything is created owner-only: a package
 * waiting to be installed must not be writable by anyone else between
 * verification and installation.
 *
 * Backups are the exception: they live beside the installation (see
 * @p BACKUP_DIR_NAME), on its own volume, because moving the installation
 * aside is a rename and a rename cannot cross a filesystem.
 */
class UpdatePaths
{
public:
	static const char* DIR_NAME;
	static const char* STAGING_DIR_NAME;
	/**
	 * Prefix of the per-transaction backup directory, placed beside the
	 * installation being replaced: "<install parent>/.PE-bear-backup-<txId>".
	 *
	 * Defined here and not spelled out in Installer because the name is a
	 * convention shared by more than one place that reads the installation's
	 * neighbourhood -- a person cleaning up a failed update by hand is one of
	 * them -- and conventions that live only at their single point of use
	 * cannot be discovered.
	 */
	static const char* BACKUP_DIR_NAME;
	/**
	 * Folder shared by PE-bear and the helper.
	 *
	 * Fixed rather than taken from QCoreApplication::applicationName(),
	 * because the two executables have different names and must still agree
	 * on where the package and the instructions live.
	 */
	static const char* APPLICATION_DIR_NAME;

	/** Per-user application data location, outside the installation. */
	static QString defaultRoot();

	/**
	 * Staging is preferred on the same volume as the installation so that the
	 * installer can replace files by rename rather than by copy. Falls back to
	 * the private root when the installation directory is not writable.
	 */
	static QString preferredStagingRoot(const QString &installDir, const QString &fallbackRoot);

	static bool restrictToOwner(const QString &path);
	static bool removeRecursively(const QString &path);

	UpdatePaths();
	UpdatePaths(const QString &root, const QString &stagingRoot);

	/** Creates the directory tree. Returns false and sets @p error on failure. */
	bool prepare(QString *error = NULL);

	QString root() const { return m_root; }
	QString downloadsDir() const;
	QString stagingDir() const;
	QString transactionsDir() const;
	QString logFilePath() const;

	/** Creates and returns a fresh, randomly named directory under downloads. */
	QString createDownloadDir(QString *error = NULL) const;

private:
	static QString randomName();

	QString m_root;
	QString m_stagingRoot;
};

}; // namespace updater
}; // namespace pe_bear
