#include "ExtractionPolicy.h"

using namespace pe_bear::updater;

namespace {

	struct RejectionName { ExtractionPolicy::Rejection r; const char *name; };

	const RejectionName NAMES[] = {
		{ ExtractionPolicy::NotRejected,              "NotRejected"              },
		{ ExtractionPolicy::EmptyPath,                "EmptyPath"                },
		{ ExtractionPolicy::AbsolutePath,             "AbsolutePath"             },
		{ ExtractionPolicy::DriveQualifiedPath,       "DriveQualifiedPath"       },
		{ ExtractionPolicy::UncPath,                  "UncPath"                  },
		{ ExtractionPolicy::PathTraversal,            "PathTraversal"            },
		{ ExtractionPolicy::TrailingDotOrSpace,       "TrailingDotOrSpace"       },
		{ ExtractionPolicy::ReservedWindowsName,      "ReservedWindowsName"      },
		{ ExtractionPolicy::ControlCharacterInPath,   "ControlCharacterInPath"   },
		{ ExtractionPolicy::PathTooLong,              "PathTooLong"              },
		{ ExtractionPolicy::TooManyPathComponents,    "TooManyPathComponents"    },
		{ ExtractionPolicy::UnsupportedEntryKind,     "UnsupportedEntryKind"     },
		{ ExtractionPolicy::LinkEntry,                "LinkEntry"                },
		{ ExtractionPolicy::DuplicateEntry,           "DuplicateEntry"           },
		{ ExtractionPolicy::CaseCollision,            "CaseCollision"            },
		{ ExtractionPolicy::EntryTooLarge,            "EntryTooLarge"            },
		{ ExtractionPolicy::TotalTooLarge,            "TotalTooLarge"            },
		{ ExtractionPolicy::TooManyEntries,           "TooManyEntries"           },
		{ ExtractionPolicy::CompressionRatioTooHigh,  "CompressionRatioTooHigh"  }
	};
	const size_t NAME_COUNT = sizeof(NAMES) / sizeof(NAMES[0]);

	/* Device names reserved by Windows. A file called "aux.txt" cannot be
	   created there, and an archive carrying one would fail mid-extraction. */
	const char* RESERVED[] = {
		"CON", "PRN", "AUX", "NUL",
		"COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
		"LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
	};
	const size_t RESERVED_COUNT = sizeof(RESERVED) / sizeof(RESERVED[0]);

}; // namespace

QString ExtractionPolicy::rejectionToString(Rejection r)
{
	for (size_t i = 0; i < NAME_COUNT; i++) {
		if (NAMES[i].r == r) return QLatin1String(NAMES[i].name);
	}
	return QLatin1String("Invalid");
}

QString ExtractionPolicy::rejectionMessage(Rejection r, const QString &path)
{
	const QString who = path.isEmpty() ? QCoreApplication::translate("Updater", "an entry")
		: QLatin1Char('"') + path + QLatin1Char('"');

	switch (r) {
		case NotRejected:
			return QString();
		case EmptyPath:
			return QCoreApplication::translate("Updater", "The package contains an entry with no name.");
		case AbsolutePath:
		case DriveQualifiedPath:
		case UncPath:
		case PathTraversal:
			return QCoreApplication::translate("Updater",
				"The package tries to write %1 outside the installation directory.").arg(who);
		case TrailingDotOrSpace:
		case ReservedWindowsName:
		case ControlCharacterInPath:
			return QCoreApplication::translate("Updater",
				"The package contains %1, which is not a usable file name on this system.").arg(who);
		case PathTooLong:
		case TooManyPathComponents:
			return QCoreApplication::translate("Updater", "The path of %1 is too long.").arg(who);
		case UnsupportedEntryKind:
			return QCoreApplication::translate("Updater",
				"The package contains %1, which is neither a file nor a directory.").arg(who);
		case LinkEntry:
			return QCoreApplication::translate("Updater",
				"The package contains a link, %1, which is not installed.").arg(who);
		case DuplicateEntry:
			return QCoreApplication::translate("Updater", "The package lists %1 more than once.").arg(who);
		case CaseCollision:
			return QCoreApplication::translate("Updater",
				"The package contains two entries differing only in letter case, including %1.").arg(who);
		case EntryTooLarge:
			return QCoreApplication::translate("Updater", "%1 is larger than expected for a package entry.").arg(who);
		case TotalTooLarge:
			return QCoreApplication::translate("Updater", "The package expands to more than the permitted size.");
		case TooManyEntries:
			return QCoreApplication::translate("Updater", "The package contains more entries than permitted.");
		case CompressionRatioTooHigh:
			return QCoreApplication::translate("Updater",
				"%1 expands far more than its stored size, which is characteristic of a decompression bomb.").arg(who);
		default:
			return QCoreApplication::translate("Updater", "The package was refused.");
	}
}

