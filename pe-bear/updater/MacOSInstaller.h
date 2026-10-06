#pragma once

#include "DirectoryInstaller.h"

namespace pe_bear {
namespace updater {

/**
 * macOS-specific installer for PE-bear updates.
 *
 * Handles macOS application bundles (.app) and ensures proper bundle structure
 * and permissions. macOS applications are typically distributed as .app bundles
 * or as zip archives containing the bundle.
 */
class MacOSInstaller : public DirectoryInstaller
{
public:
	/** @param fs, @param reader borrowed; both must outlive this object */
	MacOSInstaller(IFileSystem *fs, IArchiveReader *reader,
		const ExtractionPolicy &policy = ExtractionPolicy());

	virtual QString name() const override;
	virtual bool prepareStaging(const VerifiedUpdate &update, const QString &stagingDir,
		QList<TransactionOp> *ops) override;
	virtual bool verifyStagedLayout(const QString &stagingDir, QString *reason) const override;
	virtual QString executablePathIn(const QString &dir) const override;

protected:
	/** Ensures the .app bundle structure is correct. */
	bool verifyAppBundleStructure(const QString &stagingDir, QString *reason) const;

	/** Handles macOS .app bundle specific staging. */
	bool handleAppBundlePackage(const VerifiedUpdate &update, const QString &stagingDir,
		QList<TransactionOp> *ops);

	/** Locates the actual executable within a .app bundle or at the root. */
	QString findExecutableInBundle(const QString &dir) const;
};

}; // namespace updater
}; // namespace pe_bear
