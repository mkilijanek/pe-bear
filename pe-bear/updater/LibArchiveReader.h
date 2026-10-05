#pragma once

#include <QtCore>
#include "ArchiveTypes.h"

#ifdef PEBEAR_WITH_LIBARCHIVE

namespace pe_bear {
namespace updater {

/**
 * IArchiveReader on top of libarchive.
 *
 * Reads the formats the project publishes -- ZIP for Windows and the macOS
 * bundle, TAR.XZ for Linux -- through one decoder rather than one per format.
 *
 * It only ever *describes* entries and hands back their bytes. Every decision
 * about whether an entry may be written belongs to ExtractionPolicy, which is
 * why this class performs no path handling of its own: a decoder that also
 * judged paths would put the security rules behind a dependency and out of
 * reach of the tests.
 *
 * libarchive's auto-detection is deliberately narrowed to the formats and
 * filters actually needed. Enabling everything would accept a cpio or a RAR as
 * readily as a ZIP, widening the attack surface of a tool that already gets
 * pointed at hostile files for a living.
 */
class LibArchiveReader : public IArchiveReader
{
public:
	/** Entries read in one pass are capped, so a hostile header cannot
	    make enumeration itself unbounded. */
	static const int MAX_ENTRIES_SCANNED = 100000;
	/** Refuses to materialise a single entry larger than this. */
	static const qint64 MAX_ENTRY_BYTES = Q_INT64_C(1024) * 1024 * 1024;

	LibArchiveReader();
	virtual ~LibArchiveReader();

	virtual bool open(const QString &path);
	virtual void close();
	virtual QList<ArchiveEntry> entries() const;
	virtual QByteArray readEntry(const QString &path);
	virtual QString lastError() const { return m_lastError; }

	/** True when the archive was opened and scanned. */
	bool isOpen() const { return m_open; }

private:
	bool scan();
	bool fail(const QString &why);

	QString m_path;
	bool m_open;
	QList<ArchiveEntry> m_entries;
	QString m_lastError;
};

}; // namespace updater
}; // namespace pe_bear

#endif /* PEBEAR_WITH_LIBARCHIVE */
