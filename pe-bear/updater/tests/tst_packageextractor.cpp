/*
 * Covers the extraction driver of #19: the policy applied, the filesystem
 * written, and the steps recorded for the journal.
 *
 * Both dependencies are fakes, which is what lets the interesting cases exist
 * at all -- an archive whose last entry is malicious, a disk that fails on the
 * fourth write, a body that disagrees with its own header.
 */
#include <QtTest>
#include "../PackageExtractor.h"
#include "FakeFileSystem.h"

using namespace pe_bear::updater;

namespace {

	/** An archive described entirely in memory. */
	class FakeArchiveReader : public IArchiveReader
	{
	public:
		FakeArchiveReader() : m_openOk(true), m_opened(false), m_closes(0) {}

		void add(const ArchiveEntry &e, const QByteArray &body = QByteArray())
		{
			m_entries.append(e);
			if (e.kind == ArchiveEntry::KindFile) m_bodies.insert(e.path, body);
		}
		void addFile(const QString &path, const QByteArray &body)
		{
			add(ArchiveEntry(path, ArchiveEntry::KindFile, body.size(), body.size()), body);
		}
		void addDir(const QString &path) { add(ArchiveEntry(path, ArchiveEntry::KindDir)); }

		/** Claim a size in the header that the body does not match. */
		void addLyingFile(const QString &path, qint64 claimed, const QByteArray &body)
		{
			add(ArchiveEntry(path, ArchiveEntry::KindFile, claimed, body.size()), body);
		}

		void setOpenFails() { m_openOk = false; }
		bool wasClosed() const { return m_closes > 0; }

		virtual bool open(const QString &) { m_opened = m_openOk; return m_openOk; }
		virtual void close() { m_opened = false; m_closes++; }
		virtual QList<ArchiveEntry> entries() const { return m_entries; }
		virtual QByteArray readEntry(const QString &path)
		{
			/* keyed on the normalised path the policy produced */
			if (m_bodies.contains(path)) return m_bodies.value(path);
			foreach (const QString &k, m_bodies.keys()) {
				QString n = k;
				n.replace(QLatin1Char('\\'), QLatin1Char('/'));
				if (QDir::cleanPath(n) == path) return m_bodies.value(k);
			}
			return QByteArray();
		}
		virtual QString lastError() const { return QLatin1String("fake reader"); }

	private:
		QList<ArchiveEntry> m_entries;
		QMap<QString, QByteArray> m_bodies;
		bool m_openOk;
		bool m_opened;
		int m_closes;
	};

	const char* PKG  = "/var/dl/pkg.zip";
	const char* DEST = "/var/stage/tx-1";

}; // namespace

class TestPackageExtractor : public QObject
{
	Q_OBJECT

private slots:
	void extractsAPackageAndRecordsEveryStep();
	void createsMissingParentDirectories();
	void writesNothingWhenTheLastEntryIsRefused();
	void reportsThePolicyRejection();
	void refusesAnExistingDestination();
	void reportsAFailedOpen();
	void closesTheArchiveOnEveryPath();
	void refusesABodyThatDisagreesWithItsHeader();
	void reportsAFailedWriteAndKeepsTheStepsSoFar();
	void recordedStepsUndoTheExtraction();
	void anEmptyArchiveStillCreatesTheDestination();
};

void TestPackageExtractor::extractsAPackageAndRecordsEveryStep()
{
	FakeFileSystem fs;
	fs.addDir(QLatin1String("/var/stage"));
	FakeArchiveReader reader;
	reader.addDir(QLatin1String("PE-bear"));
	reader.addFile(QLatin1String("PE-bear/PE-bear.exe"), QByteArray("binary"));
	reader.addFile(QLatin1String("PE-bear/SIG.txt"), QByteArray("sigs"));

	PackageExtractor ex(&fs, &reader);
	QVERIFY2(ex.extract(QLatin1String(PKG), QLatin1String(DEST)), qPrintable(ex.lastError()));

	QCOMPARE(fs.contentOf(QString(DEST) + "/PE-bear/PE-bear.exe"), QByteArray("binary"));
	QCOMPARE(fs.contentOf(QString(DEST) + "/PE-bear/SIG.txt"), QByteArray("sigs"));

	/* the destination, the listed directory, and the two files */
	QCOMPARE(ex.ops().size(), 4);
	QCOMPARE(ex.ops().at(0).kind, TransactionOp::OpCreatedDir);
	QCOMPARE(ex.ops().at(0).from, QString(DEST));
	QCOMPARE(ex.ops().last().kind, TransactionOp::OpCreated);
	QVERIFY(reader.wasClosed());
}

