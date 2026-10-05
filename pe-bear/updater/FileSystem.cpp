#include "FileSystem.h"
#include "UpdatePaths.h"

using namespace pe_bear::updater;

QString pe_bear::updater::parentDirectoryOf(const QString &path)
{
	/* Refused before cleanPath touches it: cleanPath normalises separators on
	   Windows but not on POSIX, so letting a native path through is what made
	   this answer differ by host. See the header. */
	if (path.contains(QLatin1Char('\\'))) return QString();

	const QString clean = QDir::cleanPath(path);
	const int slash = clean.lastIndexOf(QLatin1Char('/'));

	if (slash < 0) return QString();
	/* Child of the POSIX root: the parent is the root, not the empty string. */
	if (slash == 0) return QLatin1String("/");
	/* "C:/x" -> "C:/", keeping the separator so the result stays absolute. */
	if (slash == 2 && clean.at(1) == QLatin1Char(':')) return clean.left(3);
	return clean.left(slash);
}

bool RealFileSystem::fail(const QString &what) const
{
	m_lastError = what;
	return false;
}

bool RealFileSystem::exists(const QString &path) const
{
	if (path.isEmpty()) return false;
	return QFileInfo::exists(path);
}

bool RealFileSystem::isDir(const QString &path) const
{
	if (path.isEmpty()) return false;
	return QFileInfo(path).isDir();
}

qint64 RealFileSystem::fileSize(const QString &path) const
{
	const QFileInfo info(path);
	if (!info.exists() || !info.isFile()) return -1;
	return info.size();
}

QStringList RealFileSystem::listDir(const QString &path) const
{
	QDir dir(path);
	if (!dir.exists()) return QStringList();
	return dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
		QDir::Name);
}

QString RealFileSystem::canonicalPath(const QString &path) const
{
	if (path.isEmpty()) return QString();
	const QString resolved = QFileInfo(path).canonicalFilePath();
	return resolved; /* empty when it does not resolve */
}

bool RealFileSystem::isWritableDir(const QString &path) const
{
	QDir dir(path);
	if (!dir.exists()) return false;

	/* Probed rather than read off the permission bits: those are misleading on
	   Windows shares and on read-only mounts. */
	for (int attempt = 0; attempt < 8; attempt++) {
		const QString name = QLatin1String(".pe-bear-probe-")
			+ QString::number(QRandomGenerator::global()->generate(), 16);
		const QString probe = dir.absoluteFilePath(name);
		if (QFileInfo::exists(probe)) continue;

		QFile f(probe);
		if (!f.open(QIODevice::WriteOnly)) return false;
		f.close();
		f.remove();
		return true;
	}
	return false;
}

bool RealFileSystem::makeDir(const QString &path)
{
	if (path.isEmpty()) return fail(QLatin1String("empty path"));
	if (QDir(path).exists()) return true;
	if (!QDir().mkpath(path)) {
		return fail(QLatin1String("could not create ") + QDir::toNativeSeparators(path));
	}
	return true;
}

bool RealFileSystem::removeFile(const QString &path)
{
	if (path.isEmpty()) return fail(QLatin1String("empty path"));
	if (!QFileInfo::exists(path)) return true; /* already gone is success */
	if (!QFile::remove(path)) {
		return fail(QLatin1String("could not remove ") + QDir::toNativeSeparators(path));
	}
	return true;
}

bool RealFileSystem::removeDirRecursively(const QString &path)
{
	if (path.isEmpty()) return fail(QLatin1String("empty path"));
	QDir dir(path);
	if (!dir.exists()) return true;
	if (!dir.removeRecursively()) {
		return fail(QLatin1String("could not remove tree ") + QDir::toNativeSeparators(path));
	}
	return true;
}

bool RealFileSystem::movePath(const QString &from, const QString &to)
{
	if (from.isEmpty() || to.isEmpty()) return fail(QLatin1String("empty path"));
	if (!QFileInfo::exists(from)) {
		return fail(QLatin1String("source does not exist: ") + QDir::toNativeSeparators(from));
	}
	if (QFileInfo::exists(to)) {
		return fail(QLatin1String("destination already exists: ") + QDir::toNativeSeparators(to));
	}
	/* QFile::rename handles both files and directories on the platforms in
	   scope, and is atomic within a volume. */
	if (!QFile::rename(from, to)) {
		return fail(QLatin1String("could not move ") + QDir::toNativeSeparators(from)
			+ QLatin1String(" to ") + QDir::toNativeSeparators(to));
	}
	return true;
}

bool RealFileSystem::copyFile(const QString &from, const QString &to)
{
	if (from.isEmpty() || to.isEmpty()) return fail(QLatin1String("empty path"));
	if (QFileInfo::exists(to)) {
		return fail(QLatin1String("destination already exists: ") + QDir::toNativeSeparators(to));
	}
	if (!QFile::copy(from, to)) {
		return fail(QLatin1String("could not copy ") + QDir::toNativeSeparators(from));
	}
	return true;
}

bool RealFileSystem::writeFile(const QString &path, const QByteArray &data)
{
	if (path.isEmpty()) return fail(QLatin1String("empty path"));

	/* Written to a sibling and renamed over the target, so a reader never sees
	   a half-written journal -- the whole point of the journal is that it can
	   be read after an interruption. */
	const QString tmp = path + QLatin1String(".tmp");
	QFile::remove(tmp);

	QFile f(tmp);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return fail(QLatin1String("could not open ") + QDir::toNativeSeparators(tmp));
	}
	const qint64 written = f.write(data);
	if (written != data.size()) {
		f.close();
		QFile::remove(tmp);
		return fail(QLatin1String("short write to ") + QDir::toNativeSeparators(tmp));
	}
	if (!f.flush()) {
		f.close();
		QFile::remove(tmp);
		return fail(QLatin1String("could not flush ") + QDir::toNativeSeparators(tmp));
	}
	f.close();

	QFile::remove(path);
	if (!QFile::rename(tmp, path)) {
		QFile::remove(tmp);
		return fail(QLatin1String("could not place ") + QDir::toNativeSeparators(path));
	}
	UpdatePaths::restrictToOwner(path);
	return true;
}

QByteArray RealFileSystem::readFile(const QString &path) const
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) {
		fail(QLatin1String("could not read ") + QDir::toNativeSeparators(path));
		return QByteArray();
	}
	const QByteArray data = f.readAll();
	f.close();
	return data;
}

bool RealFileSystem::setStandardPermissions(const QString &path, bool executable)
{
	QFile file(path);
	if (!file.exists()) {
		m_lastError = QLatin1String("cannot set permissions on a missing file: ")
			+ QDir::toNativeSeparators(path);
		return false;
	}

	/* Assigned rather than added to, because the point is to undo writeFile's
	   owner-only restriction, not to layer bits on top of it. Group and other
	   get read only -- never write, which would make an installation anyone
	   could tamper with. */
	QFile::Permissions permissions = QFile::ReadOwner | QFile::WriteOwner
		| QFile::ReadGroup | QFile::ReadOther;
	if (executable) {
		/* ExeUser is left out on purpose: it means "the current user" rather
		   than a fixed bit, so mixing it in would make the resulting mode
		   depend on who ran the updater. */
		permissions |= QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther;
	}

	if (!file.setPermissions(permissions)) {
		m_lastError = QLatin1String("could not set permissions on ")
			+ QDir::toNativeSeparators(path);
		return false;
	}
	return true;
}

bool RealFileSystem::restrictToOwner(const QString &path)
{
	return UpdatePaths::restrictToOwner(path);
}
