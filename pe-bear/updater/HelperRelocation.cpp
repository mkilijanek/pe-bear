#include "HelperRelocation.h"

#if defined(Q_OS_WIN)
	#include <windows.h>
	#include <psapi.h>
#elif defined(Q_OS_MAC)
	#include <mach-o/dyld.h>
#elif defined(Q_OS_UNIX)
	#include <link.h>
#endif

using namespace pe_bear::updater;

const char* HelperRelocation::RELOCATED_FLAG = "--relocated";
const char* HelperRelocation::RELOCATED_FROM_FLAG = "--relocated-from";

namespace {

	/* "dir/" is the prefix, so that "C:/x/pe-bear-old/a" is not under
	   "C:/x/pe-bear" and the directory itself is not under itself. */
	bool isUnder(const QString &path, const QString &dir)
	{
		if (dir.isEmpty() || path.isEmpty()) return false;
		QString prefix = dir;
		if (!prefix.endsWith(QLatin1Char('/'))) prefix += QLatin1Char('/');
		return path.length() > prefix.length() && path.startsWith(prefix);
	}

#if !defined(Q_OS_WIN) && !defined(Q_OS_MAC) && defined(Q_OS_UNIX)
	int collectModule(struct dl_phdr_info *info, size_t, void *data)
	{
		QStringList *out = static_cast<QStringList*>(data);
		/* The main program reports an empty name and the vDSO a bare one;
		   neither is a file this class could copy. */
		const QString name = QString::fromLocal8Bit(info->dlpi_name);
		if (name.startsWith(QLatin1Char('/'))) *out << name;
		return 0;
	}
#endif

}; // namespace

HelperRelocation::Plan HelperRelocation::plan(const QString &exePath,
	const QStringList &loadedModules, const QString &targetDir,
	const QString &helperRoot, const QString &runId)
{
	Plan p;
	if (!isUnder(exePath, targetDir)) return p;

	/* Needed, even if it turns out there is nowhere to go: the caller then
	   learns that from carryOut() rather than silently staying put. */
	p.needed = true;
	if (helperRoot.isEmpty() || runId.isEmpty()) return p;
	p.destDir = helperRoot + QLatin1Char('/') + runId;

	QStringList sources;
	sources << exePath;
	for (int i = 0; i < loadedModules.size(); i++) {
		const QString &m = loadedModules.at(i);
		if (isUnder(m, targetDir) && !sources.contains(m)) sources << m;
	}

	QString prefix = targetDir;
	if (!prefix.endsWith(QLatin1Char('/'))) prefix += QLatin1Char('/');
	for (int i = 0; i < sources.size(); i++) {
		Copy c;
		c.from = sources.at(i);
		c.to = p.destDir + QLatin1Char('/') + sources.at(i).mid(prefix.length());
		p.copies << c;
	}
	p.destExe = p.copies.first().to;
	return p;
}

bool HelperRelocation::carryOut(IFileSystem *fs, const Plan &plan, QString *error)
{
	if (!plan.needed) return true;
	if (plan.destDir.isEmpty() || plan.copies.isEmpty()) {
		if (error) *error = QLatin1String("nowhere to copy the helper to");
		return false;
	}

	QString why;
	bool ok = true;
	/* A directory of this name can only be a crashed earlier attempt for the
	   same run; copyFile refuses to overwrite, so it goes first. */
	if (fs->exists(plan.destDir) && !fs->removeDirRecursively(plan.destDir)) {
		ok = false;
		why = fs->lastError();
	}
	if (ok && !fs->makeDir(plan.destDir)) {
		ok = false;
		why = fs->lastError();
	}
	for (int i = 0; ok && i < plan.copies.size(); i++) {
		const Copy &c = plan.copies.at(i);
		const QString parent = parentDirectoryOf(c.to);
		if (!fs->exists(parent) && !fs->makeDir(parent)) {
			ok = false;
			why = fs->lastError();
			break;
		}
		if (!fs->copyFile(c.from, c.to)) {
			ok = false;
			why = fs->lastError();
		}
	}
	if (ok && !fs->setStandardPermissions(plan.destExe, true)) {
		ok = false;
		why = fs->lastError();
	}

	if (!ok) {
		if (error) *error = why;
		/* Half a helper is worse than none: a later sweep would remove it
		   anyway, but the directory should not look like a finished copy. */
		fs->removeDirRecursively(plan.destDir);
		return false;
	}
	/* Best effort, as for the rest of the private root. */
	fs->restrictToOwner(plan.destDir);
	return true;
}

int HelperRelocation::sweep(IFileSystem *fs, const QString &helperRoot, const QString &keepRunId)
{
	if (helperRoot.isEmpty() || !fs->isDir(helperRoot)) return 0;
	int removed = 0;
	const QStringList names = fs->listDir(helperRoot);
	for (int i = 0; i < names.size(); i++) {
		const QString &name = names.at(i);
		if (name == keepRunId) continue;
		const QString path = helperRoot + QLatin1Char('/') + name;
		if (!fs->isDir(path)) continue;
		/* On Windows the executable of a copy that is still running will not
		   go, and the directory stays for the next sweep. Not an error. */
		if (fs->removeDirRecursively(path)) removed++;
	}
	return removed;
}

QStringList HelperRelocation::loadedModules()
{
	QStringList out;
#if defined(Q_OS_WIN)
	HMODULE modules[2048];
	DWORD bytesNeeded = 0;
	if (EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &bytesNeeded)) {
		const size_t count = qMin<size_t>(bytesNeeded / sizeof(HMODULE),
			sizeof(modules) / sizeof(modules[0]));
		for (size_t i = 0; i < count; i++) {
			wchar_t buffer[2 * MAX_PATH];
			const DWORD length = GetModuleFileNameW(modules[i], buffer,
				sizeof(buffer) / sizeof(buffer[0]));
			if (length == 0 || length >= sizeof(buffer) / sizeof(buffer[0])) continue;
			out << QDir::fromNativeSeparators(QString::fromWCharArray(buffer, length));
		}
	}
#elif defined(Q_OS_MAC)
	const uint32_t count = _dyld_image_count();
	for (uint32_t i = 0; i < count; i++) {
		const char *name = _dyld_get_image_name(i);
		if (name && name[0] == '/') out << QString::fromLocal8Bit(name);
	}
#elif defined(Q_OS_UNIX)
	dl_iterate_phdr(collectModule, &out);
#endif
	return out;
}
