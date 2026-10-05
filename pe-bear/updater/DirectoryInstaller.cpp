#include "DirectoryInstaller.h"

namespace pe_bear {
namespace updater {

QString DirectoryInstaller::applicationFileName()
{
#if defined(Q_OS_WIN)
	return QLatin1String("PE-bear.exe");
#else
	return QLatin1String("PE-bear");
#endif
}

QStringList DirectoryInstaller::executableCandidates()
{
	QStringList candidates;
	const QString exe = applicationFileName();
#if defined(Q_OS_MAC)
	/* A macOS package may be the bundle's parent, or the bundle unpacked so
	   that the binary sits directly in the root. Both are published shapes. */
	candidates << (QLatin1String("PE-bear.app/Contents/MacOS/") + exe);
	candidates << exe;
#else
	candidates << exe;
#endif
	return candidates;
}

DirectoryInstaller::DirectoryInstaller(IFileSystem *fs, IArchiveReader *reader,
		const ExtractionPolicy &policy)
	: m_fs(fs), m_reader(reader), m_policy(policy),
	m_rejection(ExtractionPolicy::NotRejected)
{
}

QString DirectoryInstaller::name() const
{
	return QLatin1String("directory");
}

bool DirectoryInstaller::fail(const QString &why) const
{
	m_lastError = why;
	return false;
}

bool DirectoryInstaller::canInstall(const InstallationInfo &installation, QString *reason) const
{
	/* isUpdatable() already covers managed, unknown and non-writable. It is
	   asked first so the refusal matches what the GUI told the user, rather
	   than being decided twice in two places that could drift apart. */
	if (!installation.isUpdatable()) {
		if (reason) {
			*reason = QLatin1String("installation is not updatable: ")
				+ installationKindToString(installation.kind)
				+ (installation.writable ? QLatin1String("") : QLatin1String(", not writable"));
		}
		return false;
	}
	if (installation.installDir.isEmpty()) {
		if (reason) *reason = QLatin1String("installation directory is unknown");
		return false;
	}
	if (!m_fs->isDir(installation.installDir)) {
		if (reason) *reason = QLatin1String("installation directory is not a directory: ")
			+ QDir::toNativeSeparators(installation.installDir);
		return false;
	}
	/* The parent has to be writable too: activation replaces the directory
	   itself by moving it, which is a change to its parent, not to it. A
	   writable directory inside a read-only parent would pass every check and
	   then fail at the one step that cannot be undone cheaply. */
	const QString parent = parentDirectoryOf(installation.installDir);
	if (!m_fs->isWritableDir(parent)) {
		if (reason) *reason = QLatin1String("parent directory is not writable: ")
			+ QDir::toNativeSeparators(parent);
		return false;
	}
	if (locateExecutable(installation.installDir).isEmpty()) {
		if (reason) *reason = QLatin1String("no ") + applicationFileName()
			+ QLatin1String(" in ") + QDir::toNativeSeparators(installation.installDir);
		return false;
	}
	return true;
}

QString DirectoryInstaller::locateExecutable(const QString &dir) const
{
	const QStringList candidates = executableCandidates();
	for (int i = 0; i < candidates.size(); i++) {
		const QString path = dir + QLatin1Char('/') + candidates.at(i);
		if (m_fs->exists(path) && !m_fs->isDir(path)) return path;
	}
	return QString();
}

QString DirectoryInstaller::executablePathIn(const QString &dir) const
{
	const QString found = locateExecutable(dir);
	if (!found.isEmpty()) return found;
	/* Nothing there yet -- name the primary candidate so a caller can report
	   what was missing rather than an empty string. */
	return dir + QLatin1Char('/') + executableCandidates().first();
}

QString DirectoryInstaller::findBuildRoot(const QString &dir) const
{
	if (!locateExecutable(dir).isEmpty()) return dir;

	/* Exactly one level of unwrapping, and only when it is unambiguous.
	   Searching deeper would mean guessing which of several directories is
	   the build, and installing the wrong one is worse than refusing. */
	const QStringList entries = m_fs->listDir(dir);
	if (entries.size() != 1) return QString();

	const QString only = dir + QLatin1Char('/') + entries.first();
	if (!m_fs->isDir(only)) return QString();
	if (locateExecutable(only).isEmpty()) return QString();
	return only;
}

bool DirectoryInstaller::prepareStaging(const VerifiedUpdate &update, const QString &stagingDir,
		QList<TransactionOp> *ops)
{
	m_stagedRoot.clear();
	m_rejection = ExtractionPolicy::NotRejected;

	if (!update.isValid()) return fail(QLatin1String("package description is not self-consistent"));
	if (!m_reader) return fail(QLatin1String("no archive reader available -- built without libarchive"));

	PackageExtractor extractor(m_fs, m_reader, m_policy);
	const bool extracted = extractor.extract(update.packagePath, stagingDir);

	/* The steps are taken whether or not extraction finished: a partial tree
	   still has to be undoable, and PackageExtractor keeps what it completed
	   precisely so that it can be. */
	if (ops) *ops += extractor.ops();

	if (!extracted) {
		m_rejection = extractor.rejection();
		return fail(extractor.lastError());
	}

	m_stagedRoot = findBuildRoot(stagingDir);
	if (m_stagedRoot.isEmpty()) {
		return fail(QLatin1String("the package does not contain ") + applicationFileName()
			+ QLatin1String(" at its root or in a single top-level directory"));
	}
	return true;
}

bool DirectoryInstaller::verifyStagedLayout(const QString &stagingDir, QString *reason) const
{
	if (m_stagedRoot.isEmpty()) {
		if (reason) *reason = QLatin1String("nothing has been staged");
		return false;
	}
	/* Re-derived rather than trusted: prepareStaging and this run either side
	   of a step that writes to disk, and a staged root that has stopped being
	   under the staging directory is exactly what this exists to catch. */
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

	const QString exe = locateExecutable(m_stagedRoot);
	if (exe.isEmpty()) {
		if (reason) *reason = QLatin1String("staged build has no ") + applicationFileName();
		return false;
	}
	if (m_fs->fileSize(exe) <= 0) {
		if (reason) *reason = QLatin1String("staged ") + applicationFileName()
			+ QLatin1String(" is empty");
		return false;
	}
	return true;
}

bool DirectoryInstaller::copyTree(const QString &from, const QString &to)
{
	/* Checked before anything is created. Without this, a source that is gone
	   makes the whole copy "succeed": the directory is created, there is
	   nothing to iterate, and the caller is told an empty tree is the new
	   build. That is the one failure shape no later step can tell from a
	   finished install, and it is how a staged tree destroyed by the backup
	   step came back as a successful activation. */
	if (!m_fs->exists(from) || !m_fs->isDir(from)) {
		return fail(QLatin1String("nothing to copy from ")
			+ QDir::toNativeSeparators(from));
	}
	if (!m_fs->makeDir(to)) return fail(m_fs->lastError());

	const QStringList entries = m_fs->listDir(from);
	for (int i = 0; i < entries.size(); i++) {
		const QString src = from + QLatin1Char('/') + entries.at(i);
		const QString dst = to + QLatin1Char('/') + entries.at(i);
		if (m_fs->isDir(src)) {
			if (!copyTree(src, dst)) return false;
		} else if (!m_fs->copyFile(src, dst)) {
			return fail(m_fs->lastError());
		}
	}
	return true;
}

bool DirectoryInstaller::movePathOrCopy(const QString &from, const QString &to,
		QList<TransactionOp> *ops)
{
	if (m_fs->movePath(from, to)) {
		if (ops) *ops << TransactionOp(TransactionOp::OpMoved, from, to);
		return true;
	}

	const QString moveError = m_fs->lastError();

	/* A move can be refused for two quite different reasons, and only one of
	   them is worth a fallback. If the source is not there, the staged build
	   is gone and copying cannot conjure it back -- reporting that plainly is
	   the only honest answer. */
	if (!m_fs->exists(from)) {
		return fail(QLatin1String("the staged build is no longer at ")
			+ QDir::toNativeSeparators(from) + QLatin1String(": ") + moveError);
	}

	/* Otherwise: staging is chosen on the installation's own volume so that
	   this is a rename, but UpdatePaths falls back to the private root when
	   the parent of the installation is not writable, and that can be another
	   filesystem. Copying is slower and briefly needs room for both copies;
	   failing outright instead would make the fallback path useless. */

	/* Recorded before the copy starts, so a crash midway still leaves
	   something for recovery to remove. */
	if (ops) *ops << TransactionOp(TransactionOp::OpCreatedDir, to, QString());
	if (!copyTree(from, to)) {
		return fail(QLatin1String("move failed (") + moveError
			+ QLatin1String(") and the copy fallback failed: ") + m_lastError);
	}
	return true;
}

bool DirectoryInstaller::activate(const QString &stagingDir, const QString &targetDir,
		QList<TransactionOp> *ops)
{
	Q_UNUSED(stagingDir);

	if (m_stagedRoot.isEmpty()) return fail(QLatin1String("nothing has been staged"));
	if (m_fs->exists(targetDir)) {
		/* The transaction moves the old installation aside before calling
		   this. Something still here means the backup step did not do what
		   the record says it did, and writing into it would merge two builds
		   into one directory -- the state no later step could tell from a
		   clean install. */
		return fail(QLatin1String("target still exists: ")
			+ QDir::toNativeSeparators(targetDir));
	}
	return movePathOrCopy(m_stagedRoot, targetDir, ops);
}

}; // namespace updater
}; // namespace pe_bear
