#pragma once

#include <QtCore>
#include "FileSystem.h"
#include "UpdatePaths.h"

namespace pe_bear {
namespace updater {

/**
 * What the helper leaves behind for PE-bear to find at its next start.
 *
 * The helper's exit code is a contract, but nobody is there to read it: PE-bear
 * has closed by the time the helper decides anything, and whatever starts
 * afterwards -- the relaunched build, or the user opening PE-bear by hand after
 * a rollback -- is a fresh process. So the outcome is written down, in one
 * small file in the updater's private directory, and the next PE-bear reads
 * it, tells the user, and removes it. Removed, not kept: a result shown once
 * is information; the same result shown at every start is nagging.
 *
 * It carries the outcome by *name* rather than by enum value, and says in so
 * many words whether the installation was left untouched. PE-bear should be
 * able to phrase "nothing was changed" without depending on the helper's
 * numbering staying put.
 */
struct HelperResult
{
	static const int CURRENT_VERSION = 1;

	HelperResult() : version(CURRENT_VERSION), exitCode(-1), leftUntouched(false) {}

	int version;
	/** Pairs the result with the instruction that produced it. */
	QString runId;
	/** UpdateHelper::resultToString of the outcome, e.g. "Succeeded". */
	QString result;
	int exitCode;
	/** The helper's own statement, so the reader need not re-derive it. */
	bool leftUntouched;
	/** Human-readable, translated by the helper; what the user is shown. */
	QString message;
	/** The helper's lastError(), for the log rather than the dialog. */
	QString detail;
	QString finishedAtUtc;

	bool isValid() const;
	QByteArray toJson() const;
	static HelperResult fromJson(const QByteArray &data, bool *ok = NULL);

	static QString fileName();
	static QString pathIn(const UpdatePaths &paths);

	/** Writes @p result owner-only into the private root. */
	static bool write(IFileSystem &fs, const UpdatePaths &paths, const HelperResult &result);

	/**
	 * Reads the result and removes the file, whatever it contained.
	 *
	 * Absent is simply "nothing to report" (@p ok false, no error). A file
	 * that is present but unreadable is also removed -- it cannot be shown,
	 * and leaving it would make every later start trip over it -- and is
	 * reported through @p ok false with @p unreadable set.
	 */
	static HelperResult consume(IFileSystem &fs, const UpdatePaths &paths,
		bool *ok = NULL, bool *unreadable = NULL);
};

}; // namespace updater
}; // namespace pe_bear
