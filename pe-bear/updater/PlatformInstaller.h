#pragma once

#include <QtCore>
#include "UpdateTypes.h"
#include "TransactionTypes.h"
#include "InstallationDetector.h"

namespace pe_bear {
namespace updater {

/**
 * The steps that differ per operating system, and nothing else.
 *
 * Everything about *when* these run, in what order, and what happens when one
 * fails belongs to Installer, which is platform-independent and tested
 * natively. This interface is deliberately thin so that the part needing a
 * real Windows machine stays as small as it can be -- on Windows, replacing a
 * running installation is the one thing no other host can stand in for.
 *
 * Every method that changes the filesystem appends what it did to @p ops, in
 * the order it happened, so the transaction can undo it by replaying
 * backwards. A step that changes something without recording it is the one
 * failure mode the whole design is built to prevent.
 */
class PlatformInstaller
{
public:
	virtual ~PlatformInstaller() {}

	/** Human-readable name, for logs and for the journal. */
	virtual QString name() const = 0;

	/**
	 * Whether this installer can act on the given installation at all.
	 * A managed or non-writable installation is refused here, before any
	 * work starts, and the reason is reported rather than discovered later.
	 */
	virtual bool canInstall(const InstallationInfo &installation, QString *reason) const = 0;

	/** Unpacks the verified package into @p stagingDir. */
	virtual bool prepareStaging(const VerifiedUpdate &update, const QString &stagingDir,
		QList<TransactionOp> *ops) = 0;

	/**
	 * Checks that what was staged actually looks like PE-bear before anything
	 * is swapped. A package that verified by digest can still be the wrong
	 * package; finding that out after the old installation has been moved
	 * aside is strictly worse.
	 */
	virtual bool verifyStagedLayout(const QString &stagingDir, QString *reason) const = 0;

	/**
	 * Puts the staged files where the installation was. The old installation
	 * has already been moved aside by the transaction.
	 */
	virtual bool activate(const QString &stagingDir, const QString &targetDir,
		QList<TransactionOp> *ops) = 0;

	/** Absolute path of the application executable inside @p dir. */
	virtual QString executablePathIn(const QString &dir) const = 0;

	virtual QString lastError() const = 0;
};

}; // namespace updater
}; // namespace pe_bear
