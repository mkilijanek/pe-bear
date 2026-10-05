/*
 * Covers the libarchive-backed reader chosen for #20.
 *
 * Archives are written by libarchive itself inside each test, rather than
 * checked in or produced by external tools. Three reasons: the fixtures stay
 * readable as code, no zip/tar binary has to exist on the host, and a
 * malicious archive -- traversal, a symlink, an absolute path -- can be
 * constructed deliberately, which is awkward to do with a command-line packer
 * that tries to stop you.
 */
#include <QtTest>
#include "../LibArchiveReader.h"

#ifdef PEBEAR_WITH_LIBARCHIVE

#include <archive.h>
#include <archive_entry.h>

using namespace pe_bear::updater;

namespace {

	struct Spec
	{
		Spec(const QString &p, const QByteArray &b, mode_t t = AE_IFREG, const QString &link = QString())
			: path(p), body(b), type(t), linkTarget(link) {}

		QString path;
		QByteArray body;
		mode_t type;
		QString linkTarget;
	};

	/** Writes an archive of the given format, returning false on failure. */
	bool writeArchive(const QString &path, const char *format, const QList<Spec> &specs)
	{
		struct archive *a = archive_write_new();
		if (!a) return false;

		if (qstrcmp(format, "zip") == 0) {
			archive_write_set_format_zip(a);
		} else {
			archive_write_set_format_pax_restricted(a);
			archive_write_add_filter_xz(a);
		}
		if (archive_write_open_filename(a, QFile::encodeName(path).constData()) != ARCHIVE_OK) {
			archive_write_free(a);
			return false;
		}

		for (int i = 0; i < specs.size(); i++) {
			const Spec &s = specs.at(i);
			struct archive_entry *e = archive_entry_new();
			archive_entry_set_pathname(e, s.path.toUtf8().constData());
			archive_entry_set_filetype(e, s.type);
			archive_entry_set_perm(e, 0644);
			if (!s.linkTarget.isEmpty()) {
				archive_entry_set_symlink(e, s.linkTarget.toUtf8().constData());
			}
			archive_entry_set_size(e, s.type == AE_IFREG ? s.body.size() : 0);

			if (archive_write_header(a, e) != ARCHIVE_OK) {
				archive_entry_free(e);
				archive_write_free(a);
				return false;
			}
			if (s.type == AE_IFREG && !s.body.isEmpty()) {
				archive_write_data(a, s.body.constData(), size_t(s.body.size()));
			}
			archive_entry_free(e);
		}
		archive_write_close(a);
		archive_write_free(a);
		return true;
	}

	const ArchiveEntry* find(const QList<ArchiveEntry> &list, const QString &path)
	{
		for (int i = 0; i < list.size(); i++) {
			QString n = list.at(i).path;
			n.replace(QLatin1Char('\\'), QLatin1Char('/'));
			while (n.startsWith(QLatin1String("./"))) n.remove(0, 2);
			if (n == path) return &list.at(i);
		}
		return NULL;
	}

}; // namespace

class TestLibArchiveReader : public QObject
{
	Q_OBJECT

private slots:
	void readsAZip();
	void readsATarXz();
	void readsBodiesBackExactly_data();
	void readsBodiesBackExactly();
	void reportsDirectoriesAsDirectories();
	void reportsASymlinkAsALinkNotAFile();
	void preservesPathsVerbatimForThePolicyToJudge();
	void refusesAMissingFile();
	void refusesSomethingThatIsNotAnArchive();
	void refusesAFormatOutsideTheNarrowedSet();
	void readEntryOnAnUnopenedReaderFails();
	void readEntryOfAnUnknownNameFails();
	void closeResetsTheReader();
	void handlesAnEmptyArchive();
	void handlesAZeroLengthFile();
};

