#include "LibArchiveReader.h"

#ifdef PEBEAR_WITH_LIBARCHIVE

#include <archive.h>
#include <archive_entry.h>

using namespace pe_bear::updater;

namespace {

	/** Read block size handed to libarchive. */
	const size_t READ_BLOCK = 64 * 1024;

	/**
	 * Opens an archive with only the formats and filters this project
	 * publishes enabled.
	 *
	 * archive_read_support_format_all() would also accept cpio, 7zip, RAR, ar,
	 * mtree and more. Every one of those is decoder code that would be reached
	 * by an attacker-supplied file, for no benefit: the updater installs ZIP
	 * and TAR.XZ and nothing else.
	 */
	struct archive* openNarrowly(const QString &path, QString *error)
	{
		struct archive *a = archive_read_new();
		if (!a) {
			if (error) *error = QLatin1String("could not allocate a reader");
			return NULL;
		}
		archive_read_support_format_zip(a);
		archive_read_support_format_tar(a);
		archive_read_support_filter_xz(a);
		archive_read_support_filter_gzip(a);

		const QByteArray native = QFile::encodeName(path);
		if (archive_read_open_filename(a, native.constData(), READ_BLOCK) != ARCHIVE_OK) {
			if (error) *error = QString::fromUtf8(archive_error_string(a)
				? archive_error_string(a) : "could not open the archive");
			archive_read_free(a);
			return NULL;
		}
		return a;
	}

	ArchiveEntry::Kind kindOf(struct archive_entry *e)
	{
		/* Link-ness is checked before the file type: libarchive reports a
		   symlink as a regular file with a link target set, and treating one
		   as a file is exactly the confusion ExtractionPolicy refuses. */
		if (archive_entry_symlink(e) != NULL) return ArchiveEntry::KindSymlink;
		if (archive_entry_hardlink(e) != NULL) return ArchiveEntry::KindHardlink;

		const mode_t type = archive_entry_filetype(e);
		if (type == AE_IFREG) return ArchiveEntry::KindFile;
		if (type == AE_IFDIR) return ArchiveEntry::KindDir;
		if (type == AE_IFLNK) return ArchiveEntry::KindSymlink;
		/* block and character devices, fifos, sockets */
		return ArchiveEntry::KindOther;
	}

}; // namespace

LibArchiveReader::LibArchiveReader()
	: m_open(false)
{
}

LibArchiveReader::~LibArchiveReader()
{
	close();
}

bool LibArchiveReader::fail(const QString &why)
{
	m_lastError = why;
	return false;
}

bool LibArchiveReader::open(const QString &path)
{
	close();
	if (path.isEmpty()) return fail(QLatin1String("no archive path"));
	if (!QFileInfo::exists(path)) return fail(QLatin1String("the archive does not exist"));

	m_path = path;
	if (!scan()) return false;
	m_open = true;
	return true;
}

void LibArchiveReader::close()
{
	m_open = false;
	m_entries.clear();
	m_path.clear();
}

bool LibArchiveReader::scan()
{
	QString error;
	struct archive *a = openNarrowly(m_path, &error);
	if (!a) return fail(error);

	m_entries.clear();
	struct archive_entry *entry = NULL;
	int scanned = 0;

	while (true) {
		const int r = archive_read_next_header(a, &entry);
		if (r == ARCHIVE_EOF) break;
		if (r != ARCHIVE_OK && r != ARCHIVE_WARN) {
			const QString why = QString::fromUtf8(archive_error_string(a)
				? archive_error_string(a) : "malformed archive");
			archive_read_free(a);
			m_entries.clear();
			return fail(why);
		}
		if (++scanned > MAX_ENTRIES_SCANNED) {
			archive_read_free(a);
			m_entries.clear();
			return fail(QLatin1String("the archive declares more entries than will be read"));
		}

		ArchiveEntry out;
		/* Taken as raw bytes and decoded as UTF-8 rather than through the
		   locale: the same archive must describe the same paths on every
		   host, or the policy would judge different strings per platform. */
		const char *raw = archive_entry_pathname_utf8(entry);
		if (!raw) raw = archive_entry_pathname(entry);
		out.path = raw ? QString::fromUtf8(raw) : QString();

		out.kind = kindOf(entry);
		out.uncompressedSize = archive_entry_size_is_set(entry)
			? qint64(archive_entry_size(entry)) : -1;
		/* Per-entry compressed size is not exposed portably; the policy treats
		   a non-positive value as "no ratio to judge", which is honest. */
		out.compressedSize = 0;

		const char *link = archive_entry_symlink(entry);
		if (!link) link = archive_entry_hardlink(entry);
		if (link) out.linkTarget = QString::fromUtf8(link);

		m_entries.append(out);
		archive_read_data_skip(a);
	}

	archive_read_free(a);
	return true;
}

QList<ArchiveEntry> LibArchiveReader::entries() const
{
	return m_entries;
}

QByteArray LibArchiveReader::readEntry(const QString &path)
{
	if (!m_open) { fail(QLatin1String("the archive is not open")); return QByteArray(); }
	if (path.isEmpty()) { fail(QLatin1String("no entry name")); return QByteArray(); }

	QString error;
	struct archive *a = openNarrowly(m_path, &error);
	if (!a) { fail(error); return QByteArray(); }

	/* Reopened and walked rather than seeked: a stream filter like xz has no
	   usable random access, and the alternative -- holding every entry in
	   memory from the scan -- would let a declared size dictate the footprint. */
	struct archive_entry *entry = NULL;
	QByteArray out;
	bool found = false;

	while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
		const char *raw = archive_entry_pathname_utf8(entry);
		if (!raw) raw = archive_entry_pathname(entry);
		const QString candidate = raw ? QString::fromUtf8(raw) : QString();

		/* Compared after the same normalisation the policy applied, since the
		   caller asks with the normalised name. */
		QString normalised = candidate;
		normalised.replace(QLatin1Char('\\'), QLatin1Char('/'));
		while (normalised.startsWith(QLatin1String("./"))) normalised.remove(0, 2);
		if (normalised != path) {
			archive_read_data_skip(a);
			continue;
		}
		found = true;

		if (archive_entry_filetype(entry) != AE_IFREG) break; /* nothing to read */

		const qint64 declared = archive_entry_size_is_set(entry)
			? qint64(archive_entry_size(entry)) : -1;
		if (declared > MAX_ENTRY_BYTES) {
			archive_read_free(a);
			fail(QLatin1String("the entry is larger than will be read"));
			return QByteArray();
		}

		QByteArray buffer;
		buffer.resize(int(READ_BLOCK));
		qint64 total = 0;
		while (true) {
			const ssize_t got = archive_read_data(a, buffer.data(), READ_BLOCK);
			if (got == 0) break;
			if (got < 0) {
				archive_read_free(a);
				fail(QString::fromUtf8(archive_error_string(a)
					? archive_error_string(a) : "could not read the entry"));
				return QByteArray();
			}
			total += got;
			/* Enforced while reading, not only against the header: a stream
			   that keeps producing bytes past its declared size would
			   otherwise be bounded only by available memory. */
			if (total > MAX_ENTRY_BYTES) {
				archive_read_free(a);
				fail(QLatin1String("the entry produced more data than permitted"));
				return QByteArray();
			}
			out.append(buffer.constData(), int(got));
		}
		break;
	}

	archive_read_free(a);
	if (!found) {
		fail(QLatin1String("no such entry: ") + path);
		return QByteArray();
	}
	return out;
}

#endif /* PEBEAR_WITH_LIBARCHIVE */