ExtractionPolicy::ExtractionPolicy(const Limits &limits)
	: m_limits(limits)
{
}

bool ExtractionPolicy::isReservedWindowsName(const QString &component)
{
	/* The stem before the first dot is what Windows matches on, so "aux.txt"
	   is reserved just as "aux" is. */
	const QString stem = component.section(QLatin1Char('.'), 0, 0).trimmed().toUpper();
	if (stem.isEmpty()) return false;
	for (size_t i = 0; i < RESERVED_COUNT; i++) {
		if (stem == QLatin1String(RESERVED[i])) return true;
	}
	return false;
}

QString ExtractionPolicy::normalisePath(const QString &stored)
{
	if (stored.isEmpty()) return QString();

	/* Backslashes are separators in archives produced on Windows, so they are
	   folded first -- otherwise "..\\x" would pass a check that only looks for
	   forward slashes. */
	QString p = stored;
	p.replace(QLatin1Char('\\'), QLatin1Char('/'));

	const QStringList parts = p.split(QLatin1Char('/'));
	QStringList kept;
	QStringList::const_iterator itr;
	for (itr = parts.begin(); itr != parts.end(); ++itr) {
		const QString part = *itr;
		if (part.isEmpty() || part == QLatin1String(".")) continue;
		if (part == QLatin1String("..")) {
			/* Not resolved against what came before: an archive has no
			   business climbing, and resolving would let "a/../../b" look
			   harmless after cancellation. */
			return QString();
		}
		kept << part;
	}
	if (kept.isEmpty()) return QString();
	return kept.join(QLatin1Char('/'));
}

