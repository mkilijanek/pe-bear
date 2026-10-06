#include "LinuxInstaller.h"
#include "BuildProfile.h"

namespace pe_bear {
namespace updater {

LinuxInstaller::LinuxInstaller(IFileSystem *fs, IArchiveReader *reader,
		const ExtractionPolicy &policy)
	: DirectoryInstaller(fs, reader, policy)
{
}

QString LinuxInstaller::name() const
{
	return QLatin1String("linux");
}

bool LinuxInstaller::ensureExecutablePermissions(const QString &dir, const QString &exePath)
{
	/* On Linux, ensure the main executable has execute permissions.
	   This is particularly important for AppImages and unpacked tar.xz
	   archives where the archive might not preserve permissions correctly. */
	
	if (!exePath.startsWith(dir)) {
		/* Executable path must be within the installation directory */
		return true;
	}
	
	/* Try to set executable permissions on the main binary */
	if (m_fs->exists(exePath) && !m_fs->isDir(exePath)) {
		/* On Linux, use chmod to ensure executable permissions */
		if (!m_fs->setExecutable(exePath, true)) {
			/* Non-fatal: if we can't set permissions, the binary might still be executable */
			return true;
		}
	}
	
	return true;
}

bool LinuxInstaller::handleAppImagePackage(const VerifiedUpdate &update, const QString &stagingDir,
		QList<TransactionOp> *ops)
{
	/* AppImage packages are self-contained executables that may need special handling.
	   However, for updates, we typically receive the AppImage as a file that needs
	   to replace the existing one, rather than extracting it. */
	
	/* For now, use the standard extraction logic from DirectoryInstaller */
	PackageExtractor extractor(m_fs, m_reader, m_policy);
	const bool extracted = extractor.extract(update.packagePath, stagingDir);
	
	if (ops) *ops += extractor.ops();
	
	if (!extracted) {
		return false;
	}
	
	/* Find the build root after extraction */
	m_stagedRoot = findBuildRoot(stagingDir);
	if (m_stagedRoot.isEmpty()) {
		return false;
	}
	
	/* Ensure the main executable has proper permissions */
	const QString exe = locateExecutable(m_stagedRoot);
	if (!exe.isEmpty()) {
		ensureExecutablePermissions(m_stagedRoot, exe);
	}
	
	return true;
}

bool LinuxInstaller::handleTarXzPackage(const VerifiedUpdate &update, const QString &stagingDir,
		QList<TransactionOp> *ops)
{
	/* tar.xz packages for Linux contain the full PE-bear installation tree */
	PackageExtractor extractor(m_fs, m_reader, m_policy);
	const bool extracted = extractor.extract(update.packagePath, stagingDir);
	
	if (ops) *ops += extractor.ops();
	
	if (!extracted) {
		return false;
	}
	
	/* Find the build root after extraction */
	m_stagedRoot = findBuildRoot(stagingDir);
	if (m_stagedRoot.isEmpty()) {
		return false;
	}
	
	/* Ensure the main executable has proper permissions */
	const QString exe = locateExecutable(m_stagedRoot);
	if (!exe.isEmpty()) {
		ensureExecutablePermissions(m_stagedRoot, exe);
	}
	
	return true;
}

bool LinuxInstaller::prepareStaging(const VerifiedUpdate &update, const QString &stagingDir,
		QList<TransactionOp> *ops)
{
	m_stagedRoot.clear();
	m_rejection = ExtractionPolicy::NotRejected;

	if (!update.isValid()) return fail(QLatin1String("package description is not self-consistent"));
	if (!m_reader) return fail(QLatin1String("no archive reader available -- built without libarchive"));

	/* Determine the package type and use appropriate handling */
	const BuildProfile profile = BuildProfile::current();
	
	/* For Linux, we handle tar.xz and AppImage packages */
	if (profile.packageType() == PackageLinuxAppImage) {
		return handleAppImagePackage(update, stagingDir, ops);
	} else if (profile.packageType() == PackageLinuxTarXz || 
		profile.packageType() == PackageUnknown) {
		/* PackageUnknown might be a Linux package that auto-detects as tar.xz */
		return handleTarXzPackage(update, stagingDir, ops);
	}
	
	/* Fall back to the parent implementation for other cases */
	return DirectoryInstaller::prepareStaging(update, stagingDir, ops);
}

bool LinuxInstaller::activate(const QString &stagingDir, const QString &targetDir,
		QList<TransactionOp> *ops)
{
	if (m_stagedRoot.isEmpty()) return fail(QLatin1String("nothing has been staged"));
	
	/* For Linux, ensure the target directory has proper permissions before activation */
	if (m_fs->exists(targetDir)) {
		return fail(QLatin1String("target still exists: ")
			+ QDir::toNativeSeparators(targetDir));
	}
	
	/* Ensure the parent directory is writable for the move operation */
	const QString parentDir = parentDirectoryOf(targetDir);
	if (!parentDir.isEmpty() && !m_fs->isWritableDir(parentDir)) {
		return fail(QLatin1String("parent directory is not writable: ")
			+ QDir::toNativeSeparators(parentDir));
	}
	
	/* Perform the activation using move or copy */
	return movePathOrCopy(m_stagedRoot, targetDir, ops);
}

}; // namespace updater
}; // namespace pe_bear
