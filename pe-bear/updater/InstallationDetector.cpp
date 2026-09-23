#include "InstallationDetector.h"

using namespace pe_bear::updater;

QString pe_bear::updater::installationKindToString(InstallationKind k)
{
	switch (k) {
		case InstallPortable: return QLatin1String("portable");
		case InstallSystem: return QLatin1String("system");
		case InstallManaged: return QLatin1String("managed");
		default: return QLatin1String("unknown");
	}
}

namespace {

	QString normalized(const QString &path)
	{
		if (path.isEmpty()) return QString();
		QString p = QDir::cleanPath(QDir(path).absolutePath());
		if (p.length() > 1 && p.endsWith(QLatin1Char('/'))) {
			p.chop(1);
		}
		return p;
	}

	/** True when @p path is @p prefix or lies below it, on a path-segment boundary. */
	bool isUnder(const QString &path, const QString &prefix)
	{
		if (prefix.isEmpty() || path.isEmpty()) return false;
		const QString p = normalized(path);
		const QString pre = normalized(prefix);
		if (pre.isEmpty()) return false;
#if defined(Q_OS_WIN)
		const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
		const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
		if (p.compare(pre, cs) == 0) return true;
		const QString preSlash = pre.endsWith(QLatin1Char('/')) ? pre : (pre + QLatin1Char('/'));
		return p.startsWith(preSlash, cs);
	}

	QString envValue(const QMap<QString, QString> &env, const char *key)
	{
		return env.value(QLatin1String(key));
	}

}; // namespace

QMap<QString, QString> InstallationDetector::currentEnvironment()
{
	QMap<QString, QString> out;
	const QProcessEnvironment pe = QProcessEnvironment::systemEnvironment();
	static const char* INTERESTING[] = {
		"APPIMAGE", "FLATPAK_ID", "SNAP", "APPDIR",
		"ProgramFiles", "ProgramFiles(x86)", "ProgramW6432", "HOME", "USERPROFILE"
	};
	const size_t count = sizeof(INTERESTING) / sizeof(INTERESTING[0]);
	for (size_t i = 0; i < count; i++) {
		const QString key = QLatin1String(INTERESTING[i]);
		if (pe.contains(key)) {
			out.insert(key, pe.value(key));
		}
	}
	return out;
}

bool InstallationDetector::isDirectoryWritable(const QString &dirPath)
{
	QDir dir(dirPath);
	if (!dir.exists()) return false;

	/* An access() style check is not enough on Windows shares and on
	   read-only mounts, so actually try to create something. */
	for (int attempt = 0; attempt < 8; attempt++) {
		const QString probeName = QLatin1String(".pe-bear-write-probe-")
			+ QString::number(QRandomGenerator::global()->generate(), 16);
		const QString probePath = dir.absoluteFilePath(probeName);
		if (QFile::exists(probePath)) continue;

		QFile probe(probePath);
		if (!probe.open(QIODevice::WriteOnly)) {
			return false;
		}
		probe.close();
		probe.remove();
		return true;
	}
	return false;
}

InstallationInfo InstallationDetector::detectAt(const QString &appDirPath,
	const QString &appFilePath, const QMap<QString, QString> &env)
{
	InstallationInfo info;
	info.installDir = normalized(appDirPath);
	info.executablePath = appFilePath;

	if (info.installDir.isEmpty()) {
		info.kind = InstallUnknown;
		info.detail = QLatin1String("application directory is unknown");
		return info;
	}
	info.writable = isDirectoryWritable(info.installDir);

	/* 1. Sandboxes own their payload outright. */
	if (!envValue(env, "FLATPAK_ID").isEmpty()) {
		info.kind = InstallManaged;
		info.detail = QLatin1String("running inside a Flatpak sandbox");
		return info;
	}
	if (!envValue(env, "SNAP").isEmpty()) {
		info.kind = InstallManaged;
		info.detail = QLatin1String("running inside a Snap sandbox");
		return info;
	}
	/* 2. An AppImage is a single self-contained file wherever it sits. */
	const QString appImage = envValue(env, "APPIMAGE");
	if (!appImage.isEmpty()) {
		info.kind = InstallPortable;
		info.executablePath = appImage;
		const QFileInfo imageInfo(appImage);
		info.installDir = normalized(imageInfo.absolutePath());
		info.writable = isDirectoryWritable(info.installDir);
		info.detail = QLatin1String("running as an AppImage");
		return info;
	}

#if defined(Q_OS_WIN)
	static const char* WIN_SYSTEM_VARS[] = { "ProgramFiles", "ProgramFiles(x86)", "ProgramW6432" };
	const size_t winVarCount = sizeof(WIN_SYSTEM_VARS) / sizeof(WIN_SYSTEM_VARS[0]);
	for (size_t i = 0; i < winVarCount; i++) {
		const QString root = envValue(env, WIN_SYSTEM_VARS[i]);
		if (!root.isEmpty() && isUnder(info.installDir, root)) {
			info.kind = InstallSystem;
			info.detail = QLatin1String("installed under ") + QLatin1String(WIN_SYSTEM_VARS[i]);
			return info;
		}
	}
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
	if (isUnder(info.installDir, QLatin1String("/Applications"))) {
		const QString home = envValue(env, "HOME");
		if (!home.isEmpty() && isUnder(info.installDir, home + QLatin1String("/Applications"))) {
			info.kind = InstallPortable;
			info.detail = QLatin1String("installed in the user Applications folder");
			return info;
		}
		info.kind = InstallSystem;
		info.detail = QLatin1String("installed in /Applications");
		return info;
	}
#else
	/* Linux and the other Unixes: /usr is distribution-owned territory. */
	if (isUnder(info.installDir, QLatin1String("/usr"))) {
		info.kind = InstallManaged;
		info.detail = QLatin1String("installed under /usr, owned by the system package manager");
		return info;
	}
	if (isUnder(info.installDir, QLatin1String("/snap"))
		|| isUnder(info.installDir, QLatin1String("/var/lib/flatpak"))
		|| isUnder(info.installDir, QLatin1String("/var/lib/snapd")))
	{
		info.kind = InstallManaged;
		info.detail = QLatin1String("installed in a package-manager-owned location");
		return info;
	}
	/* /usr/local is already covered by the /usr check above. */
	if (isUnder(info.installDir, QLatin1String("/opt"))) {
		info.kind = InstallSystem;
		info.detail = QLatin1String("installed in a shared location");
		return info;
	}
#endif

	info.kind = InstallPortable;
	info.detail = QLatin1String("portable installation");
	return info;
}

InstallationInfo InstallationDetector::detect()
{
	const QString dirPath = QCoreApplication::applicationDirPath();
	const QString filePath = QCoreApplication::applicationFilePath();
	return detectAt(dirPath, filePath, currentEnvironment());
}