void TestPackageExtractor::createsMissingParentDirectories()
{
	/* Archives often omit directory entries entirely. */
	FakeFileSystem fs;
	fs.addDir(QLatin1String("/var/stage"));
	FakeArchiveReader reader;
	reader.addFile(QLatin1String("PE-bear/platforms/qwindows.dll"), QByteArray("plugin"));

	PackageExtractor ex(&fs, &reader);
	QVERIFY2(ex.extract(QLatin1String(PKG), QLatin1String(DEST)), qPrintable(ex.lastError()));

	QVERIFY(fs.hasDir(QString(DEST) + "/PE-bear/platforms"));
	QCOMPARE(fs.contentOf(QString(DEST) + "/PE-bear/platforms/qwindows.dll"), QByteArray("plugin"));
}

void TestPackageExtractor::writesNothingWhenTheLastEntryIsRefused()
{
	/* The whole list is judged before anything is written, so a package that
	   is only malicious at the end leaves no partial tree behind -- a state a
	   later step could not tell from success. */
	FakeFileSystem fs;
	fs.addDir(QLatin1String("/var/stage"));
	FakeArchiveReader reader;
	reader.addFile(QLatin1String("PE-bear/ok1.dll"), QByteArray("a"));
	reader.addFile(QLatin1String("PE-bear/ok2.dll"), QByteArray("b"));
	reader.addFile(QLatin1String("../../etc/cron.d/evil"), QByteArray("c"));

	PackageExtractor ex(&fs, &reader);
	QVERIFY2(!ex.extract(QLatin1String(PKG), QLatin1String(DEST)), "a traversal entry was extracted");
	QCOMPARE(ex.rejection(), ExtractionPolicy::PathTraversal);

	QVERIFY2(!fs.hasDir(QLatin1String(DEST)), "the destination was created before judging");
	QVERIFY(!fs.hasFile(QString(DEST) + "/PE-bear/ok1.dll"));
	QCOMPARE(ex.ops().size(), 0);
}

void TestPackageExtractor::reportsThePolicyRejection()
{
	FakeFileSystem fs;
	fs.addDir(QLatin1String("/var/stage"));
	FakeArchiveReader reader;
	reader.addFile(QLatin1String("PE-bear/Qt6Core.dll"), QByteArray("a"));
	reader.addFile(QLatin1String("PE-bear/qt6core.dll"), QByteArray("b"));

	PackageExtractor ex(&fs, &reader);
	QVERIFY(!ex.extract(QLatin1String(PKG), QLatin1String(DEST)));
	QCOMPARE(ex.rejection(), ExtractionPolicy::CaseCollision);
	/* the message names the entry, so a user can act on it */
	QVERIFY(ex.lastError().contains(QLatin1String("qt6core.dll")));
}

void TestPackageExtractor::refusesAnExistingDestination()
{
	FakeFileSystem fs;
	fs.addDir(QLatin1String(DEST));
	FakeArchiveReader reader;
	reader.addFile(QLatin1String("PE-bear/x"), QByteArray("x"));

	PackageExtractor ex(&fs, &reader);
	QVERIFY2(!ex.extract(QLatin1String(PKG), QLatin1String(DEST)),
		"extracted over an existing directory");
	QCOMPARE(ex.ops().size(), 0);
}

void TestPackageExtractor::reportsAFailedOpen()
{
	FakeFileSystem fs;
	fs.addDir(QLatin1String("/var/stage"));
	FakeArchiveReader reader;
	reader.setOpenFails();

	PackageExtractor ex(&fs, &reader);
	QVERIFY(!ex.extract(QLatin1String(PKG), QLatin1String(DEST)));
	QVERIFY(!ex.lastError().isEmpty());
	QVERIFY(!fs.hasDir(QLatin1String(DEST)));
}

