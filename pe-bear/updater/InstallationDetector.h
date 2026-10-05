#pragma once

#include <QtCore>

namespace pe_bear {
namespace updater {

enum InstallationKind {
	InstallUnknown = 0,
	/** Self-contained copy in a writable location: safe to replace in place. */
	InstallPortable,
	/** Installed into a shared location: replaceable only if it is writable. */
	InstallSystem,
	/** Owned by a package manager or sandbox: never touched, only reported. */
	InstallManaged,
	/**
	 * Sitting directly in a personal folder -- Desktop, Documents, Downloads,
	 * the home directory -- or at a filesystem root. Never replaced, because
	 * the installer replaces the *directory*, and here the directory is full
	 * of things that are not PE-bear. Unzipping a portable build straight
	 * onto the Desktop is common, and an update that then emptied the Desktop
	 * is the one outcome this whole design must never produce.
	 */
	InstallUserFolder
};

QString installationKindToString(InstallationKind k);

struct InstallationInfo
{
	InstallationInfo() : kind(InstallUnknown), writable(false) {}

	InstallationKind kind;
	QString installDir;
	QString executablePath;
	bool writable;
	/** Short, non-sensitive note on how the kind was decided. */
	QString detail;

	/**
	 * True only when this copy may be replaced by the updater. Managed and
	 * unknown installations are always notify-only, and so is anything the
	 * current user cannot write to -- the updater never elevates.
	 */
	bool isUpdatable() const
	{
		if (kind == InstallManaged || kind == InstallUnknown) return false;
		if (kind == InstallUserFolder) return false;
		return writable;
	}
};

/**
 * Works out how this copy of PE-bear was installed.
 *
 * Detection is by location and sandbox markers only. It never runs a package
 * manager -- not even to query it -- because spawning apt/dnf/pacman/winget
 * from an update check is exactly the behaviour v1 rules out.
 */
class InstallationDetector
{
public:
	/** Detects the running installation. */
	static InstallationInfo detect();

	/**
	 * Decides the kind from @p appDirPath, @p appFilePath and @p env alone --
	 * no process state is consulted, so every classification is reachable from
	 * a test.
	 *
	 * The one exception, which matters to callers: `writable` is answered by
	 * probing the real filesystem, so that field is not driven by @p env and
	 * cannot be faked. A caller that needs writability decided through its own
	 * filesystem abstraction has to set it itself.
	 */
	static InstallationInfo detectAt(const QString &appDirPath,
		const QString &appFilePath, const QMap<QString, QString> &env);

	/**
	 * The same, with the personal folders given rather than looked up, so a
	 * test can declare any directory to be somebody's Desktop. The three-
	 * argument form passes protectedDirectories().
	 */
	static InstallationInfo detectAt(const QString &appDirPath,
		const QString &appFilePath, const QMap<QString, QString> &env,
		const QStringList &protectedDirs);

	/**
	 * Directories an installation must never *be*: Desktop, Documents,
	 * Downloads, Pictures, Music, Movies and the home directory, as the
	 * platform reports them, canonicalised. A subdirectory of one of these is
	 * fine -- Desktop/pe-bear is a perfectly good place -- only the folder
	 * itself is refused.
	 */
	static QStringList protectedDirectories();

	static QMap<QString, QString> currentEnvironment();

	/** Probes writability by creating and removing a temporary entry. */
	static bool isDirectoryWritable(const QString &dirPath);
};

}; // namespace updater
}; // namespace pe_bear