void TestLibArchiveReader::readsAZip()
{
	QTemporaryDir dir;
	const QString path = QDir(dir.path()).absoluteFilePath("pkg.zip");

	QList<Spec> specs;
	specs << Spec(QLatin1String("PE-bear/PE-bear.exe"), QByteArray("MZ-binary"))
	      << Spec(QLatin1String("PE-bear/SIG.txt"), QByteArray("signatures"));
	QVERIFY(writeArchive(path, "zip", specs));

	LibArchiveReader r;
	QVERIFY2(r.open(path), qPrintable(r.lastError()));
	const QList<ArchiveEntry> entries = r.entries();
	QCOMPARE(entries.size(), 2);
	QVERIFY(find(entries, QLatin1String("PE-bear/PE-bear.exe")) != NULL);
	QCOMPARE(find(entries, QLatin1String("PE-bear/SIG.txt"))->kind, ArchiveEntry::KindFile);
}

void TestLibArchiveReader::readsATarXz()
{
	/* The Linux packages are .tar.xz, so the xz filter has to be enabled --
	   and only xz and gzip are, deliberately. */
	QTemporaryDir dir;
	const QString path = QDir(dir.path()).absoluteFilePath("pkg.tar.xz");

	QList<Spec> specs;
	specs << Spec(QLatin1String("PE-bear/PE-bear"), QByteArray("elf-binary"));
	QVERIFY(writeArchive(path, "tarxz", specs));

	LibArchiveReader r;
	QVERIFY2(r.open(path), qPrintable(r.lastError()));
	QCOMPARE(r.entries().size(), 1);
	QCOMPARE(r.readEntry(QLatin1String("PE-bear/PE-bear")), QByteArray("elf-binary"));
}

void TestLibArchiveReader::readsBodiesBackExactly_data()
{
	QTest::addColumn<QString>("format");
	QTest::addColumn<int>("size");

	QTest::newRow("zip small") << "zip" << 11;
	QTest::newRow("zip block boundary") << "zip" << 64 * 1024;
	QTest::newRow("zip over a block") << "zip" << 64 * 1024 + 7;
	QTest::newRow("tarxz small") << "tarxz" << 11;
	QTest::newRow("tarxz over a block") << "tarxz" << 64 * 1024 + 7;
}

void TestLibArchiveReader::readsBodiesBackExactly()
{
	QFETCH(QString, format);
	QFETCH(int, size);

	/* Crossing the 64 KiB read block is where a loop bug would show. */
	QByteArray body;
	body.resize(size);
	for (int i = 0; i < size; i++) body[i] = char((i * 31 + 7) % 251);

	QTemporaryDir dir;
	const QString path = QDir(dir.path()).absoluteFilePath("pkg.bin");
	QList<Spec> specs;
	specs << Spec(QLatin1String("PE-bear/data"), body);
	QVERIFY(writeArchive(path, format.toUtf8().constData(), specs));

	LibArchiveReader r;
	QVERIFY2(r.open(path), qPrintable(r.lastError()));
	const QByteArray back = r.readEntry(QLatin1String("PE-bear/data"));
	QCOMPARE(back.size(), size);
	QCOMPARE(back, body);
}

void TestLibArchiveReader::reportsDirectoriesAsDirectories()
{
	QTemporaryDir dir;
	const QString path = QDir(dir.path()).absoluteFilePath("pkg.zip");
	QList<Spec> specs;
	specs << Spec(QLatin1String("PE-bear/"), QByteArray(), AE_IFDIR)
	      << Spec(QLatin1String("PE-bear/x.dll"), QByteArray("dll"));
	QVERIFY(writeArchive(path, "zip", specs));

	LibArchiveReader r;
	QVERIFY(r.open(path));
	const QList<ArchiveEntry> entries = r.entries();
	bool sawDir = false;
	for (int i = 0; i < entries.size(); i++) {
		if (entries.at(i).kind == ArchiveEntry::KindDir) sawDir = true;
	}
	QVERIFY2(sawDir, "a directory entry was not reported as one");
}

