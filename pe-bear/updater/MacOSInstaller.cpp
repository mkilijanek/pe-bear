#include "MacOSInstaller.h"
#include "FileSystem.h"

namespace pe_bear {
namespace updater {

MacOSInstaller::MacOSInstaller(IFileSystem *fs, IArchiveReader *reader,
		const ExtractionPolicy &policy)
	: DirectoryInstaller(fs, reader, policy)
{
}

QString MacOSInstaller::name() const
{
	return QLatin1String("macos");
}

QString MacOSInstaller::findExecutableInBundle(const QString &dir) const
{
	/* macOS applications can be structured in two ways:
	 * 1. As a .app bundle: PE-bear.app/Contents/MacOS/PE-bear
	 * 2. As a bare executable: PE-bear
	 */
	
	/* First, check for .app bundle structure */
	const QStringList bundleCandidates = QStringList()
		<< (dir + QLatin1String("/PE-bear.app/Contents/MacOS/PE-bear"))
		<< (dir + QLatin1String("/Contents/MacOS/PE-bear"));
	
	for (const QString &candidate : bundleCandidates) {
		if (m_fs->exists(candidate) && !m_fs->isDir(candidate)) {
			return candidate;
		}
	}
	
	/* Fall back to the parent implementation for non-bundle layouts */
	return locateExecutable(dir);
}

QString MacOSInstaller::executablePathIn(const QString &dir) const
{
	const QString found = findExecutableInBundle(dir);
	if (!found.isEmpty()) return found;
	
	/* Nothing there yet -- name the primary candidate so a caller can report
	   what was missing rather than an empty string. */
	return dir + QLatin1String("/PE-bear.app/Contents/MacOS/PE-bear");
}

bool MacOSInstaller::verifyAppBundleStructure(const QString &stagingDir, QString *reason) const
{
	/* If we have a .app bundle, verify its structure */
	const QString appBundle = stagingDir + QLatin1String("/PE-bear.app");
	const QString contentsDir = appBundle + QLatin1String("/Contents");
	const QString macOSDir = contentsDir + QLatin1String("/MacOS");
	const QString executable = macOSDir + QLatin1String("/PE-bear");
	
	/* Check if .app bundle exists */
	if (m_fs->exists(appBundle) && m_fs->isDir(appBundle)) {
		/* Verify bundle structure */
		if (!m_fs->isDir(contentsDir)) {
			if (reason) *reason = QLatin1String("invalid .app bundle: missing Contents directory");
			return false;
		}
		if (!m_fs->isDir(macOSDir)) {
			if (reason) *reason = QLatin1String("invalid .app bundle: missing MacOS directory");
			return false;
		}
		if (!m_fs->exists(executable) || m_fs->isDir(executable)) {
			if (reason) *reason = QLatin1String("invalid .app bundle: missing or invalid executable");
			return false;
		}
		return true;
	}
	
	/* If no .app bundle, that's fine - might be a bare executable */
	return true;
}

bool MacOSInstaller::handleAppBundlePackage(const VerifiedUpdate &update, const QString &stagingDir,
		QList<TransactionOp> *ops)
{
	/* macOS packages are typically zip archives containing either:
	 * 1. A .app bundle
	 * 2. The bare application files
	 */
	
	PackageExtractor extractor(m_fs, m_reader, m_policy);
	const bool extracted = extractor.extract(update.packagePath, stagingDir);
	
	if (ops) *ops += extractor.ops();
	
	if (!extracted) {
		m_rejection = extractor.rejection();
		return fail(extractor.lastError());
	}
	
	/* Find the build root - this could be the .app bundle or the directory containing PE-bear */
	m_stagedRoot = findBuildRoot(stagingDir);
	if (m_stagedRoot.isEmpty()) {
		/* Try to find a .app bundle directly in the staging directory */
		const QStringList entries = m_fs->listDir(stagingDir);
		for (const QString &entry : entries) {
			const QString fullPath = stagingDir + QLatin1Char('/') + entry;
			if (m_fs->isDir(fullPath) && entry.endsWith(QLatin1String(".app"))) {
				m_stagedRoot = fullPath;
				break;
			}
		}
		
		if (m_stagedRoot.isEmpty()) {
			return fail(QLatin1String("the package does not contain PE-bear at its root or in a single top-level directory"));
		}
	}
	
	return true;
}

bool MacOSInstaller::prepareStaging(const VerifiedUpdate &update, const QString &stagingDir,
		QList<TransactionOp> *ops)
{
	m_stagedRoot.clear();
	m_rejection = ExtractionPolicy::NotRejected;

	if (!update.isValid()) return fail(QLatin1String("package description is not self-consistent"));
	if (!m_reader) return fail(QLatin1String("no archive reader available -- built without libarchive"));

	/* For macOS, use the app bundle handling */
	return handleAppBundlePackage(update, stagingDir, ops);
}

bool MacOSInstaller::verifyStagedLayout(const QString &stagingDir, QString *reason) const
{
	if (m_stagedRoot.isEmpty()) {
		if (reason) *reason = QLatin1String("nothing has been staged");
		return false;
	}
	
	/* Re-derived rather than trusted */
	const QString canonicalStaging = m_fs->canonicalPath(stagingDir);
	const QString canonicalRoot = m_fs->canonicalPath(m_stagedRoot);
	if (canonicalStaging.isEmpty() || canonicalRoot.isEmpty()) {
		if (reason) *reason = QLatin1String("staged layout could not be resolved");
		return false;
	}
	if (canonicalRoot != canonicalStaging
		&& !canonicalRoot.startsWith(canonicalStaging + QLatin1Char('/')))
	{
		if (reason) *reason = QLatin1String("staged build left the staging directory");
		return false;
	}

	/* Verify .app bundle structure if present */
	if (!verifyAppBundleStructure(stagingDir, reason)) {
		return false;
	}

	const QString exe = findExecutableInBundle(m_stagedRoot);
	if (exe.isEmpty()) {
		if (reason) *reason = QLatin1String("staged build has no PE-bear executable");
		return false;
	}
	if (m_fs->fileSize(exe) <= 0) {
		if (reason) *reason = QLatin1String("staged PE-bear is empty");
		return false;
	}
	return true;
}

}; // namespace updater
}; // namespace pe_bear
