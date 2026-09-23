#include "UpdatePaths.h"

using namespace pe_bear::updater;

const char* UpdatePaths::DIR_NAME = "PE-bear-updates";
const char* UpdatePaths::STAGING_DIR_NAME = ".PE-bear-staging";

QString UpdatePaths::defaultRoot()
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 4, 0)
	QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
#else
	QString base = QStandardPaths::writableLocation(QStandardPaths::DataLocation);
#endif
	if (base.isEmpty()) {
		base = QDir::tempPath();
	}
	return QDir::cleanPath(base + QDir::separator() + QLatin1String(DIR_NAME));
}

QString UpdatePaths::preferredStagingRoot(const QString &installDir, const QString &fallbackRoot)
{
	if (!installDir.isEmpty()) {
		QDir dir(installDir);
		if (dir.exists()) {
			const QString candidate = QDir::cleanPath(
				dir.absolutePath() + QDir::separator() + QLatin1String(STAGING_DIR_NAME));
			/* only worth using if we can actually create it there */
			QDir candidateDir(candidate);
			if (candidateDir.exists() || QDir().mkpath(candidate)) {
				restrictToOwner(candidate);
				return candidate;
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
