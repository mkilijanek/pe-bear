#include "UpdatePaths.h"
#include "FileSystem.h"

using namespace pe_bear::updater;

const char* UpdatePaths::DIR_NAME = "PE-bear-updates";
const char* UpdatePaths::STAGING_DIR_NAME = ".PE-bear-staging";
/* Matches what QCoreApplication::applicationName() is set to in PE-bear, so
   the path is unchanged from the one AppLocalDataLocation used to give. */
const char* UpdatePaths::APPLICATION_DIR_NAME = "PE-bear";

QString UpdatePaths::defaultRoot()
{
	/* Deliberately not AppLocalDataLocation.
	
	   That location is derived from QCoreApplication::applicationName(), and
	   two different executables have to agree on this directory: PE-bear
	   writes the package and the instructions into it, and pe-bear-updater
	   reads them back and refuses anything outside it. Asking Qt for "this
	   application's data directory" gives each of them its own -- PE-bear
	   would use .../PE-bear/updates and the helper .../pe-bear-updater/updates
	   -- and the helper would then refuse every real handoff for being in the
	   wrong place.
	
	   The folder name is therefore fixed here rather than inherited from
	   whichever binary happens to be asking. The result is byte-identical to
	   what AppLocalDataLocation yields for an application named PE-bear with
	   no organization set, on all three platforms, so nothing moves. */
	QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
	if (base.isEmpty()) {
		base = QDir::tempPath();
	}
	return QDir::cleanPath(base + QDir::separator() + QLatin1String(APPLICATION_DIR_NAME)
		+ QDir::separator() + QLatin1String(DIR_NAME));
}

QString UpdatePaths::preferredStagingRoot(const QString &installDir, const QString &fallbackRoot)
{
	/* Names a directory. Does not create one.
	
	   It used to create it, and that was a real defect: this runs before the
	   helper has validated anything, so a stale, malformed or outright refused
	   instruction left a .PE-bear-staging directory beside whatever path it
	   happened to name -- the only filesystem change before the phase that is
	   documented to change nothing, and outside the updater's private root at
	   that. It is also called when PE-bear merely starts with the updater
	   enabled, so simply running the application created it.
	
	   Creation now belongs to Installer, after canInstall has agreed the
	   installation may be touched. That check already requires the parent to
	   be writable -- replacing a directory by moving it is a write to its
	   parent -- so the directory named here can be created when the time
	   comes. */
	if (!installDir.isEmpty()) {
		QDir dir(installDir);
		if (dir.exists()) {
			/* Beside the installation, not inside it: the installer moves the
			   installation aside before activating, and a staging tree within
			   it would go along. The parent is the nearest place on the same
			   volume that survives that move, which is what keeps activation a
			   rename rather than a copy of the whole build. */
			const QString parent = parentDirectoryOf(dir.absolutePath());
			if (!parent.isEmpty() && parent != dir.absolutePath()) {
				return QDir::cleanPath(parent + QDir::separator()
					+ QLatin1String(STAGING_DIR_NAME));
			}
		}
	}
	return QDir::cleanPath(fallbackRoot + QDir::separator() + QLatin1String("staging"));
}

bool UpdatePaths::restrictToOwner(const QString &path)
{
	QFile::Permissions perms = QFile::ReadOwner | QFile::WriteOwner;
	QFileInfo info(path);
	if (info.isDir()) {
		perms |= QFile::ExeOwner;
	}
	return QFile::setPermissions(path, perms);
}

bool UpdatePaths::removeRecursively(const QString &path)
{
	if (path.isEmpty()) return false;
	QDir dir(path);
	if (!dir.exists()) return true;
	return dir.removeRecursively();
}

QString UpdatePaths::randomName()
{
	return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

UpdatePaths::UpdatePaths()
	: m_root(defaultRoot())
{
	m_stagingRoot = QDir::cleanPath(m_root + QDir::separator() + QLatin1String("staging"));
}

UpdatePaths::UpdatePaths(const QString &root, const QString &stagingRoot)
	: m_root(QDir::cleanPath(root)), m_stagingRoot(QDir::cleanPath(stagingRoot))
{
	if (m_stagingRoot.isEmpty()) {
		m_stagingRoot = QDir::cleanPath(m_root + QDir::separator() + QLatin1String("staging"));
	}
}

QString UpdatePaths::downloadsDir() const
{
	return QDir::cleanPath(m_root + QDir::separator() + QLatin1String("downloads"));
}

QString UpdatePaths::stagingDir() const
{
	return m_stagingRoot;
}

QString UpdatePaths::transactionsDir() const
{
	return QDir::cleanPath(m_root + QDir::separator() + QLatin1String("transactions"));
}

QString UpdatePaths::backupsDir() const
{
	return QDir::cleanPath(m_root + QDir::separator() + QLatin1String("backups"));
}

QString UpdatePaths::logFilePath() const
{
	return QDir::cleanPath(m_root + QDir::separator() + QLatin1String("updater.log"));
}

bool UpdatePaths::prepare(QString *error)
{
	const QString dirs[] = {
		m_root, downloadsDir(), stagingDir(), transactionsDir(), backupsDir()
	};
	const int count = sizeof(dirs) / sizeof(dirs[0]);

	for (int i = 0; i < count; i++) {
		const QString &path = dirs[i];
		if (path.isEmpty()) {
			if (error) *error = QLatin1String("empty update directory path");
			return false;
		}
		QDir dir(path);
		if (!dir.exists() && !QDir().mkpath(path)) {
			if (error) {
				*error = QLatin1String("could not create ") + QDir::toNativeSeparators(path);
			}
			return false;
		}
		restrictToOwner(path);
	}
	return true;
}

QString UpdatePaths::createDownloadDir(QString *error) const
{
	const QString parent = downloadsDir();
	if (!QDir(parent).exists() && !QDir().mkpath(parent)) {
		if (error) *error = QLatin1String("could not create the downloads directory");
		return QString();
	}
	/* A fresh random directory per attempt: a stale or hostile leftover can
	   never be mistaken for this run's download. */
	for (int attempt = 0; attempt < 8; attempt++) {
		const QString candidate = QDir::cleanPath(parent + QDir::separator() + randomName());
		if (QFileInfo::exists(candidate)) continue;
		if (!QDir().mkpath(candidate)) continue;
		restrictToOwner(candidate);
		return candidate;
	}
	if (error) *error = QLatin1String("could not create a download directory");
	return QString();
}
