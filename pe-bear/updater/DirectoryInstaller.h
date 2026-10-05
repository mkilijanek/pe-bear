#pragma once

#include <QtCore>
#include "PlatformInstaller.h"
#include "PackageExtractor.h"
#include "ArchiveTypes.h"
#include "FileSystem.h"

namespace pe_bear {
namespace updater {

/**
 * Replaces an installation that is one self-contained directory.
 *
 * This covers every shape PE-bear is actually shipped in that the updater is
 * allowed to touch: the Windows ZIP, the macOS bundle, and a Linux TAR.XZ
 * unpacked somewhere the user owns. A packaged install (deb, rpm, Homebrew,
 * winget) is refused here rather than handled, which is the same notify-only
 * rule the detector applies -- a package manager owns those files and the
 * updater must not fight it for them.
 *
 * What is deliberately *not* here: anything about ordering, backups or
 * rollback. Those live in Installer, which is platform-independent so that the
 * dangerous decisions can be driven through every failure point on any host.
 * This class only unpacks, checks what it unpacked, and moves it into place.
 *
 * Windows caveat, stated rather than hidden: on Windows another process can
 * hold a handle to a file inside the target and make the move fail with a
 * sharing violation -- an antivirus scanner or the search indexer will, given
 * the chance, and no amount of care on this side prevents it. A failed move is
 * reported and rolled back, never retried into a half-replaced directory. The
 * retry-and-back-off behaviour that a real Windows installer wants is left to
 * a WindowsInstaller subclass, because it cannot be written honestly without
 * being tested on Windows, and that testing is not mine to do.
 */
class DirectoryInstaller : public PlatformInstaller
{
public:
	/** File name of the application binary on this platform. */
	static QString applicationFileName();

	/**
	 * Every place the binary may sit relative to an installation root, most
	 * specific first. More than one because a macOS package may be the bundle
	 * directory or its contents, and guessing wrong installs the wrong thing.
	 */
	static QStringList executableCandidates();

	/** @param fs, @param reader borrowed; both must outlive this object */
	DirectoryInstaller(IFileSystem *fs, IArchiveReader *reader,
		const ExtractionPolicy &policy = ExtractionPolicy());

	virtual QString name() const;
	virtual bool canInstall(const InstallationInfo &installation, QString *reason) const;
	virtual bool prepareStaging(const VerifiedUpdate &update, const QString &stagingDir,
		QList<TransactionOp> *ops);
	virtual bool verifyStagedLayout(const QString &stagingDir, QString *reason) const;
	virtual bool activate(const QString &stagingDir, const QString &targetDir,
		QList<TransactionOp> *ops);
	virtual QString executablePathIn(const QString &dir) const;
	virtual QString lastError() const { return m_lastError; }

	/**
	 * The directory inside the staging tree that actually holds the build.
	 *
	 * Archives are published both ways -- contents at the root, or wrapped in
	 * a single top-level directory -- so the wrapper is unwrapped when it is
	 * unambiguous. Empty until prepareStaging has succeeded.
	 */
	QString stagedRoot() const { return m_stagedRoot; }

	/** Set when extraction was refused by the policy rather than by I/O. */
	ExtractionPolicy::Rejection rejection() const { return m_rejection; }

private:
	bool fail(const QString &why) const;

	/** Locates the build root under @p dir, unwrapping at most one level. */
	QString findBuildRoot(const QString &dir) const;
	/** First existing executable candidate under @p dir, or empty. */
	QString locateExecutable(const QString &dir) const;

	/** Same-volume move, falling back to a copy when the move is refused. */
	bool movePathOrCopy(const QString &from, const QString &to, QList<TransactionOp> *ops);
	bool copyTree(const QString &from, const QString &to);

	IFileSystem *m_fs;
	IArchiveReader *m_reader;
	ExtractionPolicy m_policy;

	QString m_stagedRoot;
	ExtractionPolicy::Rejection m_rejection;
	mutable QString m_lastError;
};

}; // namespace updater
}; // namespace pe_bear