void TestPackageExtractor::closesTheArchiveOnEveryPath()
{
	FakeFileSystem fs;
	fs.addDir(QLatin1String("/var/stage"));

	/* refused by policy */
	FakeArchiveReader refused;
	refused.addFile(QLatin1String("../x"), QByteArray("x"));
	PackageExtractor a(&fs, &refused);
	QVERIFY(!a.extract(QLatin1String(PKG), QLatin1String(DEST)));
	QVERIFY2(refused.wasClosed(), "the archive was left open after a refusal");

	/* failed write */
	FakeFileSystem fs2;
	fs2.addDir(QLatin1String("/var/stage"));
	fs2.failAlways(QLatin1String("writeFile"));
	FakeArchiveReader ok;
	ok.addFile(QLatin1String("PE-bear/x"), QByteArray("x"));
	PackageExtractor b(&fs2, &ok);
	QVERIFY(!b.extract(QLatin1String(PKG), QLatin1String(DEST)));
	QVERIFY2(ok.wasClosed(), "the archive was left open after an I/O failure");
}

void TestPackageExtractor::refusesABodyThatDisagreesWithItsHeader()
{
	/* The size was judged against the policy from the header. A body that
	   disagrees means the archive is lying, and the limits that were checked
	   do not describe what would be written. */
	FakeFileSystem fs;
	fs.addDir(QLatin1String("/var/stage"));
	FakeArchiveReader reader;
	reader.addLyingFile(QLatin1String("PE-bear/x"), 10, QByteArray("much longer than ten"));

	PackageExtractor ex(&fs, &reader);
	QVERIFY2(!ex.extract(QLatin1String(PKG), QLatin1String(DEST)),
		"a body disagreeing with its header was written");
	QVERIFY(ex.lastError().contains(QLatin1String("does not match")));
}

void TestPackageExtractor::reportsAFailedWriteAndKeepsTheStepsSoFar()
{
	/* The steps already performed must survive the failure: they are what the
	   transaction replays to undo the partial extraction. */
	FakeFileSystem fs;
	fs.addDir(QLatin1String("/var/stage"));
	FakeArchiveReader reader;
	reader.addFile(QLatin1String("PE-bear/a.dll"), QByteArray("a"));
	reader.addFile(QLatin1String("PE-bear/b.dll"), QByteArray("b"));

	fs.failOnCall(QLatin1String("writeFile"), 2);
	PackageExtractor ex(&fs, &reader);
	QVERIFY(!ex.extract(QLatin1String(PKG), QLatin1String(DEST)));
	QVERIFY2(ex.ops().size() >= 2, "the completed steps were discarded");
	QCOMPARE(ex.ops().at(0).from, QString(DEST));
}

void TestPackageExtractor::recordedStepsUndoTheExtraction()
{
	/* The point of recording: replaying backwards must leave nothing behind. */
	FakeFileSystem fs;
	fs.addDir(QLatin1String("/var/stage"));
	FakeArchiveReader reader;
	reader.addDir(QLatin1String("PE-bear"));
	reader.addFile(QLatin1String("PE-bear/a.dll"), QByteArray("a"));
	reader.addFile(QLatin1String("PE-bear/sub/b.dll"), QByteArray("b"));

	PackageExtractor ex(&fs, &reader);
	QVERIFY2(ex.extract(QLatin1String(PKG), QLatin1String(DEST)), qPrintable(ex.lastError()));
	QVERIFY(fs.hasFile(QString(DEST) + "/PE-bear/sub/b.dll"));

	const QList<TransactionOp> ops = ex.ops();
	for (int i = ops.size() - 1; i >= 0; i--) {
		const TransactionOp &op = ops.at(i);
		if (op.kind == TransactionOp::OpCreated) fs.removeFile(op.from);
		else if (op.kind == TransactionOp::OpCreatedDir) fs.removeDirRecursively(op.from);
	}
	QVERIFY2(!fs.hasDir(QLatin1String(DEST)), "undoing the recorded steps left the tree behind");
	QVERIFY(fs.hasDir(QLatin1String("/var/stage")));
}

void TestPackageExtractor::anEmptyArchiveStillCreatesTheDestination()
{
	FakeFileSystem fs;
	fs.addDir(QLatin1String("/var/stage"));
	FakeArchiveReader reader;

	PackageExtractor ex(&fs, &reader);
	QVERIFY(ex.extract(QLatin1String(PKG), QLatin1String(DEST)));
	QVERIFY(fs.hasDir(QLatin1String(DEST)));
	QCOMPARE(ex.ops().size(), 1);
}

QTEST_GUILESS_MAIN(TestPackageExtractor)
#include "tst_packageextractor.moc"
