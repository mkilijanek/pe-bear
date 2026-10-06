#pragma once

#include "DirectoryInstaller.h"

namespace pe_bear {
namespace updater {

/**
 * Linux-specific installer for PE-bear updates.
 *
 * Handles Linux packaging shapes: tar.xz archives and AppImage files.
 * Ensures proper file permissions, symlink handling, and desktop integration
 * considerations specific to Linux environments.
 */
class LinuxInstaller : public DirectoryInstaller
{
public:
	/** @param fs, @param reader borrowed; both must outlive this object */
	LinuxInstaller(IFileSystem *fs, IArchiveReader *reader,
		const ExtractionPolicy &policy = ExtractionPolicy());

	virtual QString name() const override;
	virtual bool prepareStaging(const VerifiedUpdate &update, const QString &stagingDir,
		QList<TransactionOp> *ops) override;
	virtual bool activate(const QString &stagingDir, const QString &targetDir,
		QList<TransactionOp> *ops) override;

protected:
	/** Ensures the PE-bear executable has proper executable permissions. */
	bool ensureExecutablePermissions(const QString &dir, const QString &exePath);

	/** Handles AppImage-specific staging considerations. */
	bool handleAppImagePackage(const VerifiedUpdate &update, const QString &stagingDir,
		QList<TransactionOp> *ops);

	/** Handles tar.xz-specific staging considerations. */
	bool handleTarXzPackage(const VerifiedUpdate &update, const QString &stagingDir,
		QList<TransactionOp> *ops);
};

}; // namespace updater
}; // namespace pe_bear