void TestLibArchiveReader::reportsASymlinkAsALinkNotAFile()
{
	/* libarchive describes a symlink as a regular file with a link target set.
	   Reporting it as a file would hand ExtractionPolicy the one shape it
	   cannot refuse, so link-ness is checked first. */
	QTemporaryDir dir;
	const QString path = QDir(dir.path()).absoluteFilePath("pkg.tar.xz");
	QList<Spec> specs;
	specs << Spec(QLatin1String("PE-bear/real.dll"), QByteArray("real"))
	      << Spec(QLatin1String("PE-bear/evil"), QByteArray(), AE_IFLNK,
	              QLatin1String("/etc/shadow"));
	QVERIFY(writeArchive(path, "tarxz", specs));

	LibArchiveReader r;
	QVERIFY2(r.open(path), qPrintable(r.lastError()));
	const ArchiveEntry *link = find(r.entries(), QLatin1String("PE-bear/evil"));
	QVERIFY(link != NULL);
	QCOMPARE(link->kind, ArchiveEntry::KindSymlink);
	QCOMPARE(link->linkTarget, QString("/etc/shadow"));
}

void TestLibArchiveReader::preservesPathsVerbatimForThePolicyToJudge()
{
	/* The reader must not clean paths: deciding what is acceptable belongs to
	   ExtractionPolicy, and a decoder that normalised first would hide the
	   very shapes the policy exists to reject. */
	QTemporaryDir dir;
	const QString path = QDir(dir.path()).absoluteFilePath("evil.tar.xz");
	QList<Spec> specs;
	specs << Spec(QLatin1String("../../etc/cron.d/evil"), QByteArray("x"))
	      << Spec(QLatin1String("/absolute/path"), QByteArray("y"));
	QVERIFY(writeArchive(path, "tarxz", specs));

	LibArchiveReader r;
	QVERIFY2(r.open(path), qPrintable(r.lastError()));
	const QList<ArchiveEntry> entries = r.entries();
	QCOMPARE(entries.size(), 2);

	bool sawTraversal = false;
	for (int i = 0; i < entries.size(); i++) {
		if (entries.at(i).path.contains(QLatin1String(".."))) sawTraversal = true;
	}
	QVERIFY2(sawTraversal, "the reader cleaned a traversal path instead of reporting it");
}

void TestLibArchiveReader::refusesAMissingFile()
{
	LibArchiveReader r;
	QVERIFY(!r.open(QLatin1String("/no/such/archive.zip")));
	QVERIFY(!r.lastError().isEmpty());
	QVERIFY(!r.isOpen());
}

void TestLibArchiveReader::refusesSomethingThatIsNotAnArchive()
{
	QTemporaryDir dir;
	const QString path = QDir(dir.path()).absoluteFilePath("not-an-archive.zip");
	QFile f(path);
	QVERIFY(f.open(QIODevice::WriteOnly));
	f.write(QByteArray(4096, 'A'));
	f.close();

	LibArchiveReader r;
	QVERIFY2(!r.open(path), "random bytes were accepted as an archive");
	QVERIFY(!r.lastError().isEmpty());
}

void TestLibArchiveReader::refusesAFormatOutsideTheNarrowedSet()
{
	/* Only zip and tar, with xz and gzip filters, are enabled. A cpio is a
	   valid archive that this reader must still refuse -- enabling every
	   format would add decoder code reachable by a hostile file for no gain. */
	QTemporaryDir dir;
	const QString path = QDir(dir.path()).absoluteFilePath("pkg.cpio");

	struct archive *a = archive_write_new();
	QVERIFY(a != NULL);
	archive_write_set_format_cpio(a);
	QCOMPARE(archive_write_open_filename(a, QFile::encodeName(path).constData()), ARCHIVE_OK);
	struct archive_entry *e = archive_entry_new();
	archive_entry_set_pathname(e, "PE-bear/x");
	archive_entry_set_filetype(e, AE_IFREG);
	archive_entry_set_size(e, 1);
	archive_entry_set_perm(e, 0644);
	archive_write_header(a, e);
	archive_write_data(a, "x", 1);
	archive_entry_free(e);
	archive_write_close(a);
	archive_write_free(a);

	LibArchiveReader r;
	QVERIFY2(!r.open(path), "a cpio was accepted despite the narrowed format set");
}

