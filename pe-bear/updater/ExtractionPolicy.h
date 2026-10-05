#pragma once

#include <QtCore>
#include "ArchiveTypes.h"

namespace pe_bear {
namespace updater {

/**
 * Decides which archive entries may be written, and refuses the rest.
 *
 * Pure: no filesystem, no decoder, no clock. An entry list in, a verdict out.
 * That is what lets every rule below be tested exhaustively on any host --
 * including the rules whose consequences only differ on Windows.
 *
 * The posture is allow-listing by shape: an entry must be a plain file or a
 * directory, with a relative path that stays inside the destination, and the
 * set as a whole must stay within declared limits. Anything else is refused by
 * name, because a silently skipped entry would produce a half-extracted
 * package that looks complete.
 */
class ExtractionPolicy
{
public:
	/** Why a specific entry, or the archive as a whole, was refused. */
	enum Rejection {
		NotRejected = 0,
		EmptyPath,
		AbsolutePath,
		DriveQualifiedPath,     /* C:\... or C:/... */
		UncPath,                /* \\server\share */
		PathTraversal,          /* a ".." component */
		TrailingDotOrSpace,     /* "a." and "a " collide with "a" on Windows */
		ReservedWindowsName,    /* CON, PRN, AUX, NUL, COM1..9, LPT1..9 */
		ControlCharacterInPath,
		PathTooLong,
		TooManyPathComponents,
		UnsupportedEntryKind,   /* device, fifo, socket, ... */
		LinkEntry,              /* symlink or hardlink */
		DuplicateEntry,
		CaseCollision,
		EntryTooLarge,
		TotalTooLarge,
		TooManyEntries,
		CompressionRatioTooHigh,
		REJECTIONS_COUNT
	};

	static QString rejectionToString(Rejection r);
	/** A message fit to show a user, naming the entry. */
	static QString rejectionMessage(Rejection r, const QString &path);

	struct Limits
	{
		Limits()
			: maxEntryBytes(Q_INT64_C(512) * 1024 * 1024),
			maxTotalBytes(Q_INT64_C(2) * 1024 * 1024 * 1024),
			maxEntries(20000),
			maxPathLength(240),
			maxPathComponents(32),
			maxCompressionRatio(200) {}

		qint64 maxEntryBytes;
		qint64 maxTotalBytes;
		int maxEntries;
		/** Kept below MAX_PATH so a package cannot become un-deletable on Windows. */
		int maxPathLength;
		int maxPathComponents;
		/** Guards against a bomb: uncompressed / compressed, per entry. */
		int maxCompressionRatio;
	};

	struct Verdict
	{
		Verdict() : ok(false), rejection(NotRejected), totalBytes(0) {}

		bool ok;
		Rejection rejection;
		/** The offending entry, when one entry is to blame. */
		QString offendingPath;
		/** Entries that passed, with paths normalised to forward slashes. */
		QList<ArchiveEntry> accepted;
		qint64 totalBytes;

		QString message() const
		{
			return ok ? QString() : rejectionMessage(rejection, offendingPath);
		}
	};

	explicit ExtractionPolicy(const Limits &limits = Limits());

	/** Checks one entry's path and kind in isolation. */
	Rejection checkEntry(const ArchiveEntry &entry) const;

	/**
	 * Checks the whole archive: every entry, plus the properties that only
	 * exist across entries -- duplicates, case collisions and the totals.
	 */
	Verdict check(const QList<ArchiveEntry> &entries) const;

	/**
	 * Normalises a stored path to forward slashes with no redundant parts.
	 * Returns empty when the path cannot be made safe, which the caller must
	 * treat as a refusal rather than as an empty name.
	 */
	static QString normalisePath(const QString &stored);

	/** True when a name collides with a Windows device name, with or without extension. */
	static bool isReservedWindowsName(const QString &component);

	const Limits& limits() const { return m_limits; }

private:
	Limits m_limits;
};

}; // namespace updater
}; // namespace pe_bear
