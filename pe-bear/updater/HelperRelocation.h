#pragma once

#include <QtCore>
#include "FileSystem.h"

namespace pe_bear {
namespace updater {

/**
 * Moves the helper out of the directory it is about to replace.
 *
 * pe-bear-updater is shipped beside PE-bear, so PE-bear starts it from inside
 * the installation. The installer then moves that whole directory aside as the
 * backup and, after the new build has confirmed itself, deletes the backup --
 * with the helper's own image still inside it. On Windows a running executable
 * cannot be deleted, so the removal fails and an empty backup directory is left
 * beside the installation, with "could not remove the backup" recorded on a
 * record that is otherwise committed (#38). Linux allows the unlink and never
 * showed the problem, which is why no test here did either.
 *
 * The cure is for the helper to run from somewhere else: it copies its own
 * executable and every loaded module that lives under the installation
 * (on Windows: the Qt and libarchive DLLs deployed beside it) into a directory
 * of its own under the updater's private root, starts that copy with the same
 * instruction, and exits. Modules that live elsewhere -- system libraries, a
 * Qt installed on the machine -- are not copied; the copy finds them where the
 * original did.
 *
 * Pure planning is separated from doing, so the decision can be tested without
 * a process to inspect. Nothing here is a privilege boundary: the copy runs as
 * the same user, from a directory only that user can read.
 */
class HelperRelocation
{
public:
	/** One file to carry over, with the target-relative layout preserved. */
	struct Copy
	{
		QString from;
		QString to;
	};

	struct Plan
	{
		Plan() : needed(false) {}
		/** False when the helper already runs from outside the installation. */
		bool needed;
		/** Where the copy lives: `<helperRoot>/<runId>`. */
		QString destDir;
		/** The executable to start instead of this one. */
		QString destExe;
		QList<Copy> copies;
	};

	/**
	 * Decides whether and what to copy. All paths are expected canonical with
	 * forward slashes, as IFileSystem::canonicalPath returns them; a target
	 * that is a prefix of the executable's directory name ("pe-bear" against
	 * "pe-bear-old/x") does not count as containing it.
	 */
	static Plan plan(const QString &exePath, const QStringList &loadedModules,
		const QString &targetDir, const QString &helperRoot, const QString &runId);

	/**
	 * Carries the plan out. On failure whatever was copied is removed again
	 * and the caller is expected to continue in place -- the pre-#38
	 * behaviour, which installs correctly and leaves a directory behind.
	 */
	static bool carryOut(IFileSystem *fs, const Plan &plan, QString *error);

	/**
	 * Removes the directories of earlier runs under helperRoot, keeping
	 * keepRunId. A copy cannot delete itself while it runs, so each run
	 * sweeps up after the ones before it. Returns how many were removed.
	 */
	static int sweep(IFileSystem *fs, const QString &helperRoot, const QString &keepRunId);

	/**
	 * The modules mapped into this process that exist as files, as paths with
	 * forward slashes; the executable itself is included where the platform
	 * reports it. Platform-specific, and the only part of this class that
	 * looks at the running process.
	 */
	static QStringList loadedModules();

	/** The command-line marker the started copy is given, so it never relocates again. */
	static const char* RELOCATED_FLAG;
	/** Precedes the pid of the process that made the copy, for the copy to wait on. */
	static const char* RELOCATED_FROM_FLAG;
};

}; // namespace updater
}; // namespace pe_bear