void TestLibArchiveReader::readEntryOnAnUnopenedReaderFails()
{
	LibArchiveReader r;
	QVERIFY(r.readEntry(QLatin1String("anything")).isEmpty());
	QVERIFY(!r.lastError().isEmpty());
}

void TestLibArchiveReader::readEntryOfAnUnknownNameFails()
{
	QTemporaryDir dir;
	const QString path = QDir(dir.path()).absoluteFilePath("pkg.zip");
	QList<Spec> specs;
	specs << Spec(QLatin1String("PE-bear/x.dll"), QByteArray("dll"));
	QVERIFY(writeArchive(path, "zip", specs));

	LibArchiveReader r;
	QVERIFY(r.open(path));
	QVERIFY(r.readEntry(QLatin1String("PE-bear/absent.dll")).isEmpty());
	QVERIFY(r.lastError().contains(QLatin1String("no such entry")));
}

void TestLibArchiveReader::closeResetsTheReader()
{
	QTemporaryDir dir;
	const QString path = QDir(dir.path()).absoluteFilePath("pkg.zip");
	QList<Spec> specs;
	specs << Spec(QLatin1String("PE-bear/x.dll"), QByteArray("dll"));
	QVERIFY(writeArchive(path, "zip", specs));

	LibArchiveReader r;
	QVERIFY(r.open(path));
	QCOMPARE(r.entries().size(), 1);
	r.close();
	QVERIFY(!r.isOpen());
	QCOMPARE(r.entries().size(), 0);
	QVERIFY(r.readEntry(QLatin1String("PE-bear/x.dll")).isEmpty());
}

void TestLibArchiveReader::handlesAnEmptyArchive()
{
	QTemporaryDir dir;
	const QString path = QDir(dir.path()).absoluteFilePath("empty.zip");
	QVERIFY(writeArchive(path, "zip", QList<Spec>()));

	LibArchiveReader r;
	QVERIFY2(r.open(path), qPrintable(r.lastError()));
	QCOMPARE(r.entries().size(), 0);
}

void TestLibArchiveReader::handlesAZeroLengthFile()
{
	QTemporaryDir dir;
	const QString path = QDir(dir.path()).absoluteFilePath("pkg.zip");
	QList<Spec> specs;
	specs << Spec(QLatin1String("PE-bear/empty.txt"), QByteArray());
	QVERIFY(writeArchive(path, "zip", specs));

	LibArchiveReader r;
	QVERIFY(r.open(path));
	const ArchiveEntry *e = find(r.entries(), QLatin1String("PE-bear/empty.txt"));
	QVERIFY(e != NULL);
	QCOMPARE(e->uncompressedSize, Q_INT64_C(0));
	QCOMPARE(r.readEntry(QLatin1String("PE-bear/empty.txt")), QByteArray());
}

QTEST_GUILESS_MAIN(TestLibArchiveReader)
#include "tst_libarchivereader.moc"

#else /* PEBEAR_WITH_LIBARCHIVE */

/* Built without libarchive, so there is nothing to exercise. Reported as a
   skip rather than omitted, so the absence is visible in the ctest output
   instead of looking like full coverage. */
#include <QtTest>
class TestLibArchiveReader : public QObject
{
	Q_OBJECT
private slots:
	void skipped() { QSKIP("built without libarchive (PEBEAR_WITH_LIBARCHIVE=OFF)"); }
};
QTEST_GUILESS_MAIN(TestLibArchiveReader)
#include "tst_libarchivereader.moc"

#endif