ExtractionPolicy::Rejection ExtractionPolicy::checkEntry(const ArchiveEntry &entry) const
{
	const QString raw = entry.path;
	if (raw.trimmed().isEmpty()) return EmptyPath;

	/* Folded here too, so the shape checks below see one separator. */
	QString p = raw;
	p.replace(QLatin1Char('\\'), QLatin1Char('/'));

	/* Order matters: the most specific shapes are named first, so a message
	   says "drive-qualified" rather than the vaguer "absolute". */
	if (raw.startsWith(QLatin1String("\\\\")) || p.startsWith(QLatin1String("//"))) {
		return UncPath;
	}
	if (p.length() >= 2 && p.at(1) == QLatin1Char(':')) {
		const QChar c = p.at(0);
		if ((c >= QLatin1Char('A') && c <= QLatin1Char('Z'))
			|| (c >= QLatin1Char('a') && c <= QLatin1Char('z')))
		{
			return DriveQualifiedPath;
		}
	}
	if (p.startsWith(QLatin1Char('/'))) return AbsolutePath;

	/* Split plainly and drop empties by hand: the flag for this is spelled
	   differently in Qt5 and Qt6, and the project's QtCompat.h cannot be used
	   here because it includes QtWidgets, which this library deliberately does
	   not link (see tst_no_widgets_dependency). */
	QStringList parts;
	foreach (const QString &candidate, p.split(QLatin1Char('/'))) {
		if (candidate.isEmpty()) continue;
		/* A "." component is removed by normalisation and must be skipped here
		   too. GNU tar routinely stores paths as "./name", so applying the
		   per-component rules to it would reject ordinary Linux packages on
		   the trailing-dot rule. */
		if (candidate == QLatin1String(".")) continue;
		parts << candidate;
	}
	if (parts.isEmpty()) return EmptyPath;
	if (parts.size() > m_limits.maxPathComponents) return TooManyPathComponents;

	QStringList::const_iterator itr;
	for (itr = parts.begin(); itr != parts.end(); ++itr) {
		const QString part = *itr;
		if (part == QLatin1String("..")) return PathTraversal;

		for (int i = 0; i < part.length(); i++) {
			/* Control characters, including the NUL that truncates a C string
			   and could make the written name differ from the checked one. */
			if (part.at(i).unicode() < 0x20 || part.at(i).unicode() == 0x7F) {
				return ControlCharacterInPath;
			}
		}
		/* "a." and "a " both resolve to "a" on Windows, so two such entries
		   would silently overwrite one another. */
		if (part.endsWith(QLatin1Char('.')) || part.endsWith(QLatin1Char(' '))) {
			return TrailingDotOrSpace;
		}
		if (isReservedWindowsName(part)) return ReservedWindowsName;
	}

	const QString normalised = normalisePath(raw);
	if (normalised.isEmpty()) return PathTraversal;
	if (normalised.length() > m_limits.maxPathLength) return PathTooLong;

	switch (entry.kind) {
		case ArchiveEntry::KindFile:
		case ArchiveEntry::KindDir:
			break;
		case ArchiveEntry::KindSymlink:
		case ArchiveEntry::KindHardlink:
			/* Refused outright rather than validated against the destination.
			   A link whose target is checked at extraction time can still be
			   made to point elsewhere afterwards, and PE-bear's packages do
			   not contain links. */
			return LinkEntry;
		default:
			return UnsupportedEntryKind;
	}

	if (entry.kind == ArchiveEntry::KindFile) {
		if (entry.uncompressedSize < 0) return EntryTooLarge;
		if (entry.uncompressedSize > m_limits.maxEntryBytes) return EntryTooLarge;
		/* A ratio only means something once there is enough compressed data
		   for it to be meaningful; tiny entries routinely compress oddly. */
		if (entry.compressedSize > 0 && entry.compressedSize >= 512) {
			const qint64 ratio = entry.uncompressedSize / entry.compressedSize;
			if (ratio > m_limits.maxCompressionRatio) return CompressionRatioTooHigh;
		}
	}
	return NotRejected;
}

ExtractionPolicy::Verdict ExtractionPolicy::check(const QList<ArchiveEntry> &entries) const
{
	Verdict v;

	if (entries.size() > m_limits.maxEntries) {
		v.rejection = TooManyEntries;
		return v;
	}

	QSet<QString> seen;
	QSet<QString> seenLower;
	qint64 total = 0;

	QList<ArchiveEntry>::const_iterator itr;
	for (itr = entries.begin(); itr != entries.end(); ++itr) {
		const Rejection r = checkEntry(*itr);
		if (r != NotRejected) {
			v.rejection = r;
			v.offendingPath = itr->path;
			return v;
		}
		const QString normalised = normalisePath(itr->path);

		if (seen.contains(normalised)) {
			v.rejection = DuplicateEntry;
			v.offendingPath = itr->path;
			return v;
		}
		/* Checked on every host, not only on Windows. Two entries differing
		   only in case extract to one file there, so the archive is malformed
		   regardless of where the check happens to run -- and a Linux-only
		   test run would otherwise report this archive as fine. */
		const QString lower = normalised.toLower();
		if (seenLower.contains(lower)) {
			v.rejection = CaseCollision;
			v.offendingPath = itr->path;
			return v;
		}
		seen.insert(normalised);
		seenLower.insert(lower);

		if (itr->kind == ArchiveEntry::KindFile) {
			total += itr->uncompressedSize;
			if (total > m_limits.maxTotalBytes) {
				v.rejection = TotalTooLarge;
				v.offendingPath = itr->path;
				return v;
			}
		}
		ArchiveEntry accepted = *itr;
		accepted.path = normalised;
		v.accepted.append(accepted);
	}

	v.ok = true;
	v.totalBytes = total;
	return v;
}
